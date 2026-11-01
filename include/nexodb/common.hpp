// =============================================================================
// common.hpp
// Tipos basicos compartilhados por todos os modulos do NexoDB: codigo de
// status de operacoes (sucesso/erro) e um wrapper Result<T> inspirado em
// std::expected, usado no lugar de excecoes nos caminhos "quentes" do core
// para evitar custo de stack unwinding em operacoes de banco de dados.
// =============================================================================
#pragma once

#include <string>
#include <variant>

namespace nexodb::common {

// Codigos de erro padronizados retornados pelas operacoes do core.
enum class StatusCode {
  kOk = 0,
  kNotFound,
  kAlreadyExists,
  kInvalidArgument,
  kCorruptedData,
  kIOError,
  kLockTimeout,
  kTransactionConflict,
  kOutOfMemory,
  kUnsupported,
};

// Representa o resultado de uma operacao que pode falhar, sem usar excecoes.
class Status {
 public:
  static Status Ok() { return Status(StatusCode::kOk, ""); }
  static Status Error(StatusCode code, std::string message) {
    return Status(code, std::move(message));
  }

  bool ok() const { return code_ == StatusCode::kOk; }
  StatusCode code() const { return code_; }
  const std::string& message() const { return message_; }

 private:
  Status(StatusCode code, std::string message)
      : code_(code), message_(std::move(message)) {}

  StatusCode code_;
  std::string message_;
};

// Placeholder para void em Result<void>
struct Void {};

// Result<T>: carrega um valor T em caso de sucesso, ou um Status de erro.
// TODO: substituir por std::expected<T, Status> quando o baseline de C++
// do projeto puder assumir C++23 em todas as plataformas suportadas.
template <typename T>
class Result {
 public:
  Result() : data_(Status::Error(StatusCode::kUnsupported, "Default constructed Result")) {}
  Result(T value) : data_(std::move(value)) {}
  Result(Status status) : data_(std::move(status)) {}

  bool ok() const { return std::holds_alternative<T>(data_); }
  const T& value() const { return std::get<T>(data_); }
  T& value() { return std::get<T>(data_); }
  const Status& status() const { return std::get<Status>(data_); }

 private:
  std::variant<T, Status> data_;
};

// Especializacao para void
template <>
class Result<Void> {
 public:
  Result() : has_value_(true), status_(Status::Ok()) {}
  Result(Status status) : has_value_(false), status_(std::move(status)) {}

  bool ok() const { return has_value_; }
  Void value() const { return {}; }
  const Status& status() const { return status_; }

 private:
  bool has_value_;
  Status status_;
};

}  // namespace nexodb::common
