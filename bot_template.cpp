#include "bot_client.hpp"
#include "adaptive_hybrid.hpp"

using namespace lpc;

// One strategy instance for the process keeps map memory between turns.
// AH_VARIANT and AH_PARAMS select and tune it (see adaptive_hybrid.hpp).
static ah::Brain &brain() {
  static ah::Brain instance(ah::load_params());
  return instance;
}

Actions calculate_actions(const Json &state, const Json &identity) {
  return brain().decide(state, identity.at("teamId").get<std::string>());
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
