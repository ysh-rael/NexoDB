// =============================================================================
// database_binding.hpp
// Wrapper Node-API (via node-addon-api) que expoe nexodb::storage::StorageEngine
// e nexodb::query::QueryEngine como a classe `Database` usada pelo lado
// JavaScript (ver lib/Connection.js). Cada metodo aqui converte entre
// objetos JS (Napi::Value) e os tipos C++ do core (document::Document,
// query::Filter, etc.), sem conter logica de negocio propria.
// =============================================================================
#pragma once

#include <napi.h>

#include <memory>

#include "nexodb/index_manager.hpp"
#include "nexodb/query_engine.hpp"
#include "nexodb/storage_engine.hpp"

namespace nexodb::napi {

class DatabaseBinding : public Napi::ObjectWrap<DatabaseBinding> {
 public:
  static Napi::Object Init(Napi::Env env, Napi::Object exports);
  explicit DatabaseBinding(const Napi::CallbackInfo& info);

 private:
  // --- CRUD basico (ver README: find, findOne, findById, updateOne, ...) ---
  Napi::Value Find(const Napi::CallbackInfo& info);
  Napi::Value FindOne(const Napi::CallbackInfo& info);
  Napi::Value FindById(const Napi::CallbackInfo& info);
  Napi::Value FindOneAndUpdate(const Napi::CallbackInfo& info);
  Napi::Value FindByIdAndUpdate(const Napi::CallbackInfo& info);
  Napi::Value Count(const Napi::CallbackInfo& info);
  Napi::Value CountDocuments(const Napi::CallbackInfo& info);
  Napi::Value InsertOne(const Napi::CallbackInfo& info);
  Napi::Value UpdateOne(const Napi::CallbackInfo& info);
  Napi::Value UpdateMany(const Napi::CallbackInfo& info);
  Napi::Value ReplaceOne(const Napi::CallbackInfo& info);
  Napi::Value DeleteOne(const Napi::CallbackInfo& info);
  Napi::Value FindOneAndRemove(const Napi::CallbackInfo& info);
  Napi::Value DeleteMany(const Napi::CallbackInfo& info);
  Napi::Value Remove(const Napi::CallbackInfo& info);
  Napi::Value FindOneAndDelete(const Napi::CallbackInfo& info);
  Napi::Value FindOneAndReplace(const Napi::CallbackInfo& info);
  Napi::Value Aggregate(const Napi::CallbackInfo& info);

  // --- Administracao: backup/restore, flush, fechamento ---
  Napi::Value Backup(const Napi::CallbackInfo& info);
  Napi::Value Restore(const Napi::CallbackInfo& info);
  Napi::Value Close(const Napi::CallbackInfo& info);

  std::unique_ptr<storage::StorageEngine> storage_;
  std::unique_ptr<index::IndexManager> index_manager_;
  std::unique_ptr<query::QueryEngine> query_engine_;
};

}  // namespace nexodb::napi
