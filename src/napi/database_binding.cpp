// =============================================================================
// database_binding.cpp
// Implementacao do wrapper `Database`. Cada metodo publico corresponde a um
// metodo do Mongoose listado no README. Delegao para o core (QueryEngine)
// e propaga erros como excecoes JS via Napi::Error, no padrao esperado pelo
// wrapper lib/Model.js (promises).
// =============================================================================
#include "database_binding.hpp"

#include "nexodb/document.hpp"
#include "nexodb/logger.hpp"
#include "nexodb/query_engine.hpp"
#include "nexodb/storage_engine.hpp"

#include <napi.h>

namespace nexodb::napi {

namespace {

// Converte document::Value para Napi::Value
Napi::Value ValueToNapi(Napi::Env env, const document::Value& value) {
  const auto& storage = value.storage();

  if (std::holds_alternative<std::nullptr_t>(storage)) {
    return env.Null();
  }

  if (std::holds_alternative<bool>(storage)) {
    return Napi::Boolean::New(env, std::get<bool>(storage));
  }

  if (std::holds_alternative<int64_t>(storage)) {
    return Napi::Number::New(env, static_cast<double>(std::get<int64_t>(storage)));
  }

  if (std::holds_alternative<double>(storage)) {
    return Napi::Number::New(env, std::get<double>(storage));
  }

  if (std::holds_alternative<std::string>(storage)) {
    return Napi::String::New(env, std::get<std::string>(storage));
  }

  if (std::holds_alternative<document::ObjectId>(storage)) {
    const auto& oid = std::get<document::ObjectId>(storage);
    return Napi::String::New(env, oid.ToHexString());
  }

  if (std::holds_alternative<document::Array>(storage)) {
    const auto& arr = std::get<document::Array>(storage);
    Napi::Array napi_arr = Napi::Array::New(env, arr.size());
    for (size_t i = 0; i < arr.size(); ++i) {
      napi_arr.Set(i, ValueToNapi(env, arr[i]));
    }
    return napi_arr;
  }

  if (std::holds_alternative<document::Object>(storage)) {
    const auto& obj = std::get<document::Object>(storage);
    Napi::Object napi_obj = Napi::Object::New(env);
    for (const auto& [key, val] : obj) {
      napi_obj.Set(key, ValueToNapi(env, val));
    }
    return napi_obj;
  }

  return env.Null();
}

// Converte document::Document para Napi::Object
Napi::Object DocumentToNapi(Napi::Env env, const document::Document& doc) {
  Napi::Object obj = Napi::Object::New(env);
  for (const auto& [key, val] : doc.fields()) {
    obj.Set(key, ValueToNapi(env, val));
  }
  return obj;
}

// Converte Napi::Value para document::Value
common::Result<document::Value> NapiToValue(Napi::Env env, const Napi::Value& value) {
  if (value.IsNull() || value.IsUndefined()) {
    return document::Value(nullptr);
  }

  if (value.IsBoolean()) {
    return document::Value(value.As<Napi::Boolean>().Value());
  }

  if (value.IsNumber()) {
    double num = value.As<Napi::Number>().DoubleValue();
    if (num == std::floor(num) && num >= INT64_MIN && num <= INT64_MAX) {
      return document::Value(static_cast<int64_t>(num));
    }
    return document::Value(num);
  }

  if (value.IsString()) {
    return document::Value(value.As<Napi::String>().Utf8Value());
  }

  if (value.IsArray()) {
    Napi::Array arr = value.As<Napi::Array>();
    document::Array result;
    result.reserve(arr.Length());
    for (uint32_t i = 0; i < arr.Length(); ++i) {
      auto elem_result = NapiToValue(env, arr.Get(i));
      if (!elem_result.ok()) return elem_result.status();
      result.push_back(elem_result.value());
    }
    return document::Value(std::move(result));
  }

  if (value.IsObject()) {
    Napi::Object obj = value.As<Napi::Object>();
    document::Object result;
    auto keys = obj.GetPropertyNames();
    for (uint32_t i = 0; i < keys.Length(); ++i) {
      Napi::Value key_val = keys.Get(i);
      std::string key = key_val.As<Napi::String>().Utf8Value();
      Napi::Value val = obj.Get(key_val);
      auto val_result = NapiToValue(env, val);
      if (!val_result.ok()) return val_result.status();
      result.emplace(std::move(key), val_result.value());
    }
    return document::Value(std::move(result));
  }

  return common::Status::Error(common::StatusCode::kInvalidArgument, "Tipo JS nao suportado");
}

// Converte Napi::Object para document::Object (filtro/update)
common::Result<document::Object> NapiToObject(Napi::Env env, const Napi::Value& value) {
  if (!value.IsObject()) {
    return common::Status::Error(common::StatusCode::kInvalidArgument, "Esperado objeto");
  }
  Napi::Object obj = value.As<Napi::Object>();
  document::Object result;
  auto keys = obj.GetPropertyNames();
  for (uint32_t i = 0; i < keys.Length(); ++i) {
    Napi::Value key_val = keys.Get(i);
    std::string key = key_val.As<Napi::String>().Utf8Value();
    Napi::Value val = obj.Get(key_val);
    auto val_result = NapiToValue(env, val);
    if (!val_result.ok()) return val_result.status();
    result.emplace(std::move(key), val_result.value());
  }
  return result;
}

// Converte QueryOptions do JS
query::QueryOptions ParseQueryOptions(Napi::Env env, const Napi::Value& value) {
  query::QueryOptions options;
  if (!value.IsObject()) return options;

  Napi::Object obj = value.As<Napi::Object>();

  if (obj.Has("sort")) {
    auto sort_result = NapiToObject(env, obj.Get("sort"));
    if (sort_result.ok()) options.sort = std::move(sort_result.value());
  }
  if (obj.Has("projection") || obj.Has("select")) {
    auto proj_result = NapiToObject(env, obj.Get("projection"));
    if (!proj_result.ok()) proj_result = NapiToObject(env, obj.Get("select"));
    if (proj_result.ok()) options.projection = std::move(proj_result.value());
  }
  if (obj.Has("skip")) {
    options.skip = obj.Get("skip").As<Napi::Number>().Int64Value();
  }
  if (obj.Has("limit")) {
    options.limit = obj.Get("limit").As<Napi::Number>().Int64Value();
  }

  return options;
}

// Helper: Converte vetor de Document para Napi::Array
Napi::Array DocumentsToArray(Napi::Env env, const std::vector<document::Document>& docs) {
  Napi::Array arr = Napi::Array::New(env, docs.size());
  for (size_t i = 0; i < docs.size(); ++i) {
    arr.Set(i, DocumentToNapi(env, docs[i]));
  }
  return arr;
}

// Helper: Cria Promise resolvida com array de documentos
Napi::Value ResolveDocs(Napi::Env env, const std::vector<document::Document>& docs) {
  return DocumentsToArray(env, docs);
}

// Helper: Cria Promise resolvida com documento unico ou null
Napi::Value ResolveDoc(Napi::Env env, const std::optional<document::Document>& doc) {
  if (doc) {
    return DocumentToNapi(env, *doc);
  }
  return env.Null();
}

// Helper: Cria Promise resolvida com count
Napi::Value ResolveCount(Napi::Env env, int64_t count) {
  return Napi::Number::New(env, static_cast<double>(count));
}

// =============================================================================
// AsyncWorker classes (all at namespace level to avoid inline class issues)
// =============================================================================

class FindWorker : public Napi::AsyncWorker {
 public:
  FindWorker(Napi::Env env, query::QueryEngine* engine, query::Filter filter,
             query::QueryOptions options, Napi::Promise::Deferred deferred)
      : Napi::AsyncWorker(env), engine_(engine), filter_(std::move(filter)),
        options_(std::move(options)), deferred_(std::move(deferred)) {}

  void Execute() override {
    result_ = engine_->Find(filter_, options_);
  }

  void OnOK() override {
    if (result_.ok()) {
      deferred_.Resolve(DocumentsToArray(Env(), result_.value()));
    } else {
      deferred_.Reject(Napi::Error::New(Env(), result_.status().message()).Value());
    }
  }

  void OnError(const Napi::Error& e) override {
    deferred_.Reject(e.Value());
  }

 private:
  query::QueryEngine* engine_;
  query::Filter filter_;
  query::QueryOptions options_;
  Napi::Promise::Deferred deferred_;
  common::Result<std::vector<document::Document>> result_;
};

class FindOneWorker : public Napi::AsyncWorker {
 public:
  FindOneWorker(Napi::Env env, query::QueryEngine* engine, query::Filter filter,
                query::QueryOptions options, Napi::Promise::Deferred deferred)
      : Napi::AsyncWorker(env), engine_(engine), filter_(std::move(filter)),
        options_(std::move(options)), deferred_(std::move(deferred)) {}

  void Execute() override {
    result_ = engine_->FindOne(filter_, options_);
  }

  void OnOK() override {
    if (result_.ok()) {
      deferred_.Resolve(ResolveDoc(Env(), result_.value()));
    } else {
      deferred_.Reject(Napi::Error::New(Env(), result_.status().message()).Value());
    }
  }

  void OnError(const Napi::Error& e) override {
    deferred_.Reject(e.Value());
  }

 private:
  query::QueryEngine* engine_;
  query::Filter filter_;
  query::QueryOptions options_;
  Napi::Promise::Deferred deferred_;
  common::Result<std::optional<document::Document>> result_;
};

class FindByIdWorker : public Napi::AsyncWorker {
 public:
  FindByIdWorker(Napi::Env env, query::QueryEngine* engine, document::ObjectId oid,
                 query::QueryOptions options, Napi::Promise::Deferred deferred)
      : Napi::AsyncWorker(env), engine_(engine), oid_(std::move(oid)),
        options_(std::move(options)), deferred_(std::move(deferred)) {}

  void Execute() override {
    result_ = engine_->FindById(oid_);
  }

  void OnOK() override {
    if (result_.ok()) {
      deferred_.Resolve(ResolveDoc(Env(), result_.value()));
    } else {
      deferred_.Reject(Napi::Error::New(Env(), result_.status().message()).Value());
    }
  }

  void OnError(const Napi::Error& e) override {
    deferred_.Reject(e.Value());
  }

 private:
  query::QueryEngine* engine_;
  document::ObjectId oid_;
  query::QueryOptions options_;
  Napi::Promise::Deferred deferred_;
  common::Result<std::optional<document::Document>> result_;
};

class CountWorker : public Napi::AsyncWorker {
 public:
  CountWorker(Napi::Env env, query::QueryEngine* engine, query::Filter filter,
              Napi::Promise::Deferred deferred)
      : Napi::AsyncWorker(env), engine_(engine), filter_(std::move(filter)),
        deferred_(std::move(deferred)) {}

  void Execute() override {
    NEXODB_LOG_DEBUG("CountWorker", "Execute iniciado");
    result_ = engine_->Count(filter_);
    NEXODB_LOG_DEBUG("CountWorker", "Execute concluido, ok=" + std::to_string(result_.ok()));
  }

  void OnOK() override {
    if (result_.ok()) {
      deferred_.Resolve(ResolveCount(Env(), result_.value()));
    } else {
      deferred_.Reject(Napi::Error::New(Env(), result_.status().message()).Value());
    }
  }

  void OnError(const Napi::Error& e) override {
    deferred_.Reject(e.Value());
  }

 private:
  query::QueryEngine* engine_;
  query::Filter filter_;
  Napi::Promise::Deferred deferred_;
  common::Result<int64_t> result_;
};

class UpdateWorker : public Napi::AsyncWorker {
 public:
  UpdateWorker(Napi::Env env, query::QueryEngine* engine, query::Filter filter,
               query::UpdateSpec update, Napi::Promise::Deferred deferred)
      : Napi::AsyncWorker(env), engine_(engine), filter_(std::move(filter)),
        update_(std::move(update)), deferred_(std::move(deferred)) {}

  void Execute() override {
    result_ = engine_->UpdateOne(filter_, update_);
  }

  void OnOK() override {
    if (result_.ok()) {
      deferred_.Resolve(ResolveCount(Env(), result_.value()));
    } else {
      deferred_.Reject(Napi::Error::New(Env(), result_.status().message()).Value());
    }
  }

  void OnError(const Napi::Error& e) override {
    deferred_.Reject(e.Value());
  }

 private:
  query::QueryEngine* engine_;
  query::Filter filter_;
  query::UpdateSpec update_;
  Napi::Promise::Deferred deferred_;
  common::Result<int64_t> result_;
};

class UpdateManyWorker : public Napi::AsyncWorker {
 public:
  UpdateManyWorker(Napi::Env env, query::QueryEngine* engine, query::Filter filter,
                   query::UpdateSpec update, Napi::Promise::Deferred deferred)
      : Napi::AsyncWorker(env), engine_(engine), filter_(std::move(filter)),
        update_(std::move(update)), deferred_(std::move(deferred)) {}

  void Execute() override {
    result_ = engine_->UpdateMany(filter_, update_);
  }

  void OnOK() override {
    if (result_.ok()) {
      deferred_.Resolve(ResolveCount(Env(), result_.value()));
    } else {
      deferred_.Reject(Napi::Error::New(Env(), result_.status().message()).Value());
    }
  }

  void OnError(const Napi::Error& e) override {
    deferred_.Reject(e.Value());
  }

 private:
  query::QueryEngine* engine_;
  query::Filter filter_;
  query::UpdateSpec update_;
  Napi::Promise::Deferred deferred_;
  common::Result<int64_t> result_;
};

class ReplaceWorker : public Napi::AsyncWorker {
 public:
  ReplaceWorker(Napi::Env env, query::QueryEngine* engine, query::Filter filter,
                query::UpdateSpec update, Napi::Promise::Deferred deferred)
      : Napi::AsyncWorker(env), engine_(engine), filter_(std::move(filter)),
        update_(std::move(update)), deferred_(std::move(deferred)) {}

  void Execute() override {
    result_ = engine_->UpdateOne(filter_, update_);
  }

  void OnOK() override {
    if (result_.ok()) {
      deferred_.Resolve(ResolveCount(Env(), result_.value()));
    } else {
      deferred_.Reject(Napi::Error::New(Env(), result_.status().message()).Value());
    }
  }

  void OnError(const Napi::Error& e) override {
    deferred_.Reject(e.Value());
  }

 private:
  query::QueryEngine* engine_;
  query::Filter filter_;
  query::UpdateSpec update_;
  Napi::Promise::Deferred deferred_;
  common::Result<int64_t> result_;
};

class DeleteWorker : public Napi::AsyncWorker {
 public:
  DeleteWorker(Napi::Env env, query::QueryEngine* engine, query::Filter filter,
               Napi::Promise::Deferred deferred)
      : Napi::AsyncWorker(env), engine_(engine), filter_(std::move(filter)),
        deferred_(std::move(deferred)) {}

  void Execute() override {
    result_ = engine_->DeleteOne(filter_);
  }

  void OnOK() override {
    if (result_.ok()) {
      deferred_.Resolve(ResolveCount(Env(), result_.value()));
    } else {
      deferred_.Reject(Napi::Error::New(Env(), result_.status().message()).Value());
    }
  }

  void OnError(const Napi::Error& e) override {
    deferred_.Reject(e.Value());
  }

 private:
  query::QueryEngine* engine_;
  query::Filter filter_;
  Napi::Promise::Deferred deferred_;
  common::Result<int64_t> result_;
};

class DeleteManyWorker : public Napi::AsyncWorker {
 public:
  DeleteManyWorker(Napi::Env env, query::QueryEngine* engine, query::Filter filter,
                   Napi::Promise::Deferred deferred)
      : Napi::AsyncWorker(env), engine_(engine), filter_(std::move(filter)),
        deferred_(std::move(deferred)) {}

  void Execute() override {
    result_ = engine_->DeleteMany(filter_);
  }

  void OnOK() override {
    if (result_.ok()) {
      deferred_.Resolve(ResolveCount(Env(), result_.value()));
    } else {
      deferred_.Reject(Napi::Error::New(Env(), result_.status().message()).Value());
    }
  }

  void OnError(const Napi::Error& e) override {
    deferred_.Reject(e.Value());
  }

 private:
  query::QueryEngine* engine_;
  query::Filter filter_;
  Napi::Promise::Deferred deferred_;
  common::Result<int64_t> result_;
};

class FindOneAndDeleteWorker : public Napi::AsyncWorker {
 public:
  FindOneAndDeleteWorker(Napi::Env env, query::QueryEngine* engine, query::Filter filter,
                         query::QueryOptions options, Napi::Promise::Deferred deferred)
      : Napi::AsyncWorker(env), engine_(engine), filter_(std::move(filter)),
        options_(std::move(options)), deferred_(std::move(deferred)) {}

  void Execute() override {
    find_result_ = engine_->FindOne(filter_, options_);
    if (find_result_.ok() && find_result_.value()) {
      // TODO: Deletar o documento encontrado
      // Precisa da localizacao do registro
    }
  }

  void OnOK() override {
    if (find_result_.ok()) {
      deferred_.Resolve(ResolveDoc(Env(), find_result_.value()));
    } else {
      deferred_.Reject(Napi::Error::New(Env(), find_result_.status().message()).Value());
    }
  }

  void OnError(const Napi::Error& e) override {
    deferred_.Reject(e.Value());
  }

 private:
  query::QueryEngine* engine_;
  query::Filter filter_;
  query::QueryOptions options_;
  Napi::Promise::Deferred deferred_;
  common::Result<std::optional<document::Document>> find_result_;
};

class FindOneAndReplaceWorker : public Napi::AsyncWorker {
 public:
  FindOneAndReplaceWorker(Napi::Env env, query::QueryEngine* engine, query::Filter filter,
                          query::UpdateSpec update, query::QueryOptions options,
                          Napi::Promise::Deferred deferred)
      : Napi::AsyncWorker(env), engine_(engine), filter_(std::move(filter)),
        update_(std::move(update)), options_(std::move(options)),
        deferred_(std::move(deferred)) {}

  void Execute() override {
    find_result_ = engine_->FindOne(filter_, options_);
    if (find_result_.ok() && find_result_.value()) {
      update_result_ = engine_->UpdateOne(filter_, update_);
    }
  }

  void OnOK() override {
    if (find_result_.ok()) {
      deferred_.Resolve(ResolveDoc(Env(), find_result_.value()));
    } else {
      deferred_.Reject(Napi::Error::New(Env(), find_result_.status().message()).Value());
    }
  }

  void OnError(const Napi::Error& e) override {
    deferred_.Reject(e.Value());
  }

 private:
  query::QueryEngine* engine_;
  query::Filter filter_;
  query::UpdateSpec update_;
  query::QueryOptions options_;
  Napi::Promise::Deferred deferred_;
  common::Result<std::optional<document::Document>> find_result_;
  common::Result<int64_t> update_result_;
};

class FindOneAndUpdateWorker : public Napi::AsyncWorker {
 public:
  FindOneAndUpdateWorker(Napi::Env env, query::QueryEngine* engine, query::Filter filter,
                         query::UpdateSpec update, query::QueryOptions options,
                         Napi::Promise::Deferred deferred)
      : Napi::AsyncWorker(env), engine_(engine), filter_(std::move(filter)),
        update_(std::move(update)), options_(std::move(options)),
        deferred_(std::move(deferred)) {}

  void Execute() override {
    find_result_ = engine_->FindOne(filter_, options_);
    if (find_result_.ok() && find_result_.value()) {
      update_result_ = engine_->UpdateOne(filter_, update_);
    }
  }

  void OnOK() override {
    if (find_result_.ok()) {
      deferred_.Resolve(ResolveDoc(Env(), find_result_.value()));
    } else {
      deferred_.Reject(Napi::Error::New(Env(), find_result_.status().message()).Value());
    }
  }

  void OnError(const Napi::Error& e) override {
    deferred_.Reject(e.Value());
  }

 private:
  query::QueryEngine* engine_;
  query::Filter filter_;
  query::UpdateSpec update_;
  query::QueryOptions options_;
  Napi::Promise::Deferred deferred_;
  common::Result<std::optional<document::Document>> find_result_;
  common::Result<int64_t> update_result_;
};

}  // namespace

// =============================================================================
// DatabaseBinding implementation
// =============================================================================

Napi::Object DatabaseBinding::Init(Napi::Env env, Napi::Object exports) {
  Napi::Function func = DefineClass(
      env, "Database",
      {
          InstanceMethod("find", &DatabaseBinding::Find),
          InstanceMethod("findOne", &DatabaseBinding::FindOne),
          InstanceMethod("findById", &DatabaseBinding::FindById),
          InstanceMethod("findOneAndUpdate", &DatabaseBinding::FindOneAndUpdate),
          InstanceMethod("findByIdAndUpdate", &DatabaseBinding::FindByIdAndUpdate),
          InstanceMethod("count", &DatabaseBinding::Count),
          InstanceMethod("countDocuments", &DatabaseBinding::CountDocuments),
          InstanceMethod("insertOne", &DatabaseBinding::InsertOne),
          InstanceMethod("updateOne", &DatabaseBinding::UpdateOne),
          InstanceMethod("updateMany", &DatabaseBinding::UpdateMany),
          InstanceMethod("replaceOne", &DatabaseBinding::ReplaceOne),
          InstanceMethod("deleteOne", &DatabaseBinding::DeleteOne),
          InstanceMethod("findOneAndRemove", &DatabaseBinding::FindOneAndRemove),
          InstanceMethod("deleteMany", &DatabaseBinding::DeleteMany),
          InstanceMethod("remove", &DatabaseBinding::Remove),
          InstanceMethod("findOneAndDelete", &DatabaseBinding::FindOneAndDelete),
          InstanceMethod("findOneAndReplace", &DatabaseBinding::FindOneAndReplace),
          InstanceMethod("aggregate", &DatabaseBinding::Aggregate),
          InstanceMethod("backup", &DatabaseBinding::Backup),
          InstanceMethod("restore", &DatabaseBinding::Restore),
          InstanceMethod("close", &DatabaseBinding::Close),
      });

  exports.Set("Database", func);
  return exports;
}

DatabaseBinding::DatabaseBinding(const Napi::CallbackInfo& info)
    : Napi::ObjectWrap<DatabaseBinding>(info) {
  Napi::Env env = info.Env();

  if (info.Length() < 1 || !info[0].IsObject()) {
    Napi::TypeError::New(env, "Database: esperado um objeto de opcoes como "
                                 "primeiro argumento ({ filePath, cacheSizeMb, ... })")
        .ThrowAsJavaScriptException();
    return;
  }

  Napi::Object opts = info[0].As<Napi::Object>();
  storage::StorageOptions storage_options;
  if (opts.Has("filePath")) {
    storage_options.file_path = opts.Get("filePath").ToString().Utf8Value();
  }
  if (opts.Has("cacheSizeMb")) {
    storage_options.cache_size_mb =
        opts.Get("cacheSizeMb").ToNumber().Uint32Value();
  }
  if (opts.Has("readOnly")) {
    storage_options.read_only = opts.Get("readOnly").ToBoolean();
  }

  auto result = storage::StorageEngine::Open(storage_options);
  if (!result.ok()) {
    Napi::Error::New(env, result.status().message())
        .ThrowAsJavaScriptException();
    return;
  }
  storage_ = std::move(result.value());

  // Cria IndexManager e QueryEngine
  index_manager_ = std::make_unique<index::IndexManager>();
  query_engine_ = std::make_unique<query::QueryEngine>(*storage_, *index_manager_);

  NEXODB_LOG_INFO("DatabaseBinding", "Conectado ao banco: " + storage_options.file_path);
}

// =============================================================================
// CRUD Methods
// =============================================================================

Napi::Value DatabaseBinding::Find(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() < 2 || !info[0].IsObject() || !info[1].IsObject()) {
    Napi::TypeError::New(env, "find(filter, options)").ThrowAsJavaScriptException();
    return env.Undefined();
  }

  auto filter_obj = NapiToObject(env, info[0]);
  if (!filter_obj.ok()) {
    Napi::Error::New(env, filter_obj.status().message()).ThrowAsJavaScriptException();
    return env.Undefined();
  }

  query::Filter filter{std::move(filter_obj.value())};
  query::QueryOptions options = ParseQueryOptions(env, info[1]);

  auto deferred = Napi::Promise::Deferred::New(env);

  auto* worker = new FindWorker(env, query_engine_.get(), std::move(filter), std::move(options), std::move(deferred));
  worker->Queue();
  return deferred.Promise();
}

Napi::Value DatabaseBinding::FindOne(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() < 2 || !info[0].IsObject() || !info[1].IsObject()) {
    Napi::TypeError::New(env, "findOne(filter, options)").ThrowAsJavaScriptException();
    return env.Undefined();
  }

  auto filter_obj = NapiToObject(env, info[0]);
  if (!filter_obj.ok()) {
    Napi::Error::New(env, filter_obj.status().message()).ThrowAsJavaScriptException();
    return env.Undefined();
  }

  query::Filter filter{std::move(filter_obj.value())};
  query::QueryOptions options = ParseQueryOptions(env, info[1]);

  auto deferred = Napi::Promise::Deferred::New(env);

  auto* worker = new FindOneWorker(env, query_engine_.get(), std::move(filter), std::move(options), std::move(deferred));
  worker->Queue();
  return deferred.Promise();
}

Napi::Value DatabaseBinding::FindById(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() < 2 || !info[0].IsString() || !info[1].IsObject()) {
    Napi::TypeError::New(env, "findById(id, options)").ThrowAsJavaScriptException();
    return env.Undefined();
  }

  std::string id_str = info[0].As<Napi::String>().Utf8Value();
  query::QueryOptions options = ParseQueryOptions(env, info[1]);

  // Converte _id string para ObjectId
  document::ObjectId oid;
  bool valid_oid = false;
  if (id_str.size() == 24) {
    try {
      for (int i = 0; i < 12; ++i) {
        std::string byte_str = id_str.substr(i * 2, 2);
        oid.Bytes()[i] = static_cast<uint8_t>(std::stoi(byte_str, nullptr, 16));
      }
      valid_oid = true;
    } catch (...) {
      valid_oid = false;
    }
  }

  if (!valid_oid) {
    // Se nao eh ObjectId valido, usa busca por string (fallback para full scan)
    document::Object filter_obj;
    filter_obj["_id"] = document::Value(id_str);
    query::Filter filter{std::move(filter_obj)};

    auto deferred = Napi::Promise::Deferred::New(env);
    auto* worker = new FindOneWorker(env, query_engine_.get(), std::move(filter), std::move(options), std::move(deferred));
    worker->Queue();
    return deferred.Promise();
  }

  // Usa o novo metodo FindById com indice primario (O(1))
  auto deferred = Napi::Promise::Deferred::New(env);
  auto* worker = new FindByIdWorker(env, query_engine_.get(), std::move(oid), std::move(options), std::move(deferred));
  worker->Queue();
  return deferred.Promise();
}

Napi::Value DatabaseBinding::Count(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() < 1 || !info[0].IsObject()) {
    Napi::TypeError::New(env, "count(filter)").ThrowAsJavaScriptException();
    return env.Undefined();
  }

  auto filter_obj = NapiToObject(env, info[0]);
  if (!filter_obj.ok()) {
    Napi::Error::New(env, filter_obj.status().message()).ThrowAsJavaScriptException();
    return env.Undefined();
  }

  query::Filter filter{std::move(filter_obj.value())};

  auto deferred = Napi::Promise::Deferred::New(env);

  auto* worker = new CountWorker(env, query_engine_.get(), std::move(filter), std::move(deferred));
  worker->Queue();
  return deferred.Promise();
}

Napi::Value DatabaseBinding::CountDocuments(const Napi::CallbackInfo& info) {
  return Count(info);  // Mesmo comportamento para countDocuments
}

class InsertOneWorker : public Napi::AsyncWorker {
 public:
  InsertOneWorker(Napi::Env env, query::QueryEngine* engine, document::Document doc,
                  Napi::Promise::Deferred deferred)
      : Napi::AsyncWorker(env), engine_(engine), doc_(std::move(doc)),
        deferred_(std::move(deferred)) {}

  void Execute() override {
    result_ = engine_->InsertOne(doc_);
  }

  void OnOK() override {
    if (result_.ok()) {
      // Retorna o documento inserido com seu _id
      deferred_.Resolve(ResolveDoc(Env(), std::make_optional(doc_)));
    } else {
      deferred_.Reject(Napi::Error::New(Env(), result_.status().message()).Value());
    }
  }

  void OnError(const Napi::Error& e) override {
    deferred_.Reject(e.Value());
  }

 private:
  query::QueryEngine* engine_;
  document::Document doc_;
  Napi::Promise::Deferred deferred_;
  common::Result<storage::RecordLocation> result_;
};

Napi::Value DatabaseBinding::InsertOne(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() < 1 || !info[0].IsObject()) {
    Napi::TypeError::New(env, "insertOne(document)").ThrowAsJavaScriptException();
    return env.Undefined();
  }

  auto doc_obj = NapiToObject(env, info[0]);
  if (!doc_obj.ok()) {
    Napi::Error::New(env, doc_obj.status().message()).ThrowAsJavaScriptException();
    return env.Undefined();
  }

  document::Document doc{std::move(doc_obj.value())};
  // Garante que o documento tem _id
  doc.EnsureId();

  auto deferred = Napi::Promise::Deferred::New(env);

  auto* worker = new InsertOneWorker(env, query_engine_.get(), std::move(doc), std::move(deferred));
  worker->Queue();
  return deferred.Promise();
}

Napi::Value DatabaseBinding::UpdateOne(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() < 2 || !info[0].IsObject() || !info[1].IsObject()) {
    Napi::TypeError::New(env, "updateOne(filter, update)").ThrowAsJavaScriptException();
    return env.Undefined();
  }

  auto filter_obj = NapiToObject(env, info[0]);
  if (!filter_obj.ok()) {
    Napi::Error::New(env, filter_obj.status().message()).ThrowAsJavaScriptException();
    return env.Undefined();
  }

  auto update_obj = NapiToObject(env, info[1]);
  if (!update_obj.ok()) {
    Napi::Error::New(env, update_obj.status().message()).ThrowAsJavaScriptException();
    return env.Undefined();
  }

  query::Filter filter{std::move(filter_obj.value())};
  query::UpdateSpec update{std::move(update_obj.value())};

  auto deferred = Napi::Promise::Deferred::New(env);

  auto* worker = new UpdateWorker(env, query_engine_.get(), std::move(filter), std::move(update), std::move(deferred));
  worker->Queue();
  return deferred.Promise();
}

Napi::Value DatabaseBinding::UpdateMany(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() < 2 || !info[0].IsObject() || !info[1].IsObject()) {
    Napi::TypeError::New(env, "updateMany(filter, update)").ThrowAsJavaScriptException();
    return env.Undefined();
  }

  auto filter_obj = NapiToObject(env, info[0]);
  if (!filter_obj.ok()) {
    Napi::Error::New(env, filter_obj.status().message()).ThrowAsJavaScriptException();
    return env.Undefined();
  }

  auto update_obj = NapiToObject(env, info[1]);
  if (!update_obj.ok()) {
    Napi::Error::New(env, update_obj.status().message()).ThrowAsJavaScriptException();
    return env.Undefined();
  }

  query::Filter filter{std::move(filter_obj.value())};
  query::UpdateSpec update{std::move(update_obj.value())};

  auto deferred = Napi::Promise::Deferred::New(env);

  auto* worker = new UpdateManyWorker(env, query_engine_.get(), std::move(filter), std::move(update), std::move(deferred));
  worker->Queue();
  return deferred.Promise();
}

Napi::Value DatabaseBinding::ReplaceOne(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() < 2 || !info[0].IsObject() || !info[1].IsObject()) {
    Napi::TypeError::New(env, "replaceOne(filter, replacement)").ThrowAsJavaScriptException();
    return env.Undefined();
  }

  auto filter_obj = NapiToObject(env, info[0]);
  if (!filter_obj.ok()) {
    Napi::Error::New(env, filter_obj.status().message()).ThrowAsJavaScriptException();
    return env.Undefined();
  }

  auto replace_obj = NapiToObject(env, info[1]);
  if (!replace_obj.ok()) {
    Napi::Error::New(env, replace_obj.status().message()).ThrowAsJavaScriptException();
    return env.Undefined();
  }

  // ReplaceOne = UpdateOne com $set do objeto inteiro (exceto _id)
  document::Object replacement = std::move(replace_obj.value());
  replacement.erase("_id");  // Nao permite mudar _id

  document::Object update_obj;
  update_obj["$set"] = document::Value(std::move(replacement));

  query::Filter filter{std::move(filter_obj.value())};
  query::UpdateSpec update{std::move(update_obj)};

  auto deferred = Napi::Promise::Deferred::New(env);

  auto* worker = new ReplaceWorker(env, query_engine_.get(), std::move(filter), std::move(update), std::move(deferred));
  worker->Queue();
  return deferred.Promise();
}

Napi::Value DatabaseBinding::DeleteOne(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() < 1 || !info[0].IsObject()) {
    Napi::TypeError::New(env, "deleteOne(filter)").ThrowAsJavaScriptException();
    return env.Undefined();
  }

  auto filter_obj = NapiToObject(env, info[0]);
  if (!filter_obj.ok()) {
    Napi::Error::New(env, filter_obj.status().message()).ThrowAsJavaScriptException();
    return env.Undefined();
  }

  query::Filter filter{std::move(filter_obj.value())};

  auto deferred = Napi::Promise::Deferred::New(env);

  auto* worker = new DeleteWorker(env, query_engine_.get(), std::move(filter), std::move(deferred));
  worker->Queue();
  return deferred.Promise();
}

Napi::Value DatabaseBinding::FindOneAndRemove(const Napi::CallbackInfo& info) {
  // Alias para findOneAndDelete
  return FindOneAndDelete(info);
}

Napi::Value DatabaseBinding::DeleteMany(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() < 1 || !info[0].IsObject()) {
    Napi::TypeError::New(env, "deleteMany(filter)").ThrowAsJavaScriptException();
    return env.Undefined();
  }

  auto filter_obj = NapiToObject(env, info[0]);
  if (!filter_obj.ok()) {
    Napi::Error::New(env, filter_obj.status().message()).ThrowAsJavaScriptException();
    return env.Undefined();
  }

  query::Filter filter{std::move(filter_obj.value())};

  auto deferred = Napi::Promise::Deferred::New(env);

  auto* worker = new DeleteManyWorker(env, query_engine_.get(), std::move(filter), std::move(deferred));
  worker->Queue();
  return deferred.Promise();
}

Napi::Value DatabaseBinding::Remove(const Napi::CallbackInfo& info) {
  // Alias para deleteMany (deprecated no Mongoose)
  return DeleteMany(info);
}

Napi::Value DatabaseBinding::FindOneAndDelete(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() < 2 || !info[0].IsObject() || !info[1].IsObject()) {
    Napi::TypeError::New(env, "findOneAndDelete(filter, options)").ThrowAsJavaScriptException();
    return env.Undefined();
  }

  auto filter_obj = NapiToObject(env, info[0]);
  if (!filter_obj.ok()) {
    Napi::Error::New(env, filter_obj.status().message()).ThrowAsJavaScriptException();
    return env.Undefined();
  }

  query::Filter filter{std::move(filter_obj.value())};
  query::QueryOptions options = ParseQueryOptions(env, info[1]);

  auto deferred = Napi::Promise::Deferred::New(env);

  auto* worker = new FindOneAndDeleteWorker(env, query_engine_.get(), std::move(filter), std::move(options), std::move(deferred));
  worker->Queue();
  return deferred.Promise();
}

Napi::Value DatabaseBinding::FindOneAndReplace(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() < 3 || !info[0].IsObject() || !info[1].IsObject() || !info[2].IsObject()) {
    Napi::TypeError::New(env, "findOneAndReplace(filter, replacement, options)").ThrowAsJavaScriptException();
    return env.Undefined();
  }

  auto filter_obj = NapiToObject(env, info[0]);
  if (!filter_obj.ok()) {
    Napi::Error::New(env, filter_obj.status().message()).ThrowAsJavaScriptException();
    return env.Undefined();
  }

  auto replace_obj = NapiToObject(env, info[1]);
  if (!replace_obj.ok()) {
    Napi::Error::New(env, replace_obj.status().message()).ThrowAsJavaScriptException();
    return env.Undefined();
  }

  query::QueryOptions options = ParseQueryOptions(env, info[2]);

  // Replace = $set com objeto inteiro
  document::Object replacement = std::move(replace_obj.value());
  replacement.erase("_id");

  document::Object update_obj;
  update_obj["$set"] = document::Value(std::move(replacement));

  query::Filter filter{std::move(filter_obj.value())};
  query::UpdateSpec update{std::move(update_obj)};

  auto deferred = Napi::Promise::Deferred::New(env);

  auto* worker = new FindOneAndReplaceWorker(env, query_engine_.get(), std::move(filter), std::move(update),
                              std::move(options), std::move(deferred));
  worker->Queue();
  return deferred.Promise();
}

Napi::Value DatabaseBinding::FindOneAndUpdate(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() < 3 || !info[0].IsObject() || !info[1].IsObject() || !info[2].IsObject()) {
    Napi::TypeError::New(env, "findOneAndUpdate(filter, update, options)").ThrowAsJavaScriptException();
    return env.Undefined();
  }

  auto filter_obj = NapiToObject(env, info[0]);
  if (!filter_obj.ok()) {
    Napi::Error::New(env, filter_obj.status().message()).ThrowAsJavaScriptException();
    return env.Undefined();
  }

  auto update_obj = NapiToObject(env, info[1]);
  if (!update_obj.ok()) {
    Napi::Error::New(env, update_obj.status().message()).ThrowAsJavaScriptException();
    return env.Undefined();
  }

  query::QueryOptions options = ParseQueryOptions(env, info[2]);

  query::Filter filter{std::move(filter_obj.value())};
  query::UpdateSpec update{std::move(update_obj.value())};

  auto deferred = Napi::Promise::Deferred::New(env);

  auto* worker = new FindOneAndUpdateWorker(env, query_engine_.get(), std::move(filter), std::move(update),
                             std::move(options), std::move(deferred));
  worker->Queue();
  return deferred.Promise();
}

Napi::Value DatabaseBinding::FindByIdAndUpdate(const Napi::CallbackInfo& info) {
  // Converte para findOneAndUpdate com _id
  Napi::Env env = info.Env();

  if (info.Length() < 3 || !info[0].IsString() || !info[1].IsObject() || !info[2].IsObject()) {
    Napi::TypeError::New(env, "findByIdAndUpdate(id, update, options)").ThrowAsJavaScriptException();
    return env.Undefined();
  }

  std::string id_str = info[0].As<Napi::String>().Utf8Value();
  document::Object filter_obj;
  if (id_str.size() == 24) {
    document::ObjectId oid;
    try {
      for (int i = 0; i < 12; ++i) {
        std::string byte_str = id_str.substr(i * 2, 2);
        oid.Bytes()[i] = static_cast<uint8_t>(std::stoi(byte_str, nullptr, 16));
      }
      filter_obj["_id"] = document::Value(oid);
    } catch (...) {
      filter_obj["_id"] = document::Value(id_str);
    }
  } else {
    filter_obj["_id"] = document::Value(id_str);
  }

  query::Filter filter{std::move(filter_obj)};
  auto update_obj = NapiToObject(env, info[1]);
  if (!update_obj.ok()) {
    Napi::Error::New(env, update_obj.status().message()).ThrowAsJavaScriptException();
    return env.Undefined();
  }
  query::UpdateSpec update{std::move(update_obj.value())};
  query::QueryOptions options = ParseQueryOptions(env, info[2]);

  auto deferred = Napi::Promise::Deferred::New(env);

  auto* worker = new FindOneAndUpdateWorker(env, query_engine_.get(), std::move(filter), std::move(update),
                             std::move(options), std::move(deferred));
  worker->Queue();
  return deferred.Promise();
}

Napi::Value DatabaseBinding::Aggregate(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() < 1 || !info[0].IsArray()) {
    Napi::TypeError::New(env, "aggregate(pipeline)").ThrowAsJavaScriptException();
    return env.Undefined();
  }

  // TODO: Implementar pipeline de agregacao
  auto deferred = Napi::Promise::Deferred::New(env);
  deferred.Reject(Napi::Error::New(env, "aggregate: ainda nao implementado").Value());
  return deferred.Promise();
}

// =============================================================================
// Admin Methods
// =============================================================================

Napi::Value DatabaseBinding::Backup(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() < 1 || !info[0].IsObject()) {
    Napi::TypeError::New(env, "backup(options)").ThrowAsJavaScriptException();
    return env.Undefined();
  }

  // TODO: Implementar backup
  auto deferred = Napi::Promise::Deferred::New(env);
  deferred.Reject(Napi::Error::New(env, "backup: ainda nao implementado").Value());
  return deferred.Promise();
}

Napi::Value DatabaseBinding::Restore(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() < 1 || !info[0].IsObject()) {
    Napi::TypeError::New(env, "restore(options)").ThrowAsJavaScriptException();
    return env.Undefined();
  }

  // TODO: Implementar restore
  auto deferred = Napi::Promise::Deferred::New(env);
  deferred.Reject(Napi::Error::New(env, "restore: ainda nao implementado").Value());
  return deferred.Promise();
}

Napi::Value DatabaseBinding::Close(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (storage_) {
    storage_->Flush();
    storage_.reset();
  }
  query_engine_.reset();
  index_manager_.reset();
  return env.Undefined();
}

}  // namespace nexodb::napi