// =============================================================================
// logger.cpp
// Implementacao minima do Logger (stderr). Trocar por um backend mais rico
// (niveis configuraveis, saida em arquivo, rotacao) conforme necessidade.
// =============================================================================
#include "nexodb/logger.hpp"

#include <cstdio>

namespace nexodb::common {

namespace {
const char* LevelToString(LogLevel level) {
  switch (level) {
    case LogLevel::kDebug: return "DEBUG";
    case LogLevel::kInfo:  return "INFO";
    case LogLevel::kWarn:  return "WARN";
    case LogLevel::kError: return "ERROR";
  }
  return "UNKNOWN";
}
}  // namespace

void Logger::Log(LogLevel level, std::string_view component,
                  std::string_view message) {
  std::fprintf(stderr, "[nexodb][%s][%.*s] %.*s\n", LevelToString(level),
               static_cast<int>(component.size()), component.data(),
               static_cast<int>(message.size()), message.data());
}

}  // namespace nexodb::common
