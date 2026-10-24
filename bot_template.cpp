#include "bot_client.hpp"
#include "game_helpers.hpp"

using namespace lpc;

Actions calculate_actions(const Json &state, const Json &identity) {
  Actions actions;
  for (const auto &worker : state.at("view").at("units")) {
    if (worker.at("teamId") != identity.at("teamId"))
      continue;
    // Write your strategy here. Example: {{"type", "move"}, {"direction",
    // "north"}}.
    actions[worker.at("unitId").get<std::string>()] = {{"type", "pass"}};
  }
  return actions;
}

int main() {
  try {
    BotClient().run("lpc2026", calculate_actions);
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
