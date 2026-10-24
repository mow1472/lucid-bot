#pragma once
#include "bot_client.hpp"
#include <optional>
#include <queue>
#include <set>
#include <vector>

namespace lpc {
using Position = std::pair<int, int>;
inline Position position(const Json &p) {
  return {p.at("x").get<int>(), p.at("y").get<int>()};
}
inline std::vector<std::string> is_claimed(const Json &bases, Position p) {
  std::set<std::string> owners;
  for (const auto &base : bases) {
    auto [x, y] = position(base.at("position"));
    if (std::abs(x - p.first) + std::abs(y - p.second) <= 2)
      owners.insert(base.at("teamId"));
  }
  return {owners.begin(), owners.end()};
}
inline std::optional<std::string> is_base(const Json &bases, Position p) {
  for (const auto &base : bases)
    if (position(base.at("position")) == p)
      return base.at("teamId").get<std::string>();
  return std::nullopt;
}
inline const Json *tile_at(const Json &grid, Position p) {
  auto [x, y] = p;
  const int width = grid.at("width"), height = grid.at("height");
  if (x < 0 || y < 0 || x >= width || y >= height)
    return nullptr;
  const auto index = static_cast<std::size_t>(y) * width + x;
  const auto &cells = grid.at("cells");
  return index < cells.size() ? &cells.at(index) : nullptr;
}
inline std::optional<std::string> is_ore(const Json &grid, Position p) {
  auto *tile = tile_at(grid, p);
  if (tile && (tile->at("type") == "gold" || tile->at("type") == "iron") &&
      tile->value("amount", 0) > 0)
    return tile->at("type").get<std::string>();
  return std::nullopt;
}
inline bool can_move_onto(const Json &grid, const Json &bases, Position p,
                          const std::string &team) {
  auto *tile = tile_at(grid, p);
  auto owner = is_base(bases, p);
  return tile && tile->at("type") != "hidden" && tile->at("type") != "void" &&
         (!owner || *owner == team);
}
// Cost until ready at destination. Start must be ready. nullopt means no known
// route; an empty vector means start == end. Returned steps exclude start.
inline std::optional<std::vector<Position>>
path_to(const Json &grid, Position start, Position end, const Json &bases,
        const std::string &team, int obstacle_cost = 3) {
  if (obstacle_cost < 1 || !can_move_onto(grid, bases, start, team) ||
      !can_move_onto(grid, bases, end, team))
    return std::nullopt;
  using Entry = std::pair<std::int64_t, Position>;
  std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> queue;
  std::map<Position, std::int64_t> distance{{start, 0}};
  std::map<Position, Position> previous;
  queue.push({0, start});
  while (!queue.empty()) {
    auto [cost, p] = queue.top();
    queue.pop();
    if (cost != distance.at(p))
      continue;
    if (p == end) {
      std::vector<Position> path;
      for (auto at = end; at != start; at = previous.at(at))
        path.push_back(at);
      std::reverse(path.begin(), path.end());
      return path;
    }
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx) {
        if (dx == 0 && dy == 0)
          continue;
        Position next{p.first + dx, p.second + dy};
        if (!can_move_onto(grid, bases, next, team))
          continue;
        const auto terrain = tile_at(grid, next)->at("type");
        const auto next_cost =
            cost + ((terrain == "mountain" || terrain == "crater")
                        ? obstacle_cost
                        : 1);
        if (distance.count(next) && distance.at(next) <= next_cost)
          continue;
        distance[next] = next_cost;
        previous[next] = p;
        queue.push({next_cost, next});
      }
  }
  return std::nullopt;
}
} // namespace lpc
