// =============================================================================
// document.hpp
// Modelo de documento do NexoDB: uma estrutura tipo BSON/JSON que representa
// cada registro armazenado. E a unidade de dados que circula entre o
// storage engine, o motor de consultas, os indices e o binding Node-API.
// Mantido deliberadamente simples aqui (sem logica de persistencia): este
// arquivo descreve apenas a FORMA dos dados em memoria.
// =============================================================================
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

#include "nexodb/common.hpp"

namespace nexodb::document {

// Identificador unico de documento, equivalente ao ObjectId do MongoDB
// (timestamp de 4 bytes + maquina/processo + contador), usado como _id
// padrao quando o chamador nao fornece um.
class ObjectId {
 public:
  static ObjectId Generate();
  std::string ToHexString() const;

  // Acesso aos bytes para comparacao/serializacao
  const uint8_t* Bytes() const { return bytes_; }
  uint8_t* Bytes() { return bytes_; }

  // Operadores de comparacao
  bool operator==(const ObjectId& other) const {
    return std::memcmp(bytes_, other.bytes_, 12) == 0;
  }
  bool operator!=(const ObjectId& other) const { return !(*this == other); }
  bool operator<(const ObjectId& other) const {
    return std::memcmp(bytes_, other.bytes_, 12) < 0;
  }

 private:
  uint8_t bytes_[12]{};
};

class Value;  // forward decl, Value pode conter outros Values (array/objeto)

using Array = std::vector<Value>;
using Object = std::map<std::string, Value>;

// Valor escalar ou composto armazenavel em um documento. Cobre os tipos
// essenciais de JSON (null, bool, number, string, array, object) mais
// extensoes tipo-BSON usadas internamente (ObjectId, data/hora, binario).
class Value {
 public:
  using Storage = std::variant<std::nullptr_t, bool, double, int64_t,
                                std::string, Array, Object, ObjectId>;

  Value() : storage_(nullptr) {}
  Value(Storage v) : storage_(std::move(v)) {}

  const Storage& storage() const { return storage_; }

 private:
  Storage storage_;
};

// Um documento e, na pratica, um Object (mapa ordenado chave -> Value) com
// um campo "_id" obrigatorio apos a primeira gravacao.
class Document {
 public:
  Document() = default;
  explicit Document(Object fields) : fields_(std::move(fields)) {}

  const Object& fields() const { return fields_; }
  Object& fields() { return fields_; }

  // Garante que o documento possui "_id", gerando um ObjectId se necessario.
  void EnsureId() const;

 private:
  mutable Object fields_;
};

// Serializacao/Deserializacao binaria (formato tipo-BSON)
common::Result<common::Void> Serialize(const Document& doc, std::vector<uint8_t>& out);
common::Result<common::Void> Deserialize(const uint8_t* data, size_t length, Document& out);

}  // namespace nexodb::document
