// =============================================================================
// query_engine.hpp
// Motor de consultas: interpreta filtros no estilo MongoDB (ex.: {age: {$gt:
// 18}}) e operadores de atualizacao (ex.: {$set: {...}}, {$inc: {...}}),
// decide se pode usar um indice (via index_manager.hpp) ou se precisa
// varrer a colecao, e produz os resultados consumidos pelos metodos
// publicos expostos ao Node.js (find, findOne, updateMany, etc.).
// =============================================================================
#pragma once

#include <optional>
#include <vector>

#include "nexodb/common.hpp"
#include "nexodb/document.hpp"
#include "nexodb/index_manager.hpp"
#include "nexodb/storage_engine.hpp"

namespace nexodb::query {

// Representa um filtro de consulta ja parseado (arvore de condicoes).
// TODO: modelar operadores logicos ($and, $or, $nor) e de comparacao
// ($eq, $ne, $gt, $gte, $lt, $lte, $in, $nin, $exists, $regex...).
struct Filter {
  document::Object raw;  // placeholder: filtro bruto ate o parser existir
};

// Representa uma operacao de atualizacao ja parseada ($set, $unset, $inc,
// $push, $pull, $addToSet, etc.).
struct UpdateSpec {
  document::Object raw;  // placeholder ate o parser de operadores existir
};

struct QueryOptions {
  std::optional<document::Object> projection;
  std::optional<document::Object> sort;
  int64_t skip = 0;
  int64_t limit = 0;  // 0 = sem limite
};

// Orquestra storage + indices para resolver as operacoes equivalentes aos
// metodos do Mongoose listados no README (find, findOne, findById,
// updateOne, updateMany, deleteOne, deleteMany, count, etc.).
class QueryEngine {
 public:
  QueryEngine(storage::StorageEngine& storage, index::IndexManager& indexes)
      : storage_(storage), indexes_(indexes) {}

  common::Result<std::vector<document::Document>> Find(
      const Filter& filter, const QueryOptions& options) const;
  common::Result<std::optional<document::Document>> FindOne(
      const Filter& filter, const QueryOptions& options) const;
  common::Result<int64_t> Count(const Filter& filter) const;

  // Insert: insere um documento e retorna sua localizacao
  common::Result<storage::RecordLocation> InsertOne(const document::Document& doc);

  common::Result<int64_t> UpdateOne(const Filter& filter,
                                     const UpdateSpec& update);
  common::Result<int64_t> UpdateMany(const Filter& filter,
                                      const UpdateSpec& update);
  common::Result<int64_t> DeleteOne(const Filter& filter);
  common::Result<int64_t> DeleteMany(const Filter& filter);

  // Busca direta por _id usando indice primario do storage engine (O(1) medio)
  common::Result<std::optional<document::Document>> FindById(
      const document::ObjectId& oid) const;

 private:
  storage::StorageEngine& storage_;
  index::IndexManager& indexes_;
};

}  // namespace nexodb::query
