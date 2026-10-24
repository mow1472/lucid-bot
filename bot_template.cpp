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
  // The starter client exits on any network error, even while waiting in the
  // practice queue. Restart it after transient failures; map memory survives
  // in the static Brain. A rejected key always stops the bot. A session
  // conflict stops it too, unless we are reclaiming our own session after a
  // restart (the server holds a dropped session for up to 60 seconds).
  const auto started = Clock::now();
  bool registered_before = false;
  for (int attempt = 1; attempt <= 50; ++attempt) {
    try {
      BotClient().run("lpc2026", calculate_actions);
      return 0;
    } catch (const ApiError &error) {
      std::cerr << error.what() << '\n';
      const bool reclaiming = error.code == "bot_session_conflict" &&
                              registered_before &&
                              Clock::now() - started < std::chrono::minutes(15);
      if (error.fatal() && !reclaiming)
        return 1;
    } catch (const std::exception &error) {
      std::cerr << error.what() << '\n';
    }
    registered_before = registered_before || brain().lastMs() > 0;
    std::cerr << "Restarting the client in 3 seconds (attempt " << attempt
              << ")\n";
    std::this_thread::sleep_for(std::chrono::seconds(3));
  }
  return 1;
}
