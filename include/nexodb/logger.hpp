// =============================================================================
// logger.hpp
// Logging minimo e sem dependencias externas, usado por todos os modulos
// para registrar eventos de diagnostico (abertura de arquivos, checkpoints
// de transacao, erros de I/O, etc.). Pensado para ser trocado por uma
// implementacao mais robusta (ex.: spdlog) sem alterar os chamadores.
// =============================================================================
#pragma once

#include <string_view>
#include <iostream>
#include <cstdarg>
#include <atomic>

namespace nexodb::common {

enum class LogLevel { kDebug, kInfo, kWarn, kError };

class Logger {
 public:
  static void Log(LogLevel level, std::string_view component,
                   std::string_view message);

  static void SetMinLevel(LogLevel level) { min_log_level_.store(level); }
  static LogLevel GetMinLevel() { return min_log_level_.load(); }

 private:
  static inline std::atomic<LogLevel> min_log_level_{LogLevel::kInfo};  // Default: suppress DEBUG
};

// Simple logging macros - just pass the message directly (pre-formatted)
#define NEXODB_LOG_DEBUG(component, message) \
    do { if (nexodb::common::Logger::GetMinLevel() <= nexodb::common::LogLevel::kDebug) \
         nexodb::common::Logger::Log(nexodb::common::LogLevel::kDebug, component, message); } while(0)

#define NEXODB_LOG_INFO(component, message) \
    do { if (nexodb::common::Logger::GetMinLevel() <= nexodb::common::LogLevel::kInfo) \
         nexodb::common::Logger::Log(nexodb::common::LogLevel::kInfo, component, message); } while(0)

#define NEXODB_LOG_WARN(component, message) \
    do { if (nexodb::common::Logger::GetMinLevel() <= nexodb::common::LogLevel::kWarn) \
         nexodb::common::Logger::Log(nexodb::common::LogLevel::kWarn, component, message); } while(0)

#define NEXODB_LOG_ERROR(component, message) \
    do { if (nexodb::common::Logger::GetMinLevel() <= nexodb::common::LogLevel::kError) \
         nexodb::common::Logger::Log(nexodb::common::LogLevel::kError, component, message); } while(0)

}  // namespace nexodb::common
