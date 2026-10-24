// Local LPC 2026 simulator for benchmarking strategy variants offline.
//
// It implements the published rules: turn phases, mining-streak priority,
// territory claims, construction order, deposit respawns and fog of war, and
// feeds our strategy the same JSON view the server sends. Map generation
// details the rules leave open (team placement, mountain ranges) and the three
// practice bots are approximations, so use it for relative comparisons and
// confirm conclusions with lpc.sh practice matches.
//
// Usage: lpc-sim [--seeds 1:20] [--variants full,baseline]
//                [--opponents prospector:15] [--params '{...}'|file]
//                [--csv results.csv] [--turns 480]

#include "adaptive_hybrid.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace sim {
using ah::Json;

struct Config {
  int width = 50, height = 50, maxTurns = 480;
  int goldPerTeam = 3, ironPerTeam = 2, goldMin = 6, goldMax = 9;
  int centerW = 16, centerH = 16, centerMin = 12, centerMax = 18;
  double centerPct = 0.35, mountain = 0.2, crater = 0.1;
  int obstacleCost = 3, startIron = 15, capacity = 8, vision = 3;
};

enum Terrain : std::uint8_t { EMPTY, MOUNTAIN, CRATER };
enum Res : std::uint8_t { NONE, GOLD, IRON };
enum Act : std::uint8_t { PASS, MOVE, MINE, DEPOSIT, TRADE_IRON, TRADE_GOLD, SPAWN, BUILD };

struct Unit {
  std::string id;
  int team = 0, x = 0, y = 0, gold = 0, iron = 0, ready = 0, streak = 0, occ = 0;
};
struct Base {
  std::string id;
  int team = 0, x = 0, y = 0;
};
struct Order {
  Act act = PASS;
  int dir = -1;
};
struct Counters {
  long move = 0, mine = 0, mineFail = 0, deposit = 0, idle = 0, cooldown = 0,
       failed = 0, spawn = 0, build = 0, trade = 0;
};
struct Known {
  std::vector<std::int8_t> terrain;  // -1 unknown
  std::vector<std::int8_t> res;
  std::vector<int> amount, seen;
};

struct Team {
  std::string id, kind;
  int gold = 0, iron = 0, recruited = 0, unitSeq = 1, baseSeq = 1;
  std::unique_ptr<ah::Brain> brain;
  Known known;
  std::mt19937 rng;
  std::map<std::string, int> memo;  // per-worker walk direction or target
  std::map<std::string, int> buildTarget;
  Counters c;
  double msTotal = 0, msMax = 0;
  int decisions = 0;
  std::vector<int> curve;  // banked gold every 40 turns
};

struct Result {
  std::string variant;
  std::uint64_t seed = 0;
  int gold = 0, iron = 0, carriedGold = 0, carriedIron = 0, bases = 0,
      workers = 0, place = 0, teams = 0, errors = 0;
  double msAvg = 0, msMax = 0;
  Counters c;
  std::vector<int> curve;
  int bestOpponent = 0;
  double meanOpponent = 0;
};

class World {
public:
  World(const Config &cfg, std::uint64_t seed, const std::string &variant,
        const ah::Params &params, const std::vector<std::string> &bots)
      : C(cfg), W(cfg.width), H(cfg.height), N(cfg.width * cfg.height),
        rng(seed * 0x9E3779B97F4A7C15ULL + 12345), seed_(seed) {
    teams.emplace_back();
    teams[0].id = "team-ah", teams[0].kind = variant;
    ah::Params p = params;
    p.persist = false, p.logEvery = 0;
    teams[0].brain = std::make_unique<ah::Brain>(p);
    for (std::size_t i = 0; i < bots.size(); ++i) {
      teams.emplace_back();
      teams.back().id = "bot-" + std::to_string(i + 1);
      teams.back().kind = bots[i];
    }
    for (std::size_t t = 0; t < teams.size(); ++t) {
      teams[t].rng.seed(static_cast<unsigned>(seed * 131 + t));
      teams[t].iron = C.startIron;
      teams[t].known.terrain.assign(N, -1);
      teams[t].known.res.assign(N, NONE);
      teams[t].known.amount.assign(N, 0);
      teams[t].known.seen.assign(N, -1);
    }
    cx0 = (W - C.centerW) / 2, cy0 = (H - C.centerH) / 2;
    generate();
    order.resize(teams.size());
    std::iota(order.begin(), order.end(), 0);
    std::shuffle(order.begin(), order.end(), rng);
  }

  Result run() {
    for (turn = 0; turn < C.maxTurns; ++turn) {
      if (turn % 40 == 0)
        for (Team &t : teams)
          t.curve.push_back(t.gold);
      std::vector<Order> orders(units.size());
      for (int t = 0; t < static_cast<int>(teams.size()); ++t) {
        const auto vis = visibility(t);
        updateKnown(t, vis);
        if (teams[t].brain)
          decideBrain(t, vis, orders);
        else
          decideBot(t, vis, orders);
      }
      resolve(orders);
    }
    for (Team &t : teams)
      t.curve.push_back(t.gold);
    return result();
  }

private:
  Config C;
  int W, H, N, cx0 = 0, cy0 = 0, turn = 0;
  std::mt19937_64 rng;
  std::uint64_t seed_;
  std::vector<std::uint8_t> terrain, res;
  std::vector<int> amount;
  std::vector<Unit> units;
  std::vector<Base> bases;
  std::vector<Team> teams;
  std::vector<int> order;
  struct Due {
    int turn;
    Res res;
    int old;
  };
  std::vector<Due> due;

  int X(int i) const { return i % W; }
  int Y(int i) const { return i / W; }
  bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < W && y < H; }
  bool inCenter(int x, int y) const {
    return x >= cx0 && x < cx0 + C.centerW && y >= cy0 && y < cy0 + C.centerH;
  }
  int rnd(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); }
  double unit01() { return std::uniform_real_distribution<double>(0, 1)(rng); }
  static int cheb(int ax, int ay, int bx, int by) {
    return std::max(std::abs(ax - bx), std::abs(ay - by));
  }
  int baseAt(int x, int y) const {
    for (int b = 0; b < static_cast<int>(bases.size()); ++b)
      if (bases[b].x == x && bases[b].y == y)
        return b;
    return -1;
  }
  bool enemyBaseAt(int x, int y, int team) const {
    const int b = baseAt(x, y);
    return b >= 0 && bases[b].team != team;
  }
  int unitsOn(int x, int y) const {
    int n = 0;
    for (const Unit &u : units)
      n += u.x == x && u.y == y;
    return n;
  }
  // Teams whose bases claim (x, y); empty means unclaimed.
  bool mayMine(int x, int y, int team) const {
    bool claimed = false;
    for (const Base &b : bases)
      if (std::abs(b.x - x) + std::abs(b.y - y) <= 2) {
        if (b.team == team)
          return true;
        claimed = true;
      }
    return !claimed;
  }

  // ------------------------------------------------------------ map setup
  void generate() {
    terrain.assign(N, EMPTY), res.assign(N, NONE), amount.assign(N, 0);
    std::vector<char> protect(N, 0);
    // Real matches place starting bases as close as 3 tiles apart.
    int spacing = 4;
    for (int t = 0; t < static_cast<int>(teams.size()); ++t) {
      for (int attempt = 0;; ++attempt) {
        if (attempt > 0 && attempt % 3000 == 0)
          spacing = std::max(3, spacing - 1);
        const int x = rnd(2, W - 3), y = rnd(2, H - 3);
        if (inCenter(x, y))
          continue;
        bool ok = true;
        for (const Base &b : bases)
          ok = ok && cheb(x, y, b.x, b.y) >= spacing;
        if (!ok)
          continue;
        Team &team = teams[t];
        bases.push_back({team.id + "-base-1", t, x, y});
        int wx, wy;
        do {
          const int k = rnd(0, 7);
          wx = x + ah::DX[k], wy = y + ah::DY[k];
        } while (!inside(wx, wy));
        units.push_back({team.id + "-unit-1", t, wx, wy});
        for (int dy = -1; dy <= 1; ++dy)
          for (int dx = -1; dx <= 1; ++dx)
            if (inside(x + dx, y + dy))
              protect[(y + dy) * W + x + dx] = 1;
        protect[wy * W + wx] = 1;
        break;
      }
    }
    // Mountain ranges (random walks up to 10 cells) then scattered craters.
    const int mountains = static_cast<int>(C.mountain * N);
    for (int placed = 0, guard = 0; placed < mountains && guard < N * 20; ++guard) {
      int x = rnd(0, W - 1), y = rnd(0, H - 1);
      const int size = rnd(2, 10);
      for (int s = 0; s < size && placed < mountains; ++s) {
        const int i = y * W + x;
        if (!protect[i] && terrain[i] == EMPTY)
          terrain[i] = MOUNTAIN, ++placed;
        const int k = rnd(0, 3) * 2;
        if (inside(x + ah::DX[k], y + ah::DY[k]))
          x += ah::DX[k], y += ah::DY[k];
      }
    }
    // Craters come in 2x2 and 3x3 formations in real matches.
    const int craters = static_cast<int>(C.crater * N);
    for (int placed = 0, guard = 0; placed < craters && guard < N * 20; ++guard) {
      const int size = rnd(2, 3), x0 = rnd(0, W - size), y0 = rnd(0, H - size);
      for (int dy = 0; dy < size; ++dy)
        for (int dx = 0; dx < size; ++dx) {
          const int i = (y0 + dy) * W + x0 + dx;
          if (!protect[i] && terrain[i] == EMPTY && placed < craters)
            terrain[i] = CRATER, ++placed;
        }
    }
    const int gold = C.goldPerTeam * static_cast<int>(teams.size());
    const int centerGold = static_cast<int>(std::lround(C.centerPct * gold));
    for (int g = 0; g < gold; ++g)
      place(GOLD, g < centerGold ? 1 : 2, -1);
    for (int i = 0; i < C.ironPerTeam * static_cast<int>(teams.size()); ++i)
      place(IRON, 0, -1);
  }

  // zone: 0 anywhere, 1 center, 2 outside the center.
  bool place(Res r, int zone, int old) {
    std::vector<int> preferred, relaxed, any;
    std::vector<char> occupied(N, 0);
    for (const Unit &u : units)
      occupied[u.y * W + u.x] = 1;
    for (const Base &b : bases)
      occupied[b.y * W + b.x] = 1;
    for (int i = 0; i < N; ++i) {
      const int x = X(i), y = Y(i);
      if (terrain[i] != EMPTY || res[i] != NONE || occupied[i])
        continue;
      if (zone == 1 && !inCenter(x, y))
        continue;
      if (zone == 2 && inCenter(x, y))
        continue;
      const bool farOld = old < 0 || cheb(x, y, X(old), Y(old)) >= 2;
      bool farOthers = true;
      for (int dy = -1; dy <= 1 && farOthers; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
          if (inside(x + dx, y + dy) && res[(y + dy) * W + x + dx] != NONE) {
            farOthers = false;
            break;
          }
      any.push_back(i);
      if (farOld)
        relaxed.push_back(i);
      if (farOld && farOthers)
        preferred.push_back(i);
    }
    const auto &pool = !preferred.empty() ? preferred : !relaxed.empty() ? relaxed : any;
    if (pool.empty()) {
      if (zone != 0)
        return place(r, zone == 1 ? 2 : 1, old);
      return false;
    }
    const int i = pool[rnd(0, static_cast<int>(pool.size()) - 1)];
    res[i] = r;
    if (r == IRON)
      amount[i] = rnd(2, 4);
    else
      amount[i] = inCenter(X(i), Y(i)) ? rnd(C.centerMin, C.centerMax)
                                       : rnd(C.goldMin, C.goldMax);
    return true;
  }

  // -------------------------------------------------------------- vision
  std::vector<std::uint8_t> visibility(int team) const {
    std::vector<std::uint8_t> vis(N, 0);
    auto reveal = [&](int x, int y) {
      for (int dy = -C.vision; dy <= C.vision; ++dy)
        for (int dx = -C.vision; dx <= C.vision; ++dx)
          if (std::abs(dx) + std::abs(dy) <= C.vision && inside(x + dx, y + dy))
            vis[(y + dy) * W + x + dx] = 1;
      if (terrain[y * W + x] != EMPTY)
        for (int k = 0; k < 8; ++k)
          if (inside(x + ah::DX[k], y + ah::DY[k]))
            vis[(y + ah::DY[k]) * W + x + ah::DX[k]] = 1;
    };
    for (const Unit &u : units)
      if (u.team == team)
        reveal(u.x, u.y);
    for (const Base &b : bases)
      if (b.team == team)
        reveal(b.x, b.y);
    return vis;
  }

  void updateKnown(int team, const std::vector<std::uint8_t> &vis) {
    Known &k = teams[team].known;
    for (int i = 0; i < N; ++i)
      if (vis[i])
        k.terrain[i] = static_cast<std::int8_t>(terrain[i]),
        k.res[i] = static_cast<std::int8_t>(res[i]), k.amount[i] = amount[i],
        k.seen[i] = turn;
  }

  // --------------------------------------------------- our strategy (JSON)
  Json stateFor(int team, const std::vector<std::uint8_t> &vis) const {
    static const char *names[] = {"empty", "mountain", "crater"};
    Json cells = Json::array();
    cells.get_ref<Json::array_t &>().reserve(N);
    for (int i = 0; i < N; ++i) {
      if (!vis[i])
        cells.push_back({{"type", "hidden"}});
      else if (res[i] != NONE)
        cells.push_back({{"type", res[i] == GOLD ? "gold" : "iron"}, {"amount", amount[i]}});
      else
        cells.push_back({{"type", names[terrain[i]]}});
    }
    Json unitList = Json::array();
    for (const Unit &u : units) {
      if (!vis[u.y * W + u.x])
        continue;
      Json j = {{"unitId", u.id},
                {"teamId", teams[u.team].id},
                {"position", {{"x", u.x}, {"y", u.y}}},
                {"inventoryCapacity", C.capacity},
                {"readyOnTurn", u.ready}};
      if (u.team == team)
        j["inventory"] = {{"gold", u.gold}, {"iron", u.iron}};
      unitList.push_back(j);
    }
    Json baseList = Json::array();
    for (const Base &b : bases)
      if (vis[b.y * W + b.x])
        baseList.push_back({{"baseId", b.id},
                            {"teamId", teams[b.team].id},
                            {"position", {{"x", b.x}, {"y", b.y}}}});
    Json scores = Json::object(), teamList = Json::array();
    for (const Team &t : teams) {
      scores[t.id] = t.gold;
      teamList.push_back({{"id", t.id}, {"displayName", t.id}, {"botType", t.brain ? Json(false) : Json(t.kind)}});
    }
    Json view = {
        {"turn", turn},
        {"map", {{"width", W}, {"height", H}, {"cells", std::move(cells)}}},
        {"highGoldArea",
         {{"width", C.centerW}, {"height", C.centerH}, {"min", C.centerMin},
          {"max", C.centerMax}, {"spawnPercent", C.centerPct * 100}}},
        {"units", std::move(unitList)},
        {"bases", std::move(baseList)},
        {"scores", scores},
        {"teamResources", {{teams[team].id, {{"gold", teams[team].gold}, {"iron", teams[team].iron}}}}},
        {"baseResourceDeposits", Json::object()},
        {"warnings", Json::array()},
        {"info", Json::array()},
        {"effects", Json::array()}};
    return {{"match",
             {{"matchId", "sim-" + std::to_string(seed_)},
              {"status", "active"},
              {"turn", turn},
              {"maxTurns", C.maxTurns},
              {"turnAdvanceMode", "client-action"},
              {"rulesVersion", "lpc2026"},
              {"teams", teamList}}},
            {"view", std::move(view)}};
  }

  void decideBrain(int team, const std::vector<std::uint8_t> &vis,
                   std::vector<Order> &orders) {
    Team &t = teams[team];
    const Json state = stateFor(team, vis);
    const auto started = std::chrono::steady_clock::now();
    const ah::Actions actions = t.brain->decide(state, t.id);
    const double ms = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - started)
                          .count();
    t.msTotal += ms, t.msMax = std::max(t.msMax, ms), ++t.decisions;
    for (int u = 0; u < static_cast<int>(units.size()); ++u) {
      if (units[u].team != team)
        continue;
      const auto it = actions.find(units[u].id);
      if (it == actions.end())
        continue;
      const Json &a = it->second;
      const std::string type = a.value("type", "");
      Order o;
      if (type == "move") {
        o.act = MOVE;
        const std::string dir = a.value("direction", "");
        for (int k = 0; k < 8; ++k)
          if (dir == ah::DIRECTIONS[k])
            o.dir = k;
        if (o.dir < 0)
          o.act = PASS, ++t.c.failed;
      } else if (type == "mine") {
        o.act = MINE;
      } else if (type == "deposit") {
        o.act = DEPOSIT;
      } else if (type == "trade") {
        o.act = a.value("from", "") == "gold" ? TRADE_GOLD : TRADE_IRON;
      } else if (type == "spawn") {
        o.act = SPAWN;
      } else if (type == "build-base") {
        o.act = BUILD;
      } else if (type != "pass") {
        ++t.c.failed;
      }
      orders[u] = o;
    }
  }

  // ----------------------------------------------------- practice bots
  int nearestOwnBase(int team, int x, int y) const {
    int best = -1, bestD = 1 << 30;
    for (int b = 0; b < static_cast<int>(bases.size()); ++b)
      if (bases[b].team == team) {
        const int d = cheb(x, y, bases[b].x, bases[b].y);
        if (d < bestD)
          bestD = d, best = b;
      }
    return best;
  }

  int stepToward(Team &t, const Unit &u, int tx, int ty) {
    int best = -1;
    double bestScore = cheb(u.x, u.y, tx, ty) * 10.0 + 5;
    for (int k = 0; k < 8; ++k) {
      const int nx = u.x + ah::DX[k], ny = u.y + ah::DY[k];
      if (!inside(nx, ny) || enemyBaseAt(nx, ny, u.team))
        continue;
      const int known = t.known.terrain[ny * W + nx];
      double score = cheb(nx, ny, tx, ty) * 10.0 +
                     (known == MOUNTAIN || known == CRATER ? (C.obstacleCost - 1) * 10 : 0) +
                     std::uniform_real_distribution<double>(0, 1)(t.rng);
      if (score < bestScore)
        bestScore = score, best = k;
    }
    return best;
  }

  Order moveOrder(Team &t, const Unit &u, int tx, int ty) {
    const int k = stepToward(t, u, tx, ty);
    return k >= 0 ? Order{MOVE, k} : Order{};
  }

  Order goHome(Team &t, const Unit &u) {
    const int b = nearestOwnBase(u.team, u.x, u.y);
    if (b < 0)
      return {};
    if (bases[b].x == u.x && bases[b].y == u.y)
      return {DEPOSIT, -1};
    return moveOrder(t, u, bases[b].x, bases[b].y);
  }

  Order explore(Team &t, const Unit &u) {
    int &target = t.memo[u.id];
    if (target <= 0 || t.known.terrain[target - 1] != -1) {
      int best = -1;
      double bestD = 1e9;
      for (int i = 0; i < N; ++i) {
        if (t.known.terrain[i] != -1)
          continue;
        const double d = cheb(u.x, u.y, X(i), Y(i)) +
                         std::uniform_real_distribution<double>(0, 3)(t.rng);
        if (d < bestD)
          bestD = d, best = i;
      }
      if (best < 0)
        return randomWalk(t, u);
      target = best + 1;
    }
    return moveOrder(t, u, X(target - 1), Y(target - 1));
  }

  Order randomWalk(Team &t, const Unit &u) {
    int &dir = t.memo[u.id];
    for (int tries = 0; tries < 8; ++tries) {
      if (dir <= 0 || t.rng() % 10 == 0 || tries > 0)
        dir = static_cast<int>(t.rng() % 8) + 1;
      const int nx = u.x + ah::DX[dir - 1], ny = u.y + ah::DY[dir - 1];
      if (inside(nx, ny) && !enemyBaseAt(nx, ny, u.team))
        return {MOVE, dir - 1};
    }
    return {};
  }

  void decideBot(int team, const std::vector<std::uint8_t> &vis,
                 std::vector<Order> &orders) {
    Team &t = teams[team];
    const int R = C.maxTurns - turn;
    int ownBases = 0, carriedIron = 0;
    for (const Base &b : bases)
      ownBases += b.team == team;
    for (const Unit &u : units)
      if (u.team == team)
        carriedIron += u.iron;
    const bool invest = t.kind == "prospector" && turn < C.maxTurns / 2;
    const int nextCost = ah::spawn_cost(t.recruited);
    bool spawned = false;
    // Remembered deposits with their distance to the nearest own base.
    std::vector<std::pair<int, int>> known;
    if (t.kind != "greed")
      for (int i = 0; i < N; ++i)
        if (t.known.res[i] != NONE && t.known.amount[i] > 0) {
          const int hb = nearestOwnBase(team, X(i), Y(i));
          known.push_back({i, hb >= 0 ? cheb(X(i), Y(i), bases[hb].x, bases[hb].y) : 50});
        }

    if (invest && ownBases < 2 && t.iron >= 6 && t.buildTarget.empty()) {
      // Second base beside remembered gold far from the first one.
      for (int i = 0; i < N && t.buildTarget.empty(); ++i) {
        if (t.known.res[i] != GOLD || t.known.amount[i] <= 0)
          continue;
        const int b = nearestOwnBase(team, X(i), Y(i));
        if (b < 0 || cheb(X(i), Y(i), bases[b].x, bases[b].y) <= 10)
          continue;
        for (int k = 0; k < 8; ++k) {
          const int sx = X(i) + ah::DX[k], sy = Y(i) + ah::DY[k];
          if (!inside(sx, sy) || t.known.terrain[sy * W + sx] != EMPTY ||
              t.known.res[sy * W + sx] != NONE)
            continue;
          int builder = -1, bestD = 1 << 30;
          for (const Unit &u : units)
            if (u.team == team && cheb(u.x, u.y, sx, sy) < bestD)
              bestD = cheb(u.x, u.y, sx, sy), builder = &u - &units[0];
          if (builder >= 0)
            t.buildTarget[units[builder].id] = sy * W + sx;
          break;
        }
      }
    }

    for (int ui = 0; ui < static_cast<int>(units.size()); ++ui) {
      Unit &u = units[ui];
      if (u.team != team)
        continue;
      if (turn < u.ready) {
        orders[ui] = {};
        continue;
      }
      const int carried = u.gold + u.iron;
      const int here = u.y * W + u.x;
      const int b = nearestOwnBase(team, u.x, u.y);
      const bool onBase = b >= 0 && bases[b].x == u.x && bases[b].y == u.y;
      const int home = b >= 0 ? cheb(u.x, u.y, bases[b].x, bases[b].y) : 99;
      Order o;
      if (invest && !spawned && t.recruited < 3 && t.iron >= nextCost) {
        orders[ui] = {SPAWN, -1};
        spawned = true;
        continue;
      }
      if (auto it = t.buildTarget.find(u.id); it != t.buildTarget.end()) {
        const int s = it->second;
        if (!invest || t.iron < 6) {
          t.buildTarget.erase(it);
        } else if (s == here) {
          orders[ui] = {BUILD, -1};
          t.buildTarget.erase(it);
          continue;
        } else {
          orders[ui] = moveOrder(t, u, X(s), Y(s));
          continue;
        }
      }
      if (onBase && carried > 0) {
        o = {DEPOSIT, -1};
      } else if (carried >= C.capacity ||
                 (carried > 0 && t.kind != "greed" && R <= home + 3)) {
        o = goHome(t, u);
      } else if (t.kind == "greed") {
        // Novice: nearest visible resource, full loads, no memory.
        if (res[here] != NONE && amount[here] > 0 && mayMine(u.x, u.y, team)) {
          o = {MINE, -1};
        } else {
          int best = -1, bestD = 1 << 30;
          for (int dy = -C.vision - 1; dy <= C.vision + 1; ++dy)
            for (int dx = -C.vision - 1; dx <= C.vision + 1; ++dx) {
              const int x = u.x + dx, y = u.y + dy;
              if (!inside(x, y))
                continue;
              const int i = y * W + x;
              if (vis[i] && res[i] != NONE && amount[i] > 0 &&
                  cheb(u.x, u.y, x, y) < bestD && mayMine(x, y, team))
                bestD = cheb(u.x, u.y, x, y), best = i;
            }
          if (best >= 0)
            o = moveOrder(t, u, X(best), Y(best));
          else if (carried > 0)
            o = goHome(t, u);
          else
            o = randomWalk(t, u);
        }
      } else {
        // Beginner mines remembered gold; Intermediate also weighs the trip
        // home and collects iron while it is investing.
        const bool wantIron =
            invest && t.iron + carriedIron < nextCost + (ownBases < 2 ? 6 : 0);
        auto wanted = [&](int i) {
          return t.known.amount[i] > 0 &&
                 (t.known.res[i] == GOLD || (wantIron && t.known.res[i] == IRON)) &&
                 turn - t.known.seen[i] < 80 && mayMine(X(i), Y(i), team);
        };
        if (res[here] != NONE && amount[here] > 0 && wanted(here)) {
          o = {MINE, -1};
        } else {
          int best = -1;
          double bestScore = -1;
          for (const auto &[i, back] : known) {
            if (!wanted(i))
              continue;
            const int go = cheb(u.x, u.y, X(i), Y(i));
            const double score = t.kind == "prospector"
                                     ? t.known.amount[i] / (go + back + 1.0)
                                     : 1.0 / (go + 1.0);
            if (score > bestScore)
              bestScore = score, best = i;
          }
          if (best >= 0)
            o = moveOrder(t, u, X(best), Y(best));
          else if (carried > 0 && t.kind == "prospector")
            o = goHome(t, u);
          else
            o = explore(t, u);
        }
      }
      orders[ui] = o;
    }
  }

  // ---------------------------------------------------------- resolution
  void resolve(std::vector<Order> &orders) {
    // 1. Due deposits respawn.
    std::vector<Due> waiting;
    for (const Due &d : due) {
      if (d.turn > turn) {
        waiting.push_back(d);
        continue;
      }
      const int zone = d.res == IRON ? 0 : (unit01() < C.centerPct ? 1 : 2);
      if (!place(d.res, zone, d.old))
        waiting.push_back({turn + 1, d.res, d.old});
    }
    due = waiting;

    const int count = static_cast<int>(units.size());
    std::vector<char> moved(count, 0), acted(count, 0);
    for (int i = 0; i < count; ++i) {
      Unit &u = units[i];
      if (turn < u.ready) {
        if (orders[i].act != PASS)
          ++teams[u.team].c.cooldown;
        continue;
      }
      acted[i] = 1;
    }
    // 2. Moves.
    for (int i = 0; i < count; ++i) {
      if (!acted[i] || orders[i].act != MOVE)
        continue;
      Unit &u = units[i];
      const int nx = u.x + ah::DX[orders[i].dir], ny = u.y + ah::DY[orders[i].dir];
      if (!inside(nx, ny) || enemyBaseAt(nx, ny, u.team)) {
        ++teams[u.team].c.failed;
        continue;
      }
      u.x = nx, u.y = ny, u.streak = 0, u.occ = 0, moved[i] = 1;
      if (terrain[ny * W + nx] != EMPTY)
        u.ready = turn + C.obstacleCost;
      ++teams[u.team].c.move;
    }
    // 3. Construction, in rotating team order then worker order.
    for (int team : order)
      for (int i = 0; i < count; ++i) {
        Unit &u = units[i];
        if (u.team != team || !acted[i])
          continue;
        Team &t = teams[team];
        if (orders[i].act == SPAWN) {
          const int cost = ah::spawn_cost(t.recruited);
          if (t.iron < cost) {
            ++t.c.failed;
            continue;
          }
          t.iron -= cost, ++t.recruited, ++t.c.spawn;
          Unit recruit;
          recruit.id = t.id + "-unit-" + std::to_string(++t.unitSeq);
          recruit.team = team, recruit.x = u.x, recruit.y = u.y, recruit.ready = turn + 1;
          units.push_back(recruit);
        } else if (orders[i].act == BUILD) {
          const int cell = u.y * W + u.x;
          bool ok = t.iron >= 6 && terrain[cell] == EMPTY && res[cell] == NONE &&
                    baseAt(u.x, u.y) < 0 && unitsOn(u.x, u.y) == 1;
          for (const Base &b : bases)
            ok = ok && (b.team == team || std::abs(b.x - u.x) + std::abs(b.y - u.y) > 2);
          if (!ok) {
            ++t.c.failed;
            continue;
          }
          t.iron -= 6, ++t.c.build;
          bases.push_back({t.id + "-base-" + std::to_string(++t.baseSeq), team, u.x, u.y});
        }
      }
    // 4. Mining, with streak and occupancy priority on scarce deposits.
    std::map<int, std::vector<int>> miners;
    for (int i = 0; i < count; ++i) {
      Unit &u = units[i];
      if (!acted[i])
        continue;
      if (orders[i].act != MINE) {
        u.streak = 0;
        continue;
      }
      const int cell = u.y * W + u.x;
      if (res[cell] == NONE || amount[cell] <= 0 || u.gold + u.iron >= C.capacity ||
          !mayMine(u.x, u.y, u.team)) {
        u.streak = 0;
        ++teams[u.team].c.mineFail;
        continue;
      }
      miners[cell].push_back(i);
    }
    for (auto &[cell, list] : miners) {
      std::shuffle(list.begin(), list.end(), rng);
      std::stable_sort(list.begin(), list.end(), [&](int a, int b) {
        if (units[a].streak != units[b].streak)
          return units[a].streak > units[b].streak;
        return units[a].occ > units[b].occ;
      });
      const Res r = static_cast<Res>(res[cell]);
      for (int i : list) {
        Unit &u = units[i];
        ++u.streak;
        if (amount[cell] <= 0)
          continue;
        --amount[cell];
        (r == GOLD ? u.gold : u.iron) += 1;
        ++teams[u.team].c.mine;
      }
      if (amount[cell] <= 0) {
        res[cell] = NONE, amount[cell] = 0;
        due.push_back({turn + 1, r, cell});
      }
    }
    // 5. Trades. 6. Deposits.
    for (int i = 0; i < count; ++i) {
      Unit &u = units[i];
      if (!acted[i])
        continue;
      Team &t = teams[u.team];
      const Act a = orders[i].act;
      if (a == TRADE_IRON || a == TRADE_GOLD) {
        int &from = a == TRADE_IRON ? u.iron : u.gold;
        int &to = a == TRADE_IRON ? u.gold : u.iron;
        if (from >= 3)
          from -= 3, to += 1, ++t.c.trade;
        else
          ++t.c.failed;
      }
    }
    for (int i = 0; i < count; ++i) {
      Unit &u = units[i];
      if (!acted[i])
        continue;
      Team &t = teams[u.team];
      if (orders[i].act == DEPOSIT) {
        const int b = baseAt(u.x, u.y);
        if (b < 0 || bases[b].team != u.team) {
          ++t.c.failed;
          continue;
        }
        t.gold += u.gold, t.iron += u.iron, u.gold = u.iron = 0, ++t.c.deposit;
      } else if (orders[i].act == PASS) {
        ++t.c.idle;
      }
    }
    for (int i = 0; i < count; ++i)
      if (!moved[i])
        ++units[i].occ;
    std::rotate(order.begin(), order.begin() + 1, order.end());
  }

  Result result() const {
    struct Rank {
      int team, gold, iron, cg, ci, bases, workers;
    };
    std::vector<Rank> ranks;
    for (int t = 0; t < static_cast<int>(teams.size()); ++t) {
      Rank r{t, teams[t].gold, teams[t].iron, 0, 0, 0, 0};
      for (const Unit &u : units)
        if (u.team == t)
          r.cg += u.gold, r.ci += u.iron, ++r.workers;
      for (const Base &b : bases)
        r.bases += b.team == t;
      ranks.push_back(r);
    }
    auto key = [](const Rank &r) {
      return std::make_tuple(r.gold, r.iron, r.cg, r.ci, r.bases, r.workers);
    };
    const Rank &us = ranks[0];
    int place = 1, bestOpponent = 0;
    double opponentTotal = 0;
    for (const Rank &r : ranks)
      if (r.team != 0) {
        place += key(r) > key(us);
        bestOpponent = std::max(bestOpponent, r.gold);
        opponentTotal += r.gold;
      }
    const Team &t = teams[0];
    Result out;
    out.variant = t.kind, out.seed = seed_, out.gold = us.gold, out.iron = us.iron;
    out.carriedGold = us.cg, out.carriedIron = us.ci, out.bases = us.bases;
    out.workers = us.workers, out.place = place;
    out.teams = static_cast<int>(teams.size());
    out.errors = t.brain->errors();
    out.msAvg = t.decisions ? t.msTotal / t.decisions : 0, out.msMax = t.msMax;
    out.c = t.c, out.curve = t.curve, out.bestOpponent = bestOpponent;
    out.meanOpponent = ranks.size() > 1 ? opponentTotal / (ranks.size() - 1) : 0;
    return out;
  }
};

std::vector<std::string> split(const std::string &s, char sep) {
  std::vector<std::string> out;
  std::stringstream in(s);
  for (std::string part; std::getline(in, part, sep);)
    if (!part.empty())
      out.push_back(part);
  return out;
}

double percentile(std::vector<double> v, double p) {
  if (v.empty())
    return 0;
  std::sort(v.begin(), v.end());
  const double idx = p * (v.size() - 1);
  const auto lo = static_cast<std::size_t>(std::floor(idx));
  const auto hi = static_cast<std::size_t>(std::ceil(idx));
  return v[lo] + (v[hi] - v[lo]) * (idx - lo);
}

} // namespace sim

int main(int argc, char **argv) {
  using namespace sim;
  int seedFrom = 1, seedTo = 20;
  std::vector<std::string> variants = {"full"};
  std::string opponents = "prospector:15", csvPath, paramsText;
  Config cfg;
  for (int i = 1; i + 1 < argc; i += 2) {
    const std::string flag = argv[i], value = argv[i + 1];
    if (flag == "--seeds") {
      const auto parts = split(value, ':');
      seedFrom = std::stoi(parts.at(0));
      seedTo = parts.size() > 1 ? std::stoi(parts[1]) : seedFrom;
    } else if (flag == "--variants") {
      variants = split(value, ',');
    } else if (flag == "--opponents") {
      opponents = value;
    } else if (flag == "--params") {
      paramsText = value;
    } else if (flag == "--csv") {
      csvPath = value;
    } else if (flag == "--turns") {
      cfg.maxTurns = std::stoi(value);
    } else {
      std::cerr << "unknown flag " << flag << '\n';
      return 2;
    }
  }
  std::vector<std::string> bots;
  for (const auto &spec : split(opponents, ',')) {
    const auto parts = split(spec, ':');
    const int count = parts.size() > 1 ? std::stoi(parts[1]) : 1;
    for (int k = 0; k < count; ++k)
      bots.push_back(parts[0]);
  }
  ah::Json overrides = ah::Json::object();
  if (!paramsText.empty()) {
    if (paramsText.front() == '{') {
      overrides = ah::Json::parse(paramsText);
    } else {
      std::ifstream file(paramsText);
      overrides = ah::Json::parse(file);
    }
  }

  std::ofstream csv;
  if (!csvPath.empty()) {
    csv.open(csvPath);
    csv << "variant,seed,gold,place,teams,best_opponent,mean_opponent,iron,carried_gold,"
           "carried_iron,bases,workers,moves,mines,mine_fail,deposits,idle,"
           "cooldown,failed,spawns,builds,trades,errors,ms_avg,ms_max\n";
  }
  std::cout << std::fixed << std::setprecision(1);
  std::cout << "opponents: " << opponents << "  seeds " << seedFrom << ".."
            << seedTo << "  turns " << cfg.maxTurns << "\n\n";
  std::cout << std::left << std::setw(10) << "variant" << std::right
            << std::setw(7) << "mean" << std::setw(7) << "sd" << std::setw(7)
            << "min" << std::setw(7) << "p10" << std::setw(7) << "med"
            << std::setw(7) << "max" << std::setw(7) << "win%" << std::setw(7)
            << "place" << std::setw(7) << "opp" << std::setw(7) << "oppAvg" << std::setw(7) << "wrk"
            << std::setw(7) << "base" << std::setw(7) << "left" << std::setw(7)
            << "idle" << std::setw(7) << "fail" << std::setw(7) << "ms"
            << std::setw(7) << "msMax" << "\n";
  for (const std::string &variant : variants) {
    ah::Params params = ah::preset(variant);
    ah::apply_overrides(params, overrides);
    std::vector<double> gold, place, opp, oppAvg, workers, basesBuilt, left, idle, fail,
        ms;
    double msMax = 0;
    int wins = 0;
    std::vector<double> curve;
    for (int seed = seedFrom; seed <= seedTo; ++seed) {
      World world(cfg, static_cast<std::uint64_t>(seed), variant, params, bots);
      const Result r = world.run();
      gold.push_back(r.gold), place.push_back(r.place), opp.push_back(r.bestOpponent), oppAvg.push_back(r.meanOpponent);
      workers.push_back(r.workers), basesBuilt.push_back(r.bases);
      left.push_back(r.carriedGold), idle.push_back(static_cast<double>(r.c.idle));
      fail.push_back(static_cast<double>(r.c.failed + r.c.mineFail));
      ms.push_back(r.msAvg), msMax = std::max(msMax, r.msMax);
      wins += r.place == 1;
      if (curve.size() < r.curve.size())
        curve.resize(r.curve.size(), 0);
      for (std::size_t k = 0; k < r.curve.size(); ++k)
        curve[k] += r.curve[k];
      if (csv)
        csv << variant << ',' << seed << ',' << r.gold << ',' << r.place << ','
            << r.teams << ',' << r.bestOpponent << ',' << r.meanOpponent << ',' << r.iron << ','
            << r.carriedGold << ',' << r.carriedIron << ',' << r.bases << ','
            << r.workers << ',' << r.c.move << ',' << r.c.mine << ','
            << r.c.mineFail << ',' << r.c.deposit << ',' << r.c.idle << ','
            << r.c.cooldown << ',' << r.c.failed << ',' << r.c.spawn << ','
            << r.c.build << ',' << r.c.trade << ',' << r.errors << ','
            << r.msAvg << ',' << r.msMax << '\n';
    }
    const double n = static_cast<double>(gold.size());
    auto mean = [&](const std::vector<double> &v) {
      return std::accumulate(v.begin(), v.end(), 0.0) / std::max(1.0, n);
    };
    const double m = mean(gold);
    double var = 0;
    for (double g : gold)
      var += (g - m) * (g - m);
    const double sd = n > 1 ? std::sqrt(var / (n - 1)) : 0;
    std::cout << std::left << std::setw(10) << variant << std::right
              << std::setw(7) << m << std::setw(7) << sd << std::setw(7)
              << percentile(gold, 0) << std::setw(7) << percentile(gold, 0.1)
              << std::setw(7) << percentile(gold, 0.5) << std::setw(7)
              << percentile(gold, 1) << std::setw(7) << 100.0 * wins / n
              << std::setw(7) << mean(place) << std::setw(7) << mean(opp) << std::setw(7) << mean(oppAvg)
              << std::setw(7) << mean(workers) << std::setw(7)
              << mean(basesBuilt) << std::setw(7) << mean(left) << std::setw(7)
              << mean(idle) << std::setw(7) << mean(fail) << std::setw(7)
              << mean(ms) << std::setw(7) << msMax << "\n";
    std::cout << "          gold by turn (0,40,..):";
    for (double c : curve)
      std::cout << ' ' << static_cast<int>(std::lround(c / n));
    std::cout << "\n";
  }
  return 0;
}
