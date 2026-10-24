#pragma once
// Adaptive Hybrid strategy for LPC 2026.
//
// One Brain per team keeps map memory between turns. Each turn it:
//   1. merges the visible view into memory (terrain, deposits, enemy bases);
//   2. builds weighted distance fields (Dijkstra, 8 directions, obstacle cost
//      learned from readyOnTurn, enemy bases impassable);
//   3. prices investments (recruit a worker, build a base) in gold per kg of
//      iron against an adaptive estimate of gold per worker-turn;
//   4. greedily assigns each ready worker the task with the highest net value
//      (gold gained minus turns used x worker rate), reserving shared deposit
//      stock and exploration areas so workers do not duplicate each other.
// Every plan is deadline-checked: a task is only feasible if its gold can be
// deposited before the final turn. See STRATEGY.md for the model.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <queue>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

// curl on Windows pulls in <windows.h>, whose min/max macros break std::min.
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace ah {

using Json = nlohmann::json;
using Actions = std::map<std::string, Json>;

// Tunable strategy parameters. Gold-equivalent units unless noted.
struct Params {
  // Feature switches (benchmark variants are presets of these).
  bool spawn = true;          // recruit workers by marginal value
  int maxSpawns = -1;         // cap on recruited workers; -1 lets the model decide
  bool bases = true;          // build bases by marginal value
  int maxBases = 6;
  bool explorePrior = true;   // center-weighted, staleness-aware exploration prior
  bool goldToIron = false;    // trade carried gold for iron when iron is scarce
  // Economics.
  double workerRate = 0.35;   // prior gold per worker-turn, blended with observation
  double rateFloor = 0.06;
  double rateCap = 0.6;
  double rateWindow = 80;     // turns of history behind the observed rate
  double saturation = 10;     // marginal worker value decays as exp(-workers/saturation)
  double spawnRamp = 6;       // turns before a recruit is productive
  double spawnMargin = 1.0;   // recruit when value >= margin * cost * (1/3 gold per iron)
  double baseMinValue = 8;    // minimum net gold a base must add
  double baseClaimBonus = 0.3;
  double ironValueCap = 3.0;  // ceiling on gold per kg of iron when mining it
  double goldToIronMargin = 1.5;
  // Exploration.
  double exploreWeight = 0.25;
  double depositLifetime = 80;  // turns for empty seen tiles to regain their prior
  double centerBoost = 1.0;
  double claimedExploreWeight = 0.15;
  // Movement and mining.
  double unknownStepCost = 1.5;
  double staleTau = 60;       // turns until a remembered deposit is ~37% likely intact
  double competition = 1.0;   // kg lost per visible enemy miner per turn of travel
  double stickiness = 0.75;   // bonus for keeping last turn's target
  int endgameSafety = 1;      // spare turns kept before the deadline
  int investCutoff = 40;      // no investments with fewer turns left
  // Infrastructure.
  bool persist = true;        // snapshot memory to disk for restarts
  int logEvery = 20;
};

inline void apply_overrides(Params &p, const Json &j) {
#define AH_FIELD(name)                                                         \
  if (j.contains(#name))                                                       \
    p.name = j.at(#name).get<decltype(p.name)>();
  AH_FIELD(spawn) AH_FIELD(maxSpawns) AH_FIELD(bases) AH_FIELD(maxBases)
  AH_FIELD(explorePrior) AH_FIELD(goldToIron) AH_FIELD(workerRate)
  AH_FIELD(rateFloor) AH_FIELD(rateCap) AH_FIELD(rateWindow)
  AH_FIELD(saturation) AH_FIELD(spawnRamp) AH_FIELD(spawnMargin)
  AH_FIELD(baseMinValue) AH_FIELD(baseClaimBonus) AH_FIELD(ironValueCap)
  AH_FIELD(goldToIronMargin) AH_FIELD(exploreWeight) AH_FIELD(depositLifetime)
  AH_FIELD(centerBoost) AH_FIELD(claimedExploreWeight)
  AH_FIELD(unknownStepCost) AH_FIELD(staleTau) AH_FIELD(competition)
  AH_FIELD(stickiness) AH_FIELD(endgameSafety) AH_FIELD(investCutoff)
  AH_FIELD(persist) AH_FIELD(logEvery)
#undef AH_FIELD
}

// Benchmark variants: each adds one component to the previous one.
inline Params preset(const std::string &variant) {
  Params p;
  if (variant == "baseline") {
    p.spawn = false, p.maxSpawns = 0, p.bases = false, p.explorePrior = false;
  } else if (variant == "early2") {
    p.maxSpawns = 2, p.bases = false, p.explorePrior = false;
  } else if (variant == "center") {
    p.maxSpawns = 2, p.bases = false;
  } else if (variant == "bases") {
    p.maxSpawns = 2;
  } else if (variant != "full") {
    std::cerr << "[ah] unknown variant '" << variant << "'; using full\n";
  }
  return p;
}

// AH_VARIANT picks a preset; AH_PARAMS is inline JSON or a JSON file path.
inline Params load_params() {
  const char *variant = std::getenv("AH_VARIANT");
  Params p = preset(variant && *variant ? variant : "full");
  if (const char *raw = std::getenv("AH_PARAMS"); raw && *raw) {
    const std::string text(raw);
    if (text.front() == '{') {
      apply_overrides(p, Json::parse(text));
    } else {
      std::ifstream file(text);
      apply_overrides(p, Json::parse(file));
    }
  }
  return p;
}

inline constexpr int DX[8] = {0, 1, 1, 1, 0, -1, -1, -1};
inline constexpr int DY[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
inline constexpr const char *DIRECTIONS[8] = {
    "north", "north-east", "east", "south-east",
    "south", "south-west", "west", "north-west"};

// Worker recruitment price: 5, 6, then each is the sum of the previous two.
inline int spawn_cost(int recruited) {
  int a = 5, b = 6;
  if (recruited <= 0)
    return a;
  for (int i = 1; i < recruited; ++i) {
    const int c = a + b;
    a = b;
    b = c;
  }
  return b;
}

class Brain {
public:
  explicit Brain(Params params = {}) : P(std::move(params)) {}

  Actions decide(const Json &state, const std::string &team) {
    const auto started = std::chrono::steady_clock::now();
    Actions out;
    try {
      ingest(state, team);
      plan(out);
    } catch (const std::exception &error) {
      ++errors_;
      std::cerr << "[ah] strategy error on turn " << turn_ << ": "
                << error.what() << "; using fallback\n";
      out = fallback(state, team);
    }
    lastMs_ = std::chrono::duration<double, std::milli>(
                  std::chrono::steady_clock::now() - started)
                  .count();
    if (P.logEvery > 0 && turn_ % P.logEvery == 0)
      std::cerr << "[ah] t=" << turn_ << " workers=" << mine_.size()
                << " bases=" << myBases_.size() << " bank=" << bankGold_ << "g/"
                << bankIron_ << "i rate=" << rate_ << " ironValue=" << ironHi_
                << " obstacle=" << obstacle_ << " ms=" << lastMs_ << '\n';
    return out;
  }

  double lastMs() const { return lastMs_; }
  int errors() const { return errors_; }
  const Params &params() const { return P; }

private:
  enum Terrain : std::uint8_t { UNKNOWN, EMPTY, MOUNTAIN, CRATER, BLOCKED };
  enum Res : std::uint8_t { NONE, GOLD, IRON };
  enum class Task : std::uint8_t { None, Mine, Home, Explore, Build, Spawn, Wait };
  struct Cell {
    std::uint8_t terrain = UNKNOWN, res = NONE;
    int amount = 0, seen = -1;
  };
  struct Unit {
    std::string id, team;
    int pos = 0, gold = 0, iron = 0, cap = 8, ready = 0;
  };
  struct Base {
    std::string id, team;
    int pos = 0;
  };
  struct Memory {
    Task task = Task::None;
    int target = -1, movedTo = -1, movedOn = -100;
  };
  struct Deposit {
    int cell = 0;
    std::uint8_t res = NONE;
    double amount = 0;
    int enemyOn = 0, enemyNear = 0;
  };
  struct Sample {
    int turn;
    double gold;
    int workers;
  };
  struct Choice {
    double net = -1e18;
    Task task = Task::Wait;
    int target = -1, deposit = -1, q = 0;
  };
  static constexpr int INF = 1 << 29;

  Params P;
  std::string match_, team_;
  int W_ = 0, H_ = 0, N_ = 0, turn_ = 0, maxTurns_ = 480, teams_ = 16;
  int lastTurn_ = -1, startWorkers_ = 1, obstacle_ = 3, errors_ = 0;
  int cx0_ = 17, cy0_ = 17, cw_ = 16, ch_ = 16;
  double cMin_ = 12, cMax_ = 18, cPct_ = 0.35;
  int bankGold_ = 0, bankIron_ = 0;
  double rate_ = 0.25, ironHi_ = 1.0 / 3, ironLo_ = 1.0 / 3, ironShort_ = 0;
  double lastMs_ = 0;
  std::vector<Cell> mem_;
  std::vector<std::uint8_t> vis_;
  std::map<std::string, Base> enemyBases_;
  std::vector<Base> myBases_;
  std::vector<Unit> mine_, enemies_;
  std::map<std::string, Memory> wm_;
  std::map<int, int> badSite_;  // cell -> turn the ban expires
  std::vector<std::pair<int, int>> pendingBuilds_;  // cell, turn
  std::vector<Sample> history_;
  std::vector<std::pair<int, int>> diamond3_;

  // Per-turn scratch.
  std::vector<int> cost_, bankDist_, depAt_;
  std::vector<std::uint8_t> myClaim_, enemyClaim_, myBaseAt_, anyBaseAt_;
  std::vector<Deposit> deps_;
  std::vector<double> reserved_, prior_, claimW_, ei_;
  std::vector<std::vector<int>> dist_, parent_;  // per index in mine_
  std::vector<int> unitsAt_;
  std::vector<int> buildingNow_;

  int X(int i) const { return i % W_; }
  int Y(int i) const { return i / W_; }
  bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < W_ && y < H_; }
  int cheb(int a, int b) const {
    return std::max(std::abs(X(a) - X(b)), std::abs(Y(a) - Y(b)));
  }
  int manh(int a, int b) const {
    return std::abs(X(a) - X(b)) + std::abs(Y(a) - Y(b));
  }
  bool inCenter(int i) const {
    return X(i) >= cx0_ && X(i) < cx0_ + cw_ && Y(i) >= cy0_ && Y(i) < cy0_ + ch_;
  }
  static int turns(int tenths) { return tenths >= INF ? INF : (tenths + 9) / 10; }
  int remaining() const { return maxTurns_ - turn_; }

  // ---------------------------------------------------------------- ingest
  void reset(const std::string &match, const std::string &team, int w, int h) {
    match_ = match, team_ = team, W_ = w, H_ = h, N_ = w * h;
    mem_.assign(N_, Cell{});
    enemyBases_.clear(), wm_.clear(), badSite_.clear(), pendingBuilds_.clear();
    history_.clear();
    obstacle_ = 3, startWorkers_ = 1, lastTurn_ = -1;
    diamond3_.clear();
    for (int dy = -3; dy <= 3; ++dy)
      for (int dx = -3; dx <= 3; ++dx)
        if (std::abs(dx) + std::abs(dy) <= 3)
          diamond3_.push_back({dx, dy});
  }

  void ingest(const Json &state, const std::string &team) {
    const Json &match = state.at("match");
    const Json &view = state.at("view");
    const Json &map = view.at("map");
    const std::string id = match.value("matchId", std::string());
    const int t = view.value("turn", match.value("turn", 0));
    const int w = map.at("width").get<int>(), h = map.at("height").get<int>();
    const bool fresh = id != match_ || team != team_ || w != W_ || h != H_ ||
                       t < lastTurn_;
    if (fresh) {
      reset(id, team, w, h);
      if (t > 0)
        load();
    }
    turn_ = t;
    maxTurns_ = match.value("maxTurns", 480);
    if (match.contains("teams") && match["teams"].is_array() &&
        !match["teams"].empty())
      teams_ = static_cast<int>(match["teams"].size());
    else if (view.contains("scores") && view["scores"].is_object() &&
             !view["scores"].empty())
      teams_ = static_cast<int>(view["scores"].size());

    cw_ = std::min(16, W_), ch_ = std::min(16, H_);
    if (view.contains("highGoldArea") && view["highGoldArea"].is_object()) {
      const Json &g = view["highGoldArea"];
      cw_ = std::clamp(g.value("width", cw_), 0, W_);
      ch_ = std::clamp(g.value("height", ch_), 0, H_);
      cMin_ = g.value("min", 12.0), cMax_ = g.value("max", 18.0);
      cPct_ = g.value("spawnPercent", 35.0) / 100.0;
    }
    cx0_ = (W_ - cw_) / 2, cy0_ = (H_ - ch_) / 2;

    vis_.assign(N_, 0);
    if (map.contains("cells") && map["cells"].is_array()) {
      const Json &cells = map["cells"];
      const int n = std::min<int>(static_cast<int>(cells.size()), N_);
      for (int i = 0; i < n; ++i)
        readCell(i, cells[i]);
    }

    mine_.clear(), enemies_.clear(), myBases_.clear();
    if (view.contains("units") && view["units"].is_array())
      for (const Json &u : view["units"]) {
        Unit unit;
        if (!readPos(u, unit.pos))
          continue;
        unit.id = u.value("unitId", std::string());
        unit.team = u.value("teamId", std::string());
        if (unit.team == team_) {
          if (u.contains("inventory") && u["inventory"].is_object()) {
            unit.gold = u["inventory"].value("gold", 0);
            unit.iron = u["inventory"].value("iron", 0);
          }
          unit.cap = u.value("inventoryCapacity", 8);
          unit.ready = u.value("readyOnTurn", 0);
          mine_.push_back(unit);
        } else {
          enemies_.push_back(unit);
        }
      }
    std::sort(mine_.begin(), mine_.end(),
              [](const Unit &a, const Unit &b) { return a.id < b.id; });
    if (view.contains("bases") && view["bases"].is_array())
      for (const Json &b : view["bases"]) {
        Base base;
        if (!readPos(b, base.pos))
          continue;
        base.id = b.value("baseId", std::string());
        base.team = b.value("teamId", std::string());
        if (base.team == team_)
          myBases_.push_back(base);
        else
          enemyBases_[base.id.empty() ? std::to_string(base.pos) : base.id] = base;
      }

    bankGold_ = bankIron_ = 0;
    if (view.contains("teamResources") && view["teamResources"].is_object() &&
        view["teamResources"].contains(team_)) {
      const Json &r = view["teamResources"][team_];
      bankGold_ = r.value("gold", 0), bankIron_ = r.value("iron", 0);
    }
    if (t == 0 && !mine_.empty())
      startWorkers_ = static_cast<int>(mine_.size());

    // Learn the obstacle cost from cooldowns after our own obstacle moves.
    for (const Unit &u : mine_) {
      auto it = wm_.find(u.id);
      if (it == wm_.end())
        continue;
      const Memory &m = it->second;
      const auto terrain = mem_[u.pos].terrain;
      if (m.movedOn == t - 1 && m.movedTo == u.pos &&
          (terrain == MOUNTAIN || terrain == CRATER)) {
        const int learned = u.ready - m.movedOn;
        if (learned >= 1 && learned <= 20)
          obstacle_ = learned;
      }
    }
    // Ban sites where construction failed (hidden enemy base, blocked tile).
    for (const auto &[cell, when] : pendingBuilds_)
      if (when == t - 1 &&
          std::none_of(myBases_.begin(), myBases_.end(),
                       [&](const Base &b) { return b.pos == cell; }))
        badSite_[cell] = t + 40;
    std::erase_if(pendingBuilds_, [&](const auto &p) { return p.second < t - 1; });

    double carried = 0;
    for (const Unit &u : mine_)
      carried += u.gold;
    history_.push_back({t, bankGold_ + carried, static_cast<int>(mine_.size())});
    updateRate();
    lastTurn_ = t;
  }

  bool readPos(const Json &o, int &pos) const {
    if (!o.contains("position") || !o["position"].is_object())
      return false;
    const int x = o["position"].value("x", -1), y = o["position"].value("y", -1);
    if (!inside(x, y))
      return false;
    pos = y * W_ + x;
    return true;
  }

  void readCell(int i, const Json &c) {
    if (!c.is_object() || !c.contains("type") || !c["type"].is_string())
      return;
    const std::string &type = c["type"].get_ref<const std::string &>();
    if (type == "hidden")
      return;
    vis_[i] = 1;
    Cell &m = mem_[i];
    m.seen = turn_;
    if (type == "gold" || type == "iron") {
      const int amount = c.value("amount", 0);
      m.terrain = EMPTY;
      m.res = amount > 0 ? (type == "gold" ? GOLD : IRON) : NONE;
      m.amount = std::max(amount, 0);
      return;
    }
    m.res = NONE, m.amount = 0;
    if (type == "empty" || type == "plain")
      m.terrain = EMPTY;
    else if (type == "mountain")
      m.terrain = MOUNTAIN;
    else if (type == "crater")
      m.terrain = CRATER;
    else
      m.terrain = BLOCKED;
  }

  void updateRate() {
    double observed = -1;
    const int from = turn_ - static_cast<int>(P.rateWindow);
    std::size_t j = 0;
    while (j < history_.size() && history_[j].turn < from)
      ++j;
    if (history_.size() - j >= 10) {
      double workerTurns = 0;
      for (std::size_t k = j; k + 1 < history_.size(); ++k)
        workerTurns += history_[k].workers *
                       (history_[k + 1].turn - history_[k].turn);
      if (workerTurns > 0)
        observed = (history_.back().gold - history_[j].gold) / workerTurns;
    }
    const double weight =
        observed < 0 ? 0 : std::clamp(turn_ / (2 * P.rateWindow), 0.0, 1.0);
    rate_ = std::clamp((1 - weight) * P.workerRate +
                           weight * std::max(observed, 0.0),
                       P.rateFloor, P.rateCap);
  }

  // ------------------------------------------------------------ distances
  void dijkstra(const std::vector<std::pair<int, int>> &sources, bool reverse,
                std::vector<int> &dist, std::vector<int> *parent) const {
    dist.assign(N_, INF);
    if (parent)
      parent->assign(N_, -1);
    using Entry = std::pair<int, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> queue;
    for (const auto &[cell, d0] : sources)
      if (d0 < dist[cell]) {
        dist[cell] = d0;
        queue.push({d0, cell});
      }
    while (!queue.empty()) {
      const auto [d, u] = queue.top();
      queue.pop();
      if (d != dist[u])
        continue;
      // Reverse fields price the step v -> u, so u must be enterable.
      if (reverse && cost_[u] >= INF)
        continue;
      const int ux = X(u), uy = Y(u);
      for (int k = 0; k < 8; ++k) {
        const int nx = ux + DX[k], ny = uy + DY[k];
        if (!inside(nx, ny))
          continue;
        const int v = ny * W_ + nx;
        const int step = reverse ? cost_[u] : cost_[v];
        if (step >= INF)
          continue;
        const int nd = d + step;
        if (nd < dist[v]) {
          dist[v] = nd;
          if (parent)
            (*parent)[v] = u;
          queue.push({nd, v});
        }
      }
    }
  }

  int firstStep(int worker, int target) const {
    const auto &par = parent_[worker];
    const int source = mine_[worker].pos;
    int cur = target, prev = -1;
    for (int guard = 0; cur != source && cur >= 0 && guard <= N_; ++guard) {
      prev = cur;
      cur = par[cur];
    }
    return cur == source ? prev : -1;
  }

  Json moveTo(int from, int to) const {
    const int dx = X(to) - X(from), dy = Y(to) - Y(from);
    for (int k = 0; k < 8; ++k)
      if (DX[k] == dx && DY[k] == dy)
        return {{"type", "move"}, {"direction", DIRECTIONS[k]}};
    return {{"type", "pass"}};
  }

  void buildFields() {
    cost_.assign(N_, INF);
    const int unknownCost =
        std::max(10, static_cast<int>(std::lround(P.unknownStepCost * 10)));
    for (int i = 0; i < N_; ++i)
      switch (mem_[i].terrain) {
      case EMPTY: cost_[i] = 10; break;
      case MOUNTAIN:
      case CRATER: cost_[i] = obstacle_ * 10; break;
      case UNKNOWN: cost_[i] = unknownCost; break;
      default: break;
      }
    myClaim_.assign(N_, 0), enemyClaim_.assign(N_, 0);
    myBaseAt_.assign(N_, 0), anyBaseAt_.assign(N_, 0);
    auto claim = [&](int pos, std::vector<std::uint8_t> &grid) {
      for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx)
          if (std::abs(dx) + std::abs(dy) <= 2 && inside(X(pos) + dx, Y(pos) + dy))
            grid[(Y(pos) + dy) * W_ + X(pos) + dx] = 1;
    };
    for (const auto &[id, b] : enemyBases_) {
      cost_[b.pos] = INF;
      anyBaseAt_[b.pos] = 1;
      claim(b.pos, enemyClaim_);
    }
    std::vector<std::pair<int, int>> sources;
    for (const Base &b : myBases_) {
      myBaseAt_[b.pos] = anyBaseAt_[b.pos] = 1;
      cost_[b.pos] = 10;
      claim(b.pos, myClaim_);
      sources.push_back({b.pos, 0});
    }
    dijkstra(sources, true, bankDist_, nullptr);

    unitsAt_.assign(N_, 0);
    for (const Unit &u : mine_)
      ++unitsAt_[u.pos];
    for (const Unit &u : enemies_)
      ++unitsAt_[u.pos];

    deps_.clear();
    depAt_.assign(N_, -1);
    for (int i = 0; i < N_; ++i) {
      const Cell &c = mem_[i];
      if (c.res == NONE || c.amount <= 0 || cost_[i] >= INF)
        continue;
      if (enemyClaim_[i] && !myClaim_[i])
        continue;  // only claiming teams may mine
      double amount = c.amount;
      if (!vis_[i])
        amount *= std::exp(-(turn_ - c.seen) / std::max(1.0, P.staleTau));
      if (amount < 0.5)
        continue;
      depAt_[i] = static_cast<int>(deps_.size());
      deps_.push_back({i, c.res, amount, 0, 0});
    }
    for (const Unit &e : enemies_)
      for (Deposit &d : deps_) {
        const int c = cheb(e.pos, d.cell);
        if (c == 0)
          ++d.enemyOn;
        else if (c == 1)
          ++d.enemyNear;
      }
    reserved_.assign(deps_.size(), 0);
  }

  // Expected gold (kg) per unseen tile, discounted by the cost of banking it.
  double goldDensity(int i) const {
    const double gold = 3.0 * teams_;
    const double centerArea = std::max(1, cw_ * ch_);
    const double outerArea = std::max(1, N_ - cw_ * ch_);
    const double centerKg = (cMin_ + cMax_) / 2, outerKg = 7.5;
    if (!P.explorePrior)
      return gold / N_ * (cPct_ * centerKg + (1 - cPct_) * outerKg);
    return inCenter(i) ? cPct_ * gold / centerArea * centerKg * P.centerBoost
                       : (1 - cPct_) * gold / outerArea * outerKg;
  }

  double bankingFactor(int i) const {
    const int back = turns(bankDist_[i]);
    if (back >= INF)
      return 0.05;
    return std::clamp(1 - rate_ * (2.0 * back + 8) / 8, 0.05, 1.0);
  }

  void buildPrior() {
    prior_.assign(N_, 0);
    const double ironKg = 2.0 * teams_ / N_ * 3.0;
    for (int i = 0; i < N_; ++i) {
      const Cell &c = mem_[i];
      if (vis_[i] || (c.terrain != UNKNOWN && c.terrain != EMPTY) ||
          c.res != NONE)
        continue;
      double fresh = 1;
      if (c.seen >= 0)
        fresh = P.explorePrior
                    ? std::min(1.0, (turn_ - c.seen) / std::max(1.0, P.depositLifetime))
                    : 0;
      if (fresh <= 0)
        continue;
      prior_[i] = fresh * (goldDensity(i) * bankingFactor(i) + ironKg * ironHi_ * 0.5);
    }
    claimW_.assign(N_, 1);
    ei_.assign(N_, 0);
    for (int i = 0; i < N_; ++i)
      ei_[i] = exploreInfo(i);
  }

  double exploreInfo(int c) const {
    double sum = 0;
    const int cx = X(c), cy = Y(c);
    for (const auto &[dx, dy] : diamond3_)
      if (inside(cx + dx, cy + dy)) {
        const int u = (cy + dy) * W_ + cx + dx;
        sum += prior_[u] * claimW_[u];
      }
    return sum;
  }

  void claimExplore(int c) {
    const int cx = X(c), cy = Y(c);
    for (const auto &[dx, dy] : diamond3_)
      if (inside(cx + dx, cy + dy))
        claimW_[(cy + dy) * W_ + cx + dx] = P.claimedExploreWeight;
    for (int dy = -6; dy <= 6; ++dy)
      for (int dx = -6; dx <= 6; ++dx)
        if (std::abs(dx) + std::abs(dy) <= 6 && inside(cx + dx, cy + dy))
          ei_[(cy + dy) * W_ + cx + dx] = exploreInfo((cy + dy) * W_ + cx + dx);
  }

  // ----------------------------------------------------------- investments
  double spawnValue(int workers) const {
    return rate_ * std::exp(-workers / std::max(1.0, P.saturation)) *
           std::max(0.0, remaining() - P.spawnRamp);
  }

  bool validSite(int s, int builder) const {
    const Cell &c = mem_[s];
    if (c.terrain != EMPTY || c.res != NONE || anyBaseAt_[s] || enemyClaim_[s])
      return false;
    if (auto it = badSite_.find(s); it != badSite_.end() && it->second > turn_)
      return false;
    const int others = unitsAt_[s] - (builder >= 0 && mine_[builder].pos == s ? 1 : 0);
    return others == 0;
  }

  // Gold-equivalent value of a base at s over the rest of the match: banking
  // trips it shortens for known gold, plus gold expected to appear nearby.
  double baseValue(int s) const {
    const int R = remaining();
    double value = 0;
    for (const Deposit &d : deps_) {
      if (d.res != GOLD)
        continue;
      const int c = cheb(s, d.cell);
      if (c > 8)
        continue;
      const double saved = turns(bankDist_[d.cell]) - c * 1.15;
      if (saved <= 0.5)
        continue;
      const double claim =
          manh(s, d.cell) <= 2 && !myClaim_[d.cell] ? 1 + P.baseClaimBonus : 1;
      value += d.amount / 8 * 2 * saved * rate_ * claim;
    }
    for (int dy = -4; dy <= 4; ++dy)
      for (int dx = -4; dx <= 4; ++dx) {
        if (!inside(X(s) + dx, Y(s) + dy))
          continue;
        const int u = (Y(s) + dy) * W_ + X(s) + dx;
        const Cell &c = mem_[u];
        if ((c.terrain != UNKNOWN && c.terrain != EMPTY) || c.res != NONE)
          continue;
        const double saved = turns(bankDist_[u]) - std::max(std::abs(dx), std::abs(dy)) * 1.15;
        if (saved <= 0.5)
          continue;
        double kg = 0;
        if (P.explorePrior)
          kg += goldDensity(u) * 0.5 * R / std::max(1.0, P.depositLifetime);
        if (c.seen < 0)
          kg += goldDensity(u) * 0.5;
        value += kg / 8 * 2 * saved * rate_;
      }
    return value;
  }

  void invest(Actions &out, std::vector<char> &assigned) {
    const int R = remaining();
    int budget = bankIron_;
    int carriedIron = 0;
    for (const Unit &u : mine_)
      carriedIron += u.iron;
    int workers = static_cast<int>(mine_.size());
    int recruited = std::max(0, workers - startWorkers_);

    // A builder already en route keeps its site and its 6 kg reservation.
    int builder = -1, site = -1;
    for (int k = 0; k < static_cast<int>(mine_.size()); ++k) {
      const Memory &m = wm_[mine_[k].id];
      if (m.task != Task::Build || builder >= 0)
        continue;
      if (P.bases && R > P.investCutoff && budget >= 6 && m.target >= 0 &&
          validSite(m.target, k) && dist_[k][m.target] < INF) {
        builder = k, site = m.target;
        budget -= 6;
      }
    }

    struct Option {
      bool base;
      double value;
      int cost, site, worker;
    };
    auto options = [&]() {
      std::vector<Option> list;
      const bool spawnAllowed =
          P.spawn && (P.maxSpawns < 0 || recruited < P.maxSpawns);
      if (spawnAllowed && R > P.investCutoff) {
        const int cost = spawn_cost(recruited);
        const double value = spawnValue(workers);
        if (value >= P.spawnMargin * cost / 3.0)
          list.push_back({false, value, cost, -1, -1});
      }
      if (P.bases && builder < 0 && R > P.investCutoff &&
          static_cast<int>(myBases_.size()) < P.maxBases) {
        Option best{true, P.baseMinValue, 6, -1, -1};
        for (int s = 0; s < N_; ++s) {
          // A site is open if empty, or if its only occupant is a ready
          // worker of ours that can build there itself.
          int occupant = -1;
          for (int k = 0; k < static_cast<int>(mine_.size()) && unitsAt_[s] == 1; ++k)
            if (mine_[k].pos == s && !assigned[k] && mine_[k].ready <= turn_)
              occupant = k;
          if (!validSite(s, occupant))
            continue;
          bool goldNearby = false;
          for (const Deposit &d : deps_)
            if (d.res == GOLD && cheb(s, d.cell) <= 4) {
              goldNearby = true;
              break;
            }
          if (!goldNearby && !(P.explorePrior && inCenter(s)))
            continue;
          int nearestBase = INF;
          for (const Base &b : myBases_)
            nearestBase = std::min(nearestBase, manh(s, b.pos));
          if (nearestBase < 5)
            continue;
          int who = occupant, travel = occupant >= 0 ? 0 : INF;
          for (int k = 0; occupant < 0 && k < static_cast<int>(mine_.size()); ++k) {
            if (assigned[k] || mine_[k].ready > turn_)
              continue;
            const int t = turns(dist_[k][s]);
            if (t < travel)
              travel = t, who = k;
          }
          if (who < 0 || R - travel <= P.investCutoff)
            continue;
          const double net = baseValue(s) - rate_ * travel;
          if (net > best.value)
            best = {true, net, 6, s, who};
        }
        if (best.site >= 0)
          list.push_back(best);
      }
      std::sort(list.begin(), list.end(), [](const Option &a, const Option &b) {
        return a.value / a.cost > b.value / b.cost;
      });
      return list;
    };

    for (int round = 0; round < 2; ++round) {
      const auto list = options();
      if (list.empty() || list.front().cost > budget)
        break;
      const Option &o = list.front();
      if (o.base) {
        builder = o.worker, site = o.site;
        budget -= 6;
        continue;
      }
      // Recruit with the worker that loses least: one on a deposit with spare
      // stock lets the recruit mine at once.
      int spawner = -1;
      double bestScore = -1e9;
      for (int k = 0; k < static_cast<int>(mine_.size()); ++k) {
        const Unit &u = mine_[k];
        if (assigned[k] || u.ready > turn_ || k == builder)
          continue;
        double score = 0;
        if (const int d = depAt_[u.pos]; d >= 0 && deps_[d].res == GOLD &&
                                         deps_[d].amount >= 4)
          score += 3;
        if (u.gold + u.iron == 0)
          score += 1;
        if (u.gold + u.iron >= u.cap)
          score -= 5;
        if (score > bestScore)
          bestScore = score, spawner = k;
      }
      if (spawner < 0)
        break;
      out[mine_[spawner].id] = {{"type", "spawn"}};
      wm_[mine_[spawner].id].task = Task::Spawn;
      assigned[spawner] = 1;
      budget -= o.cost;
      ++recruited, ++workers;
      break;  // one recruit per turn; the price rises after each
    }

    if (builder >= 0) {
      const Unit &u = mine_[builder];
      Memory &m = wm_[u.id];
      m.task = Task::Build, m.target = site;
      assigned[builder] = 1;
      if (u.pos == site) {
        out[u.id] = {{"type", "build-base"}};
        pendingBuilds_.push_back({site, turn_});
        buildingNow_.push_back(site);
      } else {
        const int step = firstStep(builder, site);
        out[u.id] = step >= 0 ? moveTo(u.pos, step) : Json{{"type", "pass"}};
        if (step >= 0)
          m.movedTo = step, m.movedOn = turn_;
      }
    }

    // Price iron for this turn's mining decisions from the next investment.
    const auto next = options();
    ironHi_ = 1.0 / 3, ironShort_ = 0;
    if (!next.empty()) {
      ironHi_ = std::clamp(next.front().value / next.front().cost, 1.0 / 3,
                           P.ironValueCap);
      ironShort_ = std::max(0, next.front().cost - budget - carriedIron);
    }
    ironLo_ = R > 10 ? 1.0 / 3 : 0;
  }

  // ------------------------------------------------------------ assignment
  double ironValue(int q) const {
    const double hi = std::min<double>(q, std::max(0.0, ironShort_));
    return hi * ironHi_ + (q - hi) * ironLo_;
  }

  Choice best(int k) const {
    const Unit &u = mine_[k];
    const auto &dist = dist_[k];
    const Memory &last = wm_.at(u.id);
    const int R = remaining();
    const int carried = u.gold + u.iron, space = u.cap - carried;
    const int home = turns(bankDist_[u.pos]);
    Choice choice;
    if (carried > 0) {
      choice.task = Task::Home;
      choice.net = last.task == Task::Home ? P.stickiness : 0;
      if (myBaseAt_[u.pos] || space <= 0)
        return choice;  // deposit now, or nothing else fits
    }
    if (space > 0)
      for (int d = 0; d < static_cast<int>(deps_.size()); ++d) {
        const Deposit &dep = deps_[d];
        const int go = turns(dist[dep.cell]);
        const int back = turns(bankDist_[dep.cell]);
        if (go >= INF || back >= INF)
          continue;
        const double pressure =
            P.competition * (dep.enemyOn + 0.5 * dep.enemyNear) * go;
        const double available = dep.amount - reserved_[d] - pressure;
        int q = std::min(space, static_cast<int>(std::floor(available)));
        q = std::min(q, R - P.endgameSafety - go - back - 1);
        if (q < 1)
          continue;
        const double value = dep.res == GOLD ? q : ironValue(q);
        const double time = go + q + back + 1 - (carried > 0 ? home + 1 : 0);
        double net = value - rate_ * time;
        if (last.task == Task::Mine && last.target == dep.cell)
          net += P.stickiness;
        if (net > choice.net)
          choice = {net, Task::Mine, dep.cell, d, q};
      }
    for (int c = 0; c < N_; ++c) {
      if (ei_[c] <= 0 || c == u.pos || dist[c] >= INF)
        continue;
      const int go = turns(dist[c]);
      const int back = turns(bankDist_[c]);
      if (carried > 0 && (back >= INF || go + back + 1 > R - P.endgameSafety))
        continue;
      const double timeLeft =
          std::clamp((R - go - 2.0 * std::min(back, 60) - 8) / 30.0, 0.0, 1.0);
      if (timeLeft <= 0)
        continue;
      double net = P.exploreWeight * ei_[c] * timeLeft - rate_ * go;
      if (last.task == Task::Explore && last.target == c)
        net += P.stickiness;
      if (net > choice.net)
        choice = {net, Task::Explore, c, -1, 0};
    }
    return choice;
  }

  void commit(int k, const Choice &c, Actions &out) {
    const Unit &u = mine_[k];
    Memory &m = wm_[u.id];
    m.task = c.task, m.target = c.target;
    const int R = remaining();
    Json action = {{"type", "pass"}};
    int step = -1;
    switch (c.task) {
    case Task::Mine:
      reserved_[c.deposit] += c.q;
      if (deps_[c.deposit].res == IRON)
        ironShort_ = std::max(0.0, ironShort_ - c.q);
      if (u.pos == c.target)
        action = {{"type", "mine"}};
      else
        step = firstStep(k, c.target);
      break;
    case Task::Explore:
      claimExplore(c.target);
      step = firstStep(k, c.target);
      break;
    case Task::Home:
      if (myBaseAt_[u.pos]) {
        if (u.iron >= 3 && ironHi_ <= 1.0 / 3 + 1e-9 && R <= P.investCutoff &&
            R >= 2)
          action = {{"type", "trade"}, {"from", "iron"}};
        else if (P.goldToIron && u.gold >= 3 && ironShort_ > 0 &&
                 ironHi_ >= 3 * P.goldToIronMargin && R > P.investCutoff)
          action = {{"type", "trade"}, {"from", "gold"}};
        else
          action = {{"type", "deposit"}};
      } else {
        int bestCell = -1, bestDist = INF;
        for (int dir = 0; dir < 8; ++dir) {
          const int nx = X(u.pos) + DX[dir], ny = Y(u.pos) + DY[dir];
          if (!inside(nx, ny))
            continue;
          const int v = ny * W_ + nx;
          if (cost_[v] >= INF || bankDist_[v] >= INF)
            continue;
          const int d = cost_[v] + bankDist_[v];
          if (d < bestDist)
            bestDist = d, bestCell = v;
        }
        step = bestCell;
      }
      break;
    default:
      break;
    }
    if (step >= 0) {
      if (std::find(buildingNow_.begin(), buildingNow_.end(), step) ==
          buildingNow_.end()) {
        action = moveTo(u.pos, step);
        m.movedTo = step, m.movedOn = turn_;
      }
    }
    out[u.id] = action;
  }

  void plan(Actions &out) {
    buildFields();
    buildingNow_.clear();
    const int n = static_cast<int>(mine_.size());
    dist_.assign(n, {});
    parent_.assign(n, {});
    std::vector<char> assigned(n, 0);
    for (int k = 0; k < n; ++k) {
      const Unit &u = mine_[k];
      wm_.try_emplace(u.id);
      if (u.ready > turn_) {
        out[u.id] = {{"type", "pass"}};
        assigned[k] = 1;
        dist_[k].assign(N_, INF);
        continue;
      }
      dijkstra({{u.pos, 0}}, false, dist_[k], &parent_[k]);
    }
    invest(out, assigned);
    buildPrior();

    // Workers on cooldown keep their deposit and exploration reservations.
    for (int k = 0; k < n; ++k) {
      if (mine_[k].ready <= turn_)
        continue;
      const Memory &m = wm_[mine_[k].id];
      if (m.task == Task::Mine && m.target >= 0 && depAt_[m.target] >= 0)
        reserved_[depAt_[m.target]] +=
            mine_[k].cap - mine_[k].gold - mine_[k].iron;
      else if (m.task == Task::Explore && m.target >= 0)
        claimExplore(m.target);
    }

    // Greedy assignment: repeatedly commit the worker whose best task is worth
    // the most, then update shared reservations before the next pick.
    while (true) {
      int pick = -1;
      Choice pickChoice;
      for (int k = 0; k < n; ++k) {
        if (assigned[k])
          continue;
        const Choice c = best(k);
        if (pick < 0 || c.net > pickChoice.net)
          pick = k, pickChoice = c;
      }
      if (pick < 0)
        break;
      assigned[pick] = 1;
      commit(pick, pickChoice, out);
    }
    if (P.persist && turn_ % 10 == 0)
      save();
  }

  // ----------------------------------------------------------- resilience
  Actions fallback(const Json &state, const std::string &team) const {
    Actions out;
    try {
      const Json &view = state.at("view");
      const Json &map = view.at("map");
      const int w = map.at("width");
      std::vector<std::pair<int, int>> bases;
      for (const Json &b : view.at("bases"))
        if (b.value("teamId", "") == team)
          bases.push_back({b["position"]["x"], b["position"]["y"]});
      for (const Json &u : view.at("units")) {
        if (u.value("teamId", "") != team)
          continue;
        const int x = u["position"]["x"], y = u["position"]["y"];
        const Json inv = u.value("inventory", Json::object());
        const int carried = inv.value("gold", 0) + inv.value("iron", 0);
        const Json &cell = map.at("cells").at(static_cast<std::size_t>(y * w + x));
        const std::string type = cell.value("type", "");
        Json action = {{"type", "pass"}};
        if (carried > 0 && std::find(bases.begin(), bases.end(),
                                     std::make_pair(x, y)) != bases.end())
          action = {{"type", "deposit"}};
        else if ((type == "gold" || type == "iron") && carried < u.value("inventoryCapacity", 8))
          action = {{"type", "mine"}};
        out[u.value("unitId", "")] = action;
      }
    } catch (...) {
    }
    return out;
  }

  std::filesystem::path memoryPath() const {
    const char *dir = std::getenv("AH_MEMORY_DIR");
    std::string name = "ah-memory-" + match_ + ".json";
    for (char &c : name)
      if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '.')
        c = '_';
    return std::filesystem::path(dir && *dir ? dir : ".") / name;
  }

  void save() const {
    try {
      Json j;
      j["match"] = match_, j["team"] = team_, j["turn"] = turn_;
      j["width"] = W_, j["height"] = H_, j["obstacle"] = obstacle_;
      j["startWorkers"] = startWorkers_;
      std::vector<int> terrain(N_), res(N_), amount(N_), seen(N_);
      for (int i = 0; i < N_; ++i)
        terrain[i] = mem_[i].terrain, res[i] = mem_[i].res,
        amount[i] = mem_[i].amount, seen[i] = mem_[i].seen;
      j["terrain"] = terrain, j["res"] = res, j["amount"] = amount, j["seen"] = seen;
      Json bases = Json::array();
      for (const auto &[id, b] : enemyBases_)
        bases.push_back({{"id", id}, {"team", b.team}, {"pos", b.pos}});
      j["enemyBases"] = bases;
      const auto path = memoryPath();
      auto tmp = path;
      tmp += ".tmp";
      {
        std::ofstream file(tmp);
        file << j.dump();
      }
      std::filesystem::rename(tmp, path);
    } catch (const std::exception &error) {
      std::cerr << "[ah] could not save memory: " << error.what() << '\n';
    }
  }

  void load() {
    if (!P.persist)
      return;
    try {
      std::ifstream file(memoryPath());
      if (!file)
        return;
      const Json j = Json::parse(file);
      if (j.value("match", "") != match_ || j.value("team", "") != team_ ||
          j.value("width", 0) != W_ || j.value("height", 0) != H_)
        return;
      const auto terrain = j.at("terrain").get<std::vector<int>>();
      const auto res = j.at("res").get<std::vector<int>>();
      const auto amount = j.at("amount").get<std::vector<int>>();
      const auto seen = j.at("seen").get<std::vector<int>>();
      for (int i = 0; i < N_ && i < static_cast<int>(terrain.size()); ++i)
        mem_[i] = {static_cast<std::uint8_t>(terrain[i]),
                   static_cast<std::uint8_t>(res[i]), amount[i], seen[i]};
      for (const Json &b : j.at("enemyBases"))
        enemyBases_[b.at("id")] = {b.at("id"), b.at("team"), b.at("pos")};
      obstacle_ = j.value("obstacle", 3);
      startWorkers_ = j.value("startWorkers", 1);
      std::cerr << "[ah] restored map memory from turn " << j.value("turn", -1)
                << '\n';
    } catch (const std::exception &error) {
      std::cerr << "[ah] ignoring saved memory: " << error.what() << '\n';
    }
  }
};

} // namespace ah
