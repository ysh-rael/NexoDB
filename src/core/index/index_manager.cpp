// =============================================================================
// index_manager.cpp
// Implementacao de indice B-Tree para aceleracao de consultas.
// =============================================================================
#include "nexodb/index_manager.hpp"

#include "nexodb/document.hpp"
#include "nexodb/logger.hpp"

#include <algorithm>
#include <memory>
#include <mutex>
#include <vector>

namespace nexodb::index {

namespace {

// No B-Tree simples (apenas em memoria por enquanto)
// Ordem do B-Tree (max filhos = 2 * t)
constexpr int BTREE_ORDER = 32;

struct BTreeNode {
  bool is_leaf = true;
  std::vector<document::Value> keys;
  std::vector<std::vector<storage::RecordLocation>> values;  // Para chaves duplicadas
  std::vector<std::unique_ptr<BTreeNode>> children;

  BTreeNode(bool leaf = true) : is_leaf(leaf) {}
};

class BTreeIndexImpl final : public BTreeIndex {
 public:
  explicit BTreeIndexImpl(IndexSpec spec) : BTreeIndex(std::move(spec)), root_(std::make_unique<BTreeNode>(true)) {}

  common::Status Insert(const document::Value& key, storage::RecordLocation location) override {
    std::lock_guard<std::mutex> lock(mutex_);
    return InsertInternal(root_, key, location);
  }

  common::Status Remove(const document::Value& key, storage::RecordLocation location) override {
    std::lock_guard<std::mutex> lock(mutex_);
    return RemoveInternal(root_, key, location);
  }

  std::vector<storage::RecordLocation> Lookup(const document::Value& key) const override {
    std::lock_guard<std::mutex> lock(mutex_);
    return LookupInternal(root_.get(), key);
  }

 private:
  mutable std::mutex mutex_;
  std::unique_ptr<BTreeNode> root_;

  // Comparacao de chaves para ordenacao no B-Tree
  int CompareKeys(const document::Value& a, const document::Value& b) const {
    const auto& sa = a.storage();
    const auto& sb = b.storage();

    // Null < tudo
    if (std::holds_alternative<std::nullptr_t>(sa) && std::holds_alternative<std::nullptr_t>(sb)) return 0;
    if (std::holds_alternative<std::nullptr_t>(sa)) return -1;
    if (std::holds_alternative<std::nullptr_t>(sb)) return 1;

    // Boolean
    if (std::holds_alternative<bool>(sa) && std::holds_alternative<bool>(sb)) {
      bool va = std::get<bool>(sa);
      bool vb = std::get<bool>(sb);
      return va == vb ? 0 : (va ? 1 : -1);
    }

    // Numeros
    if ((std::holds_alternative<int64_t>(sa) || std::holds_alternative<double>(sa)) &&
        (std::holds_alternative<int64_t>(sb) || std::holds_alternative<double>(sb))) {
      double va = std::holds_alternative<int64_t>(sa) ? std::get<int64_t>(sa) : std::get<double>(sa);
      double vb = std::holds_alternative<int64_t>(sb) ? std::get<int64_t>(sb) : std::get<double>(sb);
      if (va < vb) return -1;
      if (va > vb) return 1;
      return 0;
    }

    // String
    if (std::holds_alternative<std::string>(sa) && std::holds_alternative<std::string>(sb)) {
      return std::get<std::string>(sa).compare(std::get<std::string>(sb));
    }

    // ObjectId
    if (std::holds_alternative<document::ObjectId>(sa) && std::holds_alternative<document::ObjectId>(sb)) {
      const auto& oid_a = std::get<document::ObjectId>(sa);
      const auto& oid_b = std::get<document::ObjectId>(sb);
      for (int i = 0; i < 12; ++i) {
        if (oid_a.Bytes()[i] != oid_b.Bytes()[i]) {
          return oid_a.Bytes()[i] < oid_b.Bytes()[i] ? -1 : 1;
        }
      }
      return 0;
    }

    // Tipos diferentes - ordena por tipo
    auto type_rank = [](const document::Value::Storage& s) -> int {
      if (std::holds_alternative<std::nullptr_t>(s)) return 0;
      if (std::holds_alternative<bool>(s)) return 1;
      if (std::holds_alternative<int64_t>(s)) return 2;
      if (std::holds_alternative<double>(s)) return 3;
      if (std::holds_alternative<std::string>(s)) return 4;
      if (std::holds_alternative<document::Object>(s)) return 5;
      if (std::holds_alternative<document::Array>(s)) return 6;
      if (std::holds_alternative<document::ObjectId>(s)) return 7;
      return 8;
    };
    return type_rank(sa) - type_rank(sb);
  }

  common::Status InsertInternal(std::unique_ptr<BTreeNode>& node, const document::Value& key,
                                storage::RecordLocation location) {
    // Busca posicao de insercao
    size_t i = 0;
    while (i < node->keys.size() && CompareKeys(node->keys[i], key) < 0) {
      ++i;
    }

    if (i < node->keys.size() && CompareKeys(node->keys[i], key) == 0) {
      // Chave ja existe - adiciona localizacao a lista
      if (spec_.unique && !node->values[i].empty()) {
        return common::Status::Error(common::StatusCode::kAlreadyExists,
                                     "Violacao de constraint unique no indice");
      }
      node->values[i].push_back(location);
      return common::Status::Ok();
    }

    if (node->is_leaf) {
      // Insere na folha
      node->keys.insert(node->keys.begin() + i, key);
      node->values.insert(node->values.begin() + i, {location});
    } else {
      // Desce na arvore
      auto status = InsertInternal(node->children[i], key, location);
      if (!status.ok()) return status;

      // Verifica se filho precisa ser dividido
      if (node->children[i]->keys.size() >= 2 * BTREE_ORDER - 1) {
        SplitChild(node, i);
      }
    }

    return common::Status::Ok();
  }

  void SplitChild(std::unique_ptr<BTreeNode>& parent, size_t index) {
    auto& child = parent->children[index];
    auto new_child = std::make_unique<BTreeNode>(child->is_leaf);

    // Move metade das chaves para o novo no
    int mid = BTREE_ORDER - 1;
    document::Value mid_key = std::move(child->keys[mid]);
    std::vector<storage::RecordLocation> mid_values = std::move(child->values[mid]);

    new_child->keys.insert(new_child->keys.end(),
                          child->keys.begin() + mid + 1, child->keys.end());
    new_child->values.insert(new_child->values.end(),
                            child->values.begin() + mid + 1, child->values.end());
    child->keys.resize(mid);
    child->values.resize(mid + 1);

    if (!child->is_leaf) {
      new_child->children.insert(new_child->children.end(),
                                std::make_move_iterator(child->children.begin() + mid + 1),
                                std::make_move_iterator(child->children.end()));
      child->children.resize(mid + 1);
    }

    parent->keys.insert(parent->keys.begin() + index, std::move(mid_key));
    parent->values.insert(parent->values.begin() + index, std::move(mid_values));
    parent->children.insert(parent->children.begin() + index + 1, std::move(new_child));
  }

  common::Status RemoveInternal(std::unique_ptr<BTreeNode>& node, const document::Value& key,
                                storage::RecordLocation location) {
    size_t i = 0;
    while (i < node->keys.size() && CompareKeys(node->keys[i], key) < 0) {
      ++i;
    }

    if (i < node->keys.size() && CompareKeys(node->keys[i], key) == 0) {
      // Encontrou a chave
      auto& locations = node->values[i];
      auto it = std::find(locations.begin(), locations.end(), location);
      if (it != locations.end()) {
        locations.erase(it);
        if (locations.empty()) {
          // Remove a chave completamente
          node->keys.erase(node->keys.begin() + i);
          node->values.erase(node->values.begin() + i);
        }
        return common::Status::Ok();
      }
      return common::Status::Error(common::StatusCode::kNotFound, "Localizacao nao encontrada no indice");
    }

    if (node->is_leaf) {
      return common::Status::Error(common::StatusCode::kNotFound, "Chave nao encontrada");
    }

    return RemoveInternal(node->children[i], key, location);
  }

  std::vector<storage::RecordLocation> LookupInternal(const BTreeNode* node,
                                                      const document::Value& key) const {
    size_t i = 0;
    while (i < node->keys.size() && CompareKeys(node->keys[i], key) < 0) {
      ++i;
    }

    if (i < node->keys.size() && CompareKeys(node->keys[i], key) == 0) {
      return node->values[i];
    }

    if (node->is_leaf) {
      return {};
    }

    return LookupInternal(node->children[i].get(), key);
  }
};

}  // namespace

// =============================================================================
// IndexManager implementation
// =============================================================================

common::Status IndexManager::CreateIndex(const IndexSpec& spec) {
  // Verifica se ja existe indice nesse campo
  for (const auto& idx : indexes_) {
    if (idx->spec().field_name == spec.field_name) {
      return common::Status::Error(common::StatusCode::kAlreadyExists,
                                   "Indice ja existe para campo: " + spec.field_name);
    }
  }

  indexes_.push_back(std::make_unique<BTreeIndexImpl>(spec));
  NEXODB_LOG_INFO("IndexManager", "Indice criado para campo: " + spec.field_name);
  return common::Status::Ok();
}

common::Status IndexManager::DropIndex(const std::string& field_name) {
  auto it = std::find_if(indexes_.begin(), indexes_.end(),
                         [&field_name](const auto& idx) {
                           return idx->spec().field_name == field_name;
                         });

  if (it == indexes_.end()) {
    return common::Status::Error(common::StatusCode::kNotFound,
                                 "Indice nao encontrado: " + field_name);
  }

  indexes_.erase(it);
  NEXODB_LOG_INFO("IndexManager", "Indice removido para campo: " + field_name);
  return common::Status::Ok();
}

BTreeIndex* IndexManager::FindIndexFor(const std::string& field_name) const {
  for (const auto& idx : indexes_) {
    if (idx->spec().field_name == field_name) {
      return idx.get();
    }
  }
  return nullptr;
}

}  // namespace nexodb::index