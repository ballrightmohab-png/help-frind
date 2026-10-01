#pragma once

/**
 * Build-time shim of the preloader's logging API.
 *
 * The real `pl::log::Logger` is provided by libpreloader.so and formats through
 * fmt (and compiles with exceptions enabled).  LeviBoost builds with
 * `-fno-exceptions` like the rest of the SDK-driven mods, so this header keeps
 * the exact same public surface but formats with a tiny built-in `{}`
 * replacer and writes straight to logcat.  `Logger::getOrCreate` is header
 * inline in the real SDK too, so no libpreloader symbol is needed for logging.
 */

#include <android/log.h>

#include <cstdio>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace pl::log {

namespace detail {

inline std::string Stringify(const char *value) { return value ? value : ""; }
inline std::string Stringify(const std::string &value) { return value; }
inline std::string Stringify(std::string_view value) {
  return std::string(value);
}
inline std::string Stringify(bool value) { return value ? "true" : "false"; }
inline std::string Stringify(char value) { return std::string(1, value); }

template <typename T> std::string Stringify(const T &value) {
  std::ostringstream stream;
  stream << value;
  return stream.str();
}

inline void FormatInto(std::string &out, std::string_view format) {
  out.append(format.data(), format.size());
}

template <typename T, typename... Rest>
void FormatInto(std::string &out, std::string_view format, T &&value,
                Rest &&...rest) {
  const std::size_t placeholder = format.find("{}");
  if (placeholder == std::string_view::npos) {
    out.append(format.data(), format.size());
    return;
  }
  out.append(format.data(), placeholder);
  out.append(Stringify(std::forward<T>(value)));
  FormatInto(out, format.substr(placeholder + 2),
             std::forward<Rest>(rest)...);
}

} // namespace detail

/**
 * @brief Named Android logger (no fmt; logs plain text to logcat).
 */
class Logger {
public:
  explicit Logger(std::string name) : mLoggerName(std::move(name)) {}

  static Logger &getOrCreate(std::string name) {
    std::lock_guard<std::mutex> lock(sLoggerMutex);
    const auto it = sLoggers.find(name);
    if (it != sLoggers.end()) {
      return *it->second;
    }
    auto logger = std::make_unique<Logger>(std::move(name));
    Logger &ref = *logger;
    sLoggers.emplace(ref.mLoggerName, std::move(logger));
    return ref;
  }

  [[nodiscard]] const std::string &name() const noexcept { return mLoggerName; }

  template <typename... Args>
  void info(std::string_view format, Args &&...args) const {
    emit(ANDROID_LOG_INFO, format, std::forward<Args>(args)...);
  }

  template <typename... Args>
  void debug(std::string_view format, Args &&...args) const {
    emit(ANDROID_LOG_DEBUG, format, std::forward<Args>(args)...);
  }

  template <typename... Args>
  void warn(std::string_view format, Args &&...args) const {
    emit(ANDROID_LOG_WARN, format, std::forward<Args>(args)...);
  }

  template <typename... Args>
  void error(std::string_view format, Args &&...args) const {
    emit(ANDROID_LOG_ERROR, format, std::forward<Args>(args)...);
  }

private:
  template <typename... Args>
  void emit(int androidLevel, std::string_view format, Args &&...args) const {
    std::string message;
    detail::FormatInto(message, format, std::forward<Args>(args)...);
    writeLine(androidLevel, message);
  }

  void writeLine(int androidLevel, const std::string &message) const {
    __android_log_print(androidLevel, mLoggerName.c_str(), "%s",
                        message.c_str());
  }

  std::string mLoggerName;

  inline static std::unordered_map<std::string, std::unique_ptr<Logger>>
      sLoggers{};
  inline static std::mutex sLoggerMutex{};
};

} // namespace pl::log

/**
 * @brief Shared logger used by the preloader runtime itself.
 */
inline auto &preloaderLogger = pl::log::Logger::getOrCreate("Preloader");
