// =============================================================================
// index_manager.hpp
// Gerenciamento de indices secundarios (B-Tree) sobre campos de documentos,
// usados para acelerar find/findOne/count sem exigir varredura completa da
// colecao. Cada indice mapeia valor de campo -> lista de RecordLocation no
// storage engine (ver storage_engine.hpp).
// =============================================================================
#pragma once

#include <string>
#include <vector>

#include "nexodb/common.hpp"
#include "nexodb/document.hpp"
#include "nexodb/storage_engine.hpp"

namespace nexodb::index {

struct IndexSpec {
  std::string field_name;
  bool unique = false;
};

// Interface de um unico indice B-Tree sobre um campo.
class BTreeIndex {
 public:
  explicit BTreeIndex(IndexSpec spec) : spec_(std::move(spec)) {}
  virtual ~BTreeIndex() = default;

  virtual common::Status Insert(const document::Value& key,
                                 storage::RecordLocation location) = 0;
  virtual common::Status Remove(const document::Value& key,
                                 storage::RecordLocation location) = 0;
  virtual std::vector<storage::RecordLocation> Lookup(
      const document::Value& key) const = 0;

  const IndexSpec& spec() const { return spec_; }

 protected:
  IndexSpec spec_;
};

// Coordena todos os indices de uma colecao: cria, remove e consulta.
class IndexManager {
 public:
  common::Status CreateIndex(const IndexSpec& spec);
  common::Status DropIndex(const std::string& field_name);
  BTreeIndex* FindIndexFor(const std::string& field_name) const;

 private:
  std::vector<std::unique_ptr<BTreeIndex>> indexes_;
};

}  // namespace nexodb::index
