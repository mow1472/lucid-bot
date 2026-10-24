#pragma once

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <curl/curl.h>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

namespace lpc {
using Json = nlohmann::json;
using Actions = std::map<std::string, Json>;
// Game state uses the JSON field names documented in /help. Use at() for
// required fields; contains()/value() for optional ones (e.g. enemy inventory).
using Strategy = std::function<Actions(const Json &, const Json &)>;
using Clock = std::chrono::steady_clock;
constexpr std::uint64_t max_revision = 9007199254740991ULL;

class ApiError : public std::runtime_error {
public:
  long status;
  std::string code, detail;
  ApiError(long status, const Json &body)
      : std::runtime_error(message(status, body)), status(status),
        code(body.value("error", Json::object()).value("code", "")),
        detail(body.value("error", Json::object()).value("message", "")) {}
  bool inactive() const {
    return status == 403 && code == "forbidden" &&
           detail == "Team is not in an active match.";
  }
  bool fatal() const {
    return status == 401 || status == 403 || code == "bot_session_conflict";
  }

private:
  static std::string message(long status, const Json &body) {
    const auto error = body.value("error", Json::object());
    const auto code = error.value("code", "");
    if (status == 403 &&
        error.value("message", "") == "Team is not in an active match.")
      return "No current match is available; join a match before starting the "
             "bot.";
    if (code == "pvp_disabled")
      return "No match assigned. PvP practice is currently disabled. Create a "
             "practice match on the website or ask an organizer to assign one, "
             "then run your bot again.";
    if (code == "bot_session_conflict")
      return "This bot was replaced by another client for this team and match.";
    if (status == 401 || status == 403)
      return "API key was rejected; check API_KEY.";
    if (status == 404)
      return "Cannot reach the competition API; check LUCID_API_BASE_URL.";
    return "Server request failed (HTTP " + std::to_string(status) + ").";
  }
};

class BotClient {
  struct CurlRuntime {
    CurlRuntime() {
      if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
        throw std::runtime_error("Could not initialize HTTPS.");
    }
    ~CurlRuntime() { curl_global_cleanup(); }
  } runtime_;
  std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> http_{curl_easy_init(),
                                                            curl_easy_cleanup};
  std::string base_, key_, session_, match_, revision_;
  bool recovering_ = false;
  static constexpr const char *current = "/api/team/matches/current";

  static std::size_t write(char *data, std::size_t size, std::size_t count,
                           void *output) noexcept {
    try {
      static_cast<std::string *>(output)->append(data, size * count);
      return size * count;
    } catch (...) {
      return 0;
    }
  }
  static std::size_t header(char *data, std::size_t size, std::size_t count,
                            void *output) noexcept {
    try {
      std::string line(data, size * count);
      auto colon = line.find(':');
      if (colon != std::string::npos) {
        auto name = line.substr(0, colon);
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        auto value = line.substr(colon + 1);
        auto first = value.find_first_not_of(" \t\r\n");
        value = first == std::string::npos
                    ? ""
                    : value.substr(first, value.find_last_not_of(" \t\r\n") -
                                              first + 1);
        (*static_cast<std::map<std::string, std::string> *>(output))[name] =
            value;
      }
      return size * count;
    } catch (...) {
      return 0;
    }
  }
  static std::string env(const char *key) {
    const auto *value = std::getenv(key);
    return value ? value : "";
  }
  static std::string token() {
    std::random_device random;
    std::ostringstream out;
    for (int i = 0; i < 16; i++) {
      unsigned byte = random() & 255;
      if (i == 6)
        byte = (byte & 15) | 64;
      if (i == 8)
        byte = (byte & 63) | 128;
      if (i == 4 || i == 6 || i == 8 || i == 10)
        out << '-';
      out << std::hex << std::setw(2) << std::setfill('0') << byte;
    }
    return out.str();
  }
  static bool terminal(const Json &state) {
    const auto status = state.at("match").at("status");
    return status == "completed" || status == "cancelled";
  }
  Json request(const std::string &method, const std::string &path,
               const Json *body = nullptr, bool retry = true) {
    const int attempts = retry ? 3 : 1;
    const auto payload = body ? body->dump() : "";
    for (int attempt = 0; attempt < attempts; ++attempt) {
      // Reset options, retaining this handle's connection pool.
      curl_easy_reset(http_.get());
      std::string text;
      std::map<std::string, std::string> response_headers;
      curl_slist *raw_headers = nullptr;
      auto add = [&](const std::string &value) {
        auto *updated = curl_slist_append(raw_headers, value.c_str());
        if (!updated) {
          curl_slist_free_all(raw_headers);
          throw std::bad_alloc();
        }
        raw_headers = updated;
      };
      add("x-api-key: " + key_);
      if (!session_.empty())
        add("x-bot-session: " + session_);
      if (body)
        add("Content-Type: application/json");
      if (path == std::string(current) + "/register" && !match_.empty()) {
        add("x-bot-match-id: " + match_);
        add(recovering_ ? "x-bot-session-recover: 1"
                        : "x-bot-session-revision: " + revision_);
      }
      std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers(
          raw_headers, curl_slist_free_all);
      const std::string url = base_ + path;
      curl_easy_setopt(http_.get(), CURLOPT_URL, url.c_str());
      curl_easy_setopt(http_.get(), CURLOPT_CUSTOMREQUEST, method.c_str());
      curl_easy_setopt(http_.get(), CURLOPT_HTTPHEADER, headers.get());
      curl_easy_setopt(http_.get(), CURLOPT_TIMEOUT_MS, 5000L);
      curl_easy_setopt(http_.get(), CURLOPT_NOSIGNAL, 1L);
      // Never follow redirects with team/session headers. TLS verification
      // stays on.
      curl_easy_setopt(http_.get(), CURLOPT_FOLLOWLOCATION, 0L);
      curl_easy_setopt(http_.get(), CURLOPT_WRITEFUNCTION, &write);
      curl_easy_setopt(http_.get(), CURLOPT_WRITEDATA, &text);
      curl_easy_setopt(http_.get(), CURLOPT_HEADERFUNCTION, &header);
      curl_easy_setopt(http_.get(), CURLOPT_HEADERDATA, &response_headers);
      if (body) {
        curl_easy_setopt(http_.get(), CURLOPT_POSTFIELDS, payload.c_str());
        curl_easy_setopt(http_.get(), CURLOPT_POSTFIELDSIZE_LARGE,
                         static_cast<curl_off_t>(payload.size()));
      }
      const auto result = curl_easy_perform(http_.get());
      if (result != CURLE_OK) {
        if (attempt + 1 == attempts)
          throw std::runtime_error("Cannot reach " + base_ +
                                   "; check the URL and your connection.");
      } else {
        long status = 0;
        curl_easy_getinfo(http_.get(), CURLINFO_RESPONSE_CODE, &status);
        auto json = Json::parse(text, nullptr, false);
        if (status >= 200 && status < 300) {
          if (response_headers.count("x-bot-session-revision"))
            revision_ = response_headers.at("x-bot-session-revision");
          if (path == std::string(current) + "/register" &&
              response_headers.count("x-bot-session"))
            session_ = response_headers.at("x-bot-session");
          if (!json.is_object())
            throw std::runtime_error("Cannot read the competition API at " +
                                     base_ + "; check LUCID_API_BASE_URL.");
          return json;
        }
        if (!json.is_object() || !json.contains("error") ||
            !json["error"].is_object())
          json = Json::object();
        ApiError error(status, json);
        if (error.code == "bot_session_missing" && !recovering_ &&
            !match_.empty() && !session_.empty() &&
            path != std::string(current) + "/register") {
          recovering_ = true;
          try {
            request("POST", std::string(current) + "/register");
            auto recovered = request(method, path, body, retry);
            recovering_ = false;
            return recovered;
          } catch (...) {
            recovering_ = false;
            throw;
          }
        }
        const bool transient = status == 408 || status == 429 ||
                               status == 500 || status == 502 ||
                               status == 503 || status == 504;
        if (!transient || attempt + 1 == attempts)
          throw error;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(250 << attempt));
    }
    throw std::runtime_error("Request failed.");
  }
  bool submission_failure(const ApiError &error, const std::string &path) {
    if (!error.fatal())
      return false;
    if (!error.inactive())
      throw error;
    try {
      return terminal(request("GET", path + "/state"));
    } catch (const ApiError &latest) {
      if (latest.fatal() && !latest.inactive())
        throw;
    } catch (const std::exception &latest) {
      std::cerr << latest.what() << '\n';
    }
    return false;
  }

public:
  BotClient() : base_(env("LUCID_API_BASE_URL")), key_(env("API_KEY")) {
    if (!http_)
      throw std::runtime_error("Could not initialize HTTPS.");
    if (key_.find_first_not_of(" \t\r\n") == std::string::npos ||
        key_ == "replace-with-api-key")
      throw std::runtime_error(
          "Set API_KEY to your team's API key before starting the bot.");
    if (key_.find_first_of("\r\n") != std::string::npos)
      throw std::runtime_error("Invalid API_KEY.");
    if (base_.empty()) {
      base_ = "http://localhost:5173";
      std::cerr << "LUCID_API_BASE_URL is not set; using " << base_ << '\n';
    }
    while (!base_.empty() && base_.back() == '/')
      base_.pop_back();
    if (!(base_.starts_with("https://") || base_.starts_with("http://")) ||
        base_.find_first_of("\r\n?#@") != std::string::npos)
      throw std::runtime_error(
          "Invalid LUCID_API_BASE_URL; use the server's HTTP or HTTPS URL.");
  }
  void run(const std::string &rules, const Strategy &strategy) {
    const auto identity = request("POST", "/api/auth/api-key");
    Json state;
    try {
      state = request("GET", std::string(current) + "/state");
    } catch (const ApiError &error) {
      if (!error.inactive())
        throw;
      std::string announced;
      while (true) {
        const auto enrollment =
            request("POST", std::string(current) + "/register", nullptr, false);
        if (enrollment.contains("match")) {
          state = request("GET", std::string(current) + "/state");
          break;
        }
        if (!enrollment.contains("queue"))
          throw std::runtime_error("Unexpected registration response.");
        const std::string waiting = enrollment.at("queue").value("autoQueue", true)
            ? "Waiting in the PvP practice queue; keep your bot running."
            : "Waiting for a private practice match. Create one on the website; keep your bot running.";
        if (announced != waiting) {
          std::cout << waiting << std::endl;
          announced = waiting;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
      }
    }
    match_ = state.at("match").at("matchId").get<std::string>();
    session_ = token();
    state = request("POST", std::string(current) + "/register");
    if (state.at("match").at("rulesVersion") != rules)
      throw std::runtime_error("This starter does not support the current "
                               "game. Download its starter from Help.");
    std::cout << "registered for match " << match_ << std::endl;
    std::unique_ptr<char, decltype(&curl_free)> escaped(
        curl_easy_escape(http_.get(), match_.c_str(),
                         static_cast<int>(match_.size())),
        curl_free);
    if (!escaped)
      throw std::bad_alloc();
    const std::string path = std::string("/api/team/matches/") + escaped.get();
    auto revisions =
        state.value("actionRevisions", std::map<std::string, std::uint64_t>{});
    for (const auto &[entity, revision] : revisions)
      if (revision > max_revision)
        throw std::runtime_error("Invalid action revision.");
    const auto run_id = session_;
    int submitted_turn = -1;
    auto received_at = Clock::now();
    while (!terminal(state)) {
      const int turn = state.at("match").at("turn");
      if (state.at("match").at("status") == "active" &&
          state.contains("view") && !state["view"].is_null() &&
          submitted_turn != turn) {
        Actions actions;
        try {
          actions = strategy(state, identity);
        } catch (...) {
          std::cerr << "Strategy failed on turn " << turn
                    << "; submitting no actions.\n";
        }
        Json submissions = Json::array();
        for (const auto &[entity, action] : actions) {
          if (revisions[entity] >= max_revision)
            throw std::runtime_error(
                "Action revision exceeds the server's safe integer range.");
          submissions.push_back({{"entityId", entity},
                                 {"revision", ++revisions[entity]},
                                 {"action", action}});
        }
        Json body = {{"submissionId", run_id + "-" + std::to_string(turn)},
                     {"submissions", submissions}};
        try {
          auto receipt = request("PUT", path + "/action", &body);
          const auto elapsed =
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  Clock::now() - received_at)
                  .count();
          bool late = state.contains("turnEndsAtMs") &&
                      !state["turnEndsAtMs"].is_null() &&
                      state.value("serverTimeMs", 0LL) + elapsed >=
                          state["turnEndsAtMs"].get<long long>();
          for (const auto &accepted : receipt.value("accepted", Json::array()))
            late = late || accepted.value("acceptedForTurn", 0) > turn + 1;
          if (late)
            std::cerr << "Turn " << turn
                      << " actions were late; use a faster strategy. Fetching "
                         "the newest state.\n";
        } catch (const std::exception &error) {
          const auto *api = dynamic_cast<const ApiError *>(&error);
          if (api && submission_failure(*api, path))
            break;
          std::cerr << "Could not submit turn " << turn << ": " << error.what()
                    << "; continuing.\n";
          Json empty = {
              {"submissionId", run_id + "-" + std::to_string(turn) + "-empty"},
              {"submissions", Json::array()}};
          try {
            request("PUT", path + "/action", &empty);
          } catch (const std::exception &fallback) {
            const auto *failed = dynamic_cast<const ApiError *>(&fallback);
            if (failed && submission_failure(*failed, path))
              break;
            std::cerr << "Could not submit empty actions: " << fallback.what()
                      << '\n';
          }
        }
        submitted_turn = turn;
        try {
          state = request("GET", path + "/state");
          received_at = Clock::now();
          if (state.at("match").at("turn") != turn)
            continue;
        } catch (const ApiError &error) {
          if (error.fatal())
            throw;
          std::cerr << error.what() << '\n';
        } catch (const std::exception &error) {
          std::cerr << error.what() << '\n';
        }
      }
      long long delay = 100;
      if (state.at("match").value("turnAdvanceMode", "") == "timed" &&
          state.contains("turnEndsAtMs") && !state["turnEndsAtMs"].is_null()) {
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() -
                                                                  received_at)
                .count();
        const auto remaining = state["turnEndsAtMs"].get<long long>() -
                               state.value("serverTimeMs", 0LL) - elapsed;
        if (remaining > 0)
          delay = std::clamp(remaining - 25, 25LL, 1000LL);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(delay));
      try {
        state = request("GET", path + "/state");
        received_at = Clock::now();
      } catch (const ApiError &error) {
        if (error.fatal())
          throw;
        std::cerr << error.what() << '\n';
      } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
      }
    }
    const auto reason = state.at("match").value("cancellationReason", Json());
    std::cout << (reason.is_string() ? reason.get<std::string>()
                                     : "Match " + match_ + " ended.")
              << std::endl;
  }
};
} // namespace lpc
