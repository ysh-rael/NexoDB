// =============================================================================
// query_engine.cpp
// Motor de consultas: parser de filtros estilo MongoDB ($eq, $gt, $lt, $in, etc.)
// e operadores de atualizacao ($set, $unset, $inc). Executa varredura completa
// quando nao ha indice aplicavel.
// =============================================================================
#include "nexodb/query_engine.hpp"

#include "nexodb/document.hpp"
#include "nexodb/logger.hpp"
#include "nexodb/storage_engine.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <memory>
#include <regex>
#include <string>
#include <variant>
#include <vector>

namespace nexodb::query {

// =============================================================================
// Forward declarations for enums and helper functions
// =============================================================================

// Operadores de comparacao
enum class ComparisonOp {
  kEq,      // $eq
  kNe,      // $ne
  kGt,      // $gt
  kGte,     // $gte
  kLt,      // $lt
  kLte,     // $lte
  kIn,      // $in
  kNin,     // $nin
  kExists,  // $exists
  kRegex,   // $regex
};

// Operadores logicos
enum class LogicalOp {
  kAnd,  // $and
  kOr,   // $or
  kNor,  // $nor
  kNot,  // $not
};

namespace {

// Forward declarations
struct FilterExpr;
const document::Value* GetFieldValue(const document::Object& doc, const std::string& field);
int CompareValues(const document::Value& a, const document::Value& b);
bool CompareValues(const document::Value& a, const document::Value& b, ComparisonOp op);
bool EvaluateFilter(const document::Object& doc, const FilterExpr& expr);

// =============================================================================
// Representacao interna de filtros parseados
// =============================================================================

// Expressao de filtro (arvore)
struct FilterExpr {
  virtual ~FilterExpr() = default;
  virtual bool Evaluate(const document::Object& doc) const = 0;
  virtual void CollectFields(std::vector<std::string>& fields) const = 0;
};

// Expressao de comparacao de campo
class FieldComparison : public FilterExpr {
 public:
  FieldComparison(std::string field, ComparisonOp op, document::Value value)
      : field_(std::move(field)), op_(op), value_(std::move(value)) {}

  bool Evaluate(const document::Object& doc) const override {
    const document::Value* value = GetFieldValue(doc, field_);
    if (!value) {
      // Campo nao existe
      if (op_ == ComparisonOp::kExists) {
        const auto* exists = std::get_if<bool>(&value_.storage());
        return exists && *exists == false;  // $exists: false
      }
      return op_ == ComparisonOp::kNe;  // null != value
    }

    // Tratamento especial para $exists
    if (op_ == ComparisonOp::kExists) {
      const auto* exists = std::get_if<bool>(&value_.storage());
      return exists && *exists == true;
    }

    return CompareValues(*value, value_, op_);
  }

  void CollectFields(std::vector<std::string>& fields) const override {
    fields.push_back(field_);
  }

  const std::string& field() const { return field_; }
  ComparisonOp op() const { return op_; }
  const document::Value& value() const { return value_; }

 private:
  std::string field_;
  ComparisonOp op_;
  document::Value value_;
};

// Expressao logica (AND, OR, NOR, NOT)
class LogicalExpr : public FilterExpr {
 public:
  LogicalExpr(LogicalOp op, std::vector<std::unique_ptr<FilterExpr>> children)
      : op_(op), children_(std::move(children)) {}

  bool Evaluate(const document::Object& doc) const override {
    switch (op_) {
      case LogicalOp::kAnd:
        return std::all_of(children_.begin(), children_.end(),
                          [&doc](const auto& child) { return child->Evaluate(doc); });
      case LogicalOp::kOr:
        return std::any_of(children_.begin(), children_.end(),
                          [&doc](const auto& child) { return child->Evaluate(doc); });
      case LogicalOp::kNor:
        return std::none_of(children_.begin(), children_.end(),
                           [&doc](const auto& child) { return child->Evaluate(doc); });
      case LogicalOp::kNot:
        return children_.size() == 1 && !children_[0]->Evaluate(doc);
    }
    return false;
  }

  void CollectFields(std::vector<std::string>& fields) const override {
    for (const auto& child : children_) {
      child->CollectFields(fields);
    }
  }

 private:
  LogicalOp op_;
  std::vector<std::unique_ptr<FilterExpr>> children_;
};

// ParsedFilter: contem a arvore de expressoes parseada
struct ParsedFilter {
  std::unique_ptr<FilterExpr> root;
  std::vector<std::string> fields;  // Campos referenciados no filtro
};

// =============================================================================
// Parser de filtros
// =============================================================================

class FilterParser {
 public:
  static common::Result<ParsedFilter> Parse(const document::Object& filter_obj) {
    FilterParser parser;
    auto expr_result = parser.ParseExpr(filter_obj);
    if (!expr_result.ok()) return expr_result.status();

    ParsedFilter result;
    result.root = std::move(expr_result.value());
    result.root->CollectFields(result.fields);
    return result;
  }

 private:
  common::Result<std::unique_ptr<FilterExpr>> ParseExpr(const document::Object& obj) {
    // Verifica operadores logicos de topo
    if (obj.size() == 1) {
      auto it = obj.begin();
      const std::string& key = it->first;
      const document::Value& val = it->second;

      if (key == "$and" || key == "$or" || key == "$nor") {
        return ParseLogical(key, val);
      }
      if (key == "$not") {
        auto child_result = ParseSingleExpr(val);
        if (!child_result.ok()) return child_result.status();
        std::vector<std::unique_ptr<FilterExpr>> children;
        children.push_back(std::move(child_result.value()));
        return common::Result<std::unique_ptr<FilterExpr>>(
            std::make_unique<LogicalExpr>(LogicalOp::kNot, std::move(children)));
      }
    }

    // Filtro simples: { field: value } ou { field: { $op: value } }
    std::vector<std::unique_ptr<FilterExpr>> and_children;
    for (const auto& [field, value] : obj) {
      auto expr_result = ParseFieldExpr(field, value);
      if (!expr_result.ok()) return expr_result.status();
      and_children.push_back(std::move(expr_result.value()));
    }

    if (and_children.size() == 1) {
      return common::Result<std::unique_ptr<FilterExpr>>(std::move(and_children[0]));
    }
    return common::Result<std::unique_ptr<FilterExpr>>(
        std::make_unique<LogicalExpr>(LogicalOp::kAnd, std::move(and_children)));
  }

  common::Result<std::unique_ptr<FilterExpr>> ParseLogical(const std::string& op, const document::Value& val) {
    const auto* arr = std::get_if<document::Array>(&val.storage());
    if (!arr) {
      return common::Status::Error(common::StatusCode::kInvalidArgument,
                                   op + " requer um array");
    }

    LogicalOp logical_op;
    if (op == "$and") logical_op = LogicalOp::kAnd;
    else if (op == "$or") logical_op = LogicalOp::kOr;
    else logical_op = LogicalOp::kNor;

    std::vector<std::unique_ptr<FilterExpr>> children;
    for (const auto& item : *arr) {
      const auto* child_obj = std::get_if<document::Object>(&item.storage());
      if (!child_obj) {
        return common::Status::Error(common::StatusCode::kInvalidArgument,
                                     op + " itens devem ser objetos");
      }
      auto child_result = ParseExpr(*child_obj);
      if (!child_result.ok()) return child_result.status();
      children.push_back(std::move(child_result.value()));
    }

    return common::Result<std::unique_ptr<FilterExpr>>(
        std::make_unique<LogicalExpr>(logical_op, std::move(children)));
  }

  common::Result<std::unique_ptr<FilterExpr>> ParseSingleExpr(const document::Value& val) {
    const auto* obj = std::get_if<document::Object>(&val.storage());
    if (!obj || obj->size() != 1) {
      // Valor direto = $eq implicito (mas sem campo - invalido)
      return common::Status::Error(common::StatusCode::kInvalidArgument,
                                   "Expressao invalida");
    }
    return ParseFieldExpr("", val);
  }

  common::Result<std::unique_ptr<FilterExpr>> ParseFieldExpr(const std::string& field,
                                                             const document::Value& value) {
    const auto* obj = std::get_if<document::Object>(&value.storage());
    if (!obj) {
      // { field: value } => { field: { $eq: value } }
      return common::Result<std::unique_ptr<FilterExpr>>(
          std::make_unique<FieldComparison>(field, ComparisonOp::kEq, value));
    }

    if (obj->size() == 1) {
      auto it = obj->begin();
      const std::string& op = it->first;
      const document::Value& op_val = it->second;

      ComparisonOp cmp_op;
      if (op == "$eq") cmp_op = ComparisonOp::kEq;
      else if (op == "$ne") cmp_op = ComparisonOp::kNe;
      else if (op == "$gt") cmp_op = ComparisonOp::kGt;
      else if (op == "$gte") cmp_op = ComparisonOp::kGte;
      else if (op == "$lt") cmp_op = ComparisonOp::kLt;
      else if (op == "$lte") cmp_op = ComparisonOp::kLte;
      else if (op == "$in") cmp_op = ComparisonOp::kIn;
      else if (op == "$nin") cmp_op = ComparisonOp::kNin;
      else if (op == "$exists") cmp_op = ComparisonOp::kExists;
      else if (op == "$regex") cmp_op = ComparisonOp::kRegex;
      else {
        return common::Status::Error(common::StatusCode::kInvalidArgument,
                                     "Operador desconhecido: " + op);
      }

      return common::Result<std::unique_ptr<FilterExpr>>(
          std::make_unique<FieldComparison>(field, cmp_op, op_val));
    }

    // Multiplos operadores no mesmo campo: { field: { $gt: 10, $lt: 20 } }
    std::vector<std::unique_ptr<FilterExpr>> and_children;
    for (const auto& [op, op_val] : *obj) {
      ComparisonOp cmp_op;
      if (op == "$eq") cmp_op = ComparisonOp::kEq;
      else if (op == "$ne") cmp_op = ComparisonOp::kNe;
      else if (op == "$gt") cmp_op = ComparisonOp::kGt;
      else if (op == "$gte") cmp_op = ComparisonOp::kGte;
      else if (op == "$lt") cmp_op = ComparisonOp::kLt;
      else if (op == "$lte") cmp_op = ComparisonOp::kLte;
      else if (op == "$in") cmp_op = ComparisonOp::kIn;
      else if (op == "$nin") cmp_op = ComparisonOp::kNin;
      else if (op == "$exists") cmp_op = ComparisonOp::kExists;
      else if (op == "$regex") cmp_op = ComparisonOp::kRegex;
      else {
        return common::Status::Error(common::StatusCode::kInvalidArgument,
                                     "Operador desconhecido: " + op);
      }
      and_children.push_back(
          std::make_unique<FieldComparison>(field, cmp_op, op_val));
    }

    return common::Result<std::unique_ptr<FilterExpr>>(
        std::make_unique<LogicalExpr>(LogicalOp::kAnd, std::move(and_children)));
  }
};

// =============================================================================
// Avaliacao de filtro em documento
// =============================================================================

// =============================================================================
// Parser de UpdateSpec ($set, $unset, $inc, etc.)
// =============================================================================

struct ParsedUpdate {
  document::Object set_fields;      // $set
  std::vector<std::string> unset_fields;  // $unset
  std::map<std::string, int64_t> inc_fields;  // $inc
};

common::Result<ParsedUpdate> ParseUpdate(const document::Object& update_obj) {
  ParsedUpdate result;

  for (const auto& [op, val] : update_obj) {
    const auto* obj = std::get_if<document::Object>(&val.storage());
    if (!obj) {
      return common::Status::Error(common::StatusCode::kInvalidArgument,
                                   op + " requer um objeto");
    }

    if (op == "$set") {
      for (const auto& [field, value] : *obj) {
        result.set_fields[field] = value;
      }
    } else if (op == "$unset") {
      for (const auto& [field, value] : *obj) {
        const auto* v = std::get_if<bool>(&value.storage());
        if (v && *v) {
          result.unset_fields.push_back(field);
        }
      }
    } else if (op == "$inc") {
      for (const auto& [field, value] : *obj) {
        int64_t inc_val = 0;
        if (std::holds_alternative<int64_t>(value.storage())) {
          inc_val = std::get<int64_t>(value.storage());
        } else if (std::holds_alternative<double>(value.storage())) {
          inc_val = static_cast<int64_t>(std::get<double>(value.storage()));
        } else {
          return common::Status::Error(common::StatusCode::kInvalidArgument,
                                       "$inc requer valor numerico");
        }
        result.inc_fields[field] = inc_val;
      }
    }
  }

  return result;
}

// Aplica update a um documento
void ApplyUpdate(document::Object& doc, const ParsedUpdate& update) {
  // $unset primeiro
  for (const auto& field : update.unset_fields) {
    doc.erase(field);
  }

  // $set
  for (const auto& [field, value] : update.set_fields) {
    doc[field] = value;
  }

  // $inc
  for (const auto& [field, inc_val] : update.inc_fields) {
    auto it = doc.find(field);
    if (it != doc.end()) {
      if (std::holds_alternative<int64_t>(it->second.storage())) {
        it->second = document::Value(std::get<int64_t>(it->second.storage()) + inc_val);
      } else if (std::holds_alternative<double>(it->second.storage())) {
        it->second = document::Value(std::get<double>(it->second.storage()) + inc_val);
      } else {
        it->second = document::Value(inc_val);
      }
    } else {
      doc[field] = document::Value(inc_val);
    }
  }
}

// =============================================================================
// Helper function implementations
// =============================================================================

const document::Value* GetFieldValue(const document::Object& doc, const std::string& field) {
  const document::Value* current = nullptr;
  const document::Object* current_obj = &doc;

  size_t start = 0;
  while (start < field.size()) {
    size_t dot = field.find('.', start);
    std::string part = field.substr(start, dot - start);

    auto it = current_obj->find(part);
    if (it == current_obj->end()) {
      return nullptr;
    }
    current = &it->second;

    if (dot == std::string::npos) break;

    const auto* next_obj = std::get_if<document::Object>(&current->storage());
    if (!next_obj) return nullptr;
    current_obj = next_obj;
    start = dot + 1;
  }

  return current;
}

int CompareValues(const document::Value& a, const document::Value& b) {
  const auto& sa = a.storage();
  const auto& sb = b.storage();

  if (std::holds_alternative<std::nullptr_t>(sa) &&
      std::holds_alternative<std::nullptr_t>(sb)) return 0;
  if (std::holds_alternative<std::nullptr_t>(sa)) return -1;
  if (std::holds_alternative<std::nullptr_t>(sb)) return 1;

  if (std::holds_alternative<bool>(sa) && std::holds_alternative<bool>(sb)) {
    bool va = std::get<bool>(sa);
    bool vb = std::get<bool>(sb);
    return va == vb ? 0 : (va ? 1 : -1);
  }

  if ((std::holds_alternative<int64_t>(sa) || std::holds_alternative<double>(sa)) &&
      (std::holds_alternative<int64_t>(sb) || std::holds_alternative<double>(sb))) {
    double va = std::holds_alternative<int64_t>(sa) ? std::get<int64_t>(sa) : std::get<double>(sa);
    double vb = std::holds_alternative<int64_t>(sb) ? std::get<int64_t>(sb) : std::get<double>(sb);
    if (va < vb) return -1;
    if (va > vb) return 1;
    return 0;
  }

  if (std::holds_alternative<std::string>(sa) && std::holds_alternative<std::string>(sb)) {
    return std::get<std::string>(sa).compare(std::get<std::string>(sb));
  }

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

bool CompareValues(const document::Value& a, const document::Value& b, ComparisonOp op) {
  int cmp = CompareValues(a, b);
  switch (op) {
    case ComparisonOp::kEq: return cmp == 0;
    case ComparisonOp::kNe: return cmp != 0;
    case ComparisonOp::kGt: return cmp > 0;
    case ComparisonOp::kGte: return cmp >= 0;
    case ComparisonOp::kLt: return cmp < 0;
    case ComparisonOp::kLte: return cmp <= 0;
    case ComparisonOp::kIn: {
      const auto* arr = std::get_if<document::Array>(&b.storage());
      if (!arr) return false;
      return std::any_of(arr->begin(), arr->end(),
                        [&a](const document::Value& v) { return CompareValues(a, v) == 0; });
    }
    case ComparisonOp::kNin: {
      const auto* arr = std::get_if<document::Array>(&b.storage());
      if (!arr) return true;
      return std::none_of(arr->begin(), arr->end(),
                         [&a](const document::Value& v) { return CompareValues(a, v) == 0; });
    }
    case ComparisonOp::kExists: return true;
    case ComparisonOp::kRegex: {
      const auto* pattern = std::get_if<std::string>(&b.storage());
      const auto* str = std::get_if<std::string>(&a.storage());
      if (!pattern || !str) return false;
      try {
        std::regex re(*pattern);
        return std::regex_search(*str, re);
      } catch (...) {
        return false;
      }
    }
  }
  return false;
}

bool EvaluateFilter(const document::Object& doc, const FilterExpr& expr) {
  return expr.Evaluate(doc);
}

}  // namespace

// =============================================================================
// QueryEngine implementation
// =============================================================================

common::Result<std::vector<document::Document>> QueryEngine::Find(
    const Filter& filter, const QueryOptions& options) const {

  // Parse do filtro
  ParsedFilter parsed;
  if (!filter.raw.empty()) {
    auto parse_result = FilterParser::Parse(filter.raw);
    if (!parse_result.ok()) return parse_result.status();
    parsed = std::move(parse_result.value());
  }

  std::vector<document::Document> results;

  // Usa iterador para varredura completa
  auto iterator = storage_.NewIterator();
  storage::RecordLocation loc;
  document::Document doc;

  while (iterator->Next(loc, doc)) {
    // Avalia filtro
    bool matches = true;
    if (parsed.root) {
      matches = EvaluateFilter(doc.fields(), *parsed.root);
    }

    if (matches) {
      // Aplica projecao se especificada
      if (options.projection) {
        document::Object projected;
        for (const auto& [key, _] : *options.projection) {
          auto it = doc.fields().find(key);
          if (it != doc.fields().end()) {
            projected[key] = it->second;
          }
        }
        // Mantem _id sempre
        auto id_it = doc.fields().find("_id");
        if (id_it != doc.fields().end()) {
          projected["_id"] = id_it->second;
        }
        results.emplace_back(std::move(projected));
      } else {
        results.push_back(std::move(doc));
      }

      // Verifica limit
      if (options.limit > 0 && results.size() >= static_cast<size_t>(options.limit)) {
        break;
      }
    }
  }

  // Aplica skip (depois do filtro para simplicidade)
  if (options.skip > 0 && static_cast<size_t>(options.skip) < results.size()) {
    results.erase(results.begin(), results.begin() + options.skip);
  }

  return results;
}

common::Result<std::optional<document::Document>> QueryEngine::FindOne(
    const Filter& filter, const QueryOptions& options) const {
  auto find_result = Find(filter, options);
  if (!find_result.ok()) return find_result.status();

  const auto& results = find_result.value();
  if (results.empty()) return std::optional<document::Document>{};
  return std::optional<document::Document>(results[0]);
}

common::Result<std::optional<document::Document>> QueryEngine::FindById(
    const document::ObjectId& oid) const {
  // Usa o indice primario do storage engine para busca O(1)
  return storage_.FindById(oid);
}

common::Result<storage::RecordLocation> QueryEngine::InsertOne(const document::Document& doc) {
  return storage_.Insert(doc);
}

common::Result<int64_t> QueryEngine::Count(const Filter& filter) const {
  // Se filtro vazio, usa contador O(1) do storage engine
  if (filter.raw.empty()) {
    auto count_result = storage_.Count();
    if (!count_result.ok()) return count_result.status();
    return static_cast<int64_t>(count_result.value());
  }

  // Filtro nao vazio: precisa varrer (futuro: usar indices)
  auto find_result = Find(filter, QueryOptions{});
  if (!find_result.ok()) return find_result.status();
  return static_cast<int64_t>(find_result.value().size());
}

common::Result<int64_t> QueryEngine::UpdateOne(const Filter& filter,
                                               const UpdateSpec& update) {
  auto find_result = Find(filter, QueryOptions{.limit = 1});
  if (!find_result.ok()) return find_result.status();

  auto& results = find_result.value();
  if (results.empty()) return 0;

  // Parse update
  auto update_result = ParseUpdate(update.raw);
  if (!update_result.ok()) return update_result.status();

  ApplyUpdate(results[0].fields(), update_result.value());

  // TODO: Salvar de volta no storage (precisa da localizacao)
  return 1;
}

common::Result<int64_t> QueryEngine::UpdateMany(const Filter& filter,
                                                const UpdateSpec& update) {
  auto find_result = Find(filter, QueryOptions{});
  if (!find_result.ok()) return find_result.status();

  auto& results = find_result.value();
  if (results.empty()) return 0;

  auto update_result = ParseUpdate(update.raw);
  if (!update_result.ok()) return update_result.status();

  int64_t count = 0;
  for (auto& doc : results) {
    ApplyUpdate(doc.fields(), update_result.value());
    count++;
    // TODO: Salvar cada documento atualizado
  }

  return count;
}

common::Result<int64_t> QueryEngine::DeleteOne(const Filter& filter) {
  auto find_result = Find(filter, QueryOptions{.limit = 1});
  if (!find_result.ok()) return find_result.status();

  auto& results = find_result.value();
  if (results.empty()) return 0;

  // Usa iterador para obter localizacao e deletar
  auto iterator = storage_.NewIterator();
  storage::RecordLocation loc;
  document::Document doc;

  ParsedFilter parsed;
  if (!filter.raw.empty()) {
    auto parse_result = FilterParser::Parse(filter.raw);
    if (!parse_result.ok()) return parse_result.status();
    parsed = std::move(parse_result.value());
  }

  while (iterator->Next(loc, doc)) {
    bool matches = true;
    if (parsed.root) {
      matches = EvaluateFilter(doc.fields(), *parsed.root);
    }
    if (matches) {
      auto remove_status = storage_.Remove(loc);
      if (!remove_status.ok()) return common::Result<int64_t>(remove_status);
      return 1;
    }
  }
  return 0;
}

common::Result<int64_t> QueryEngine::DeleteMany(const Filter& filter) {
  auto iterator = storage_.NewIterator();
  storage::RecordLocation loc;
  document::Document doc;

  ParsedFilter parsed;
  if (!filter.raw.empty()) {
    auto parse_result = FilterParser::Parse(filter.raw);
    if (!parse_result.ok()) return parse_result.status();
    parsed = std::move(parse_result.value());
  }

  int64_t count = 0;
  std::vector<storage::RecordLocation> to_delete;

  while (iterator->Next(loc, doc)) {
    bool matches = true;
    if (parsed.root) {
      matches = EvaluateFilter(doc.fields(), *parsed.root);
    }
    if (matches) {
      to_delete.push_back(loc);
    }
  }

  for (const auto& del_loc : to_delete) {
    auto remove_status = storage_.Remove(del_loc);
    if (!remove_status.ok()) return common::Result<int64_t>(remove_status);
    count++;
  }

  return count;
}

}  // namespace nexodb::query