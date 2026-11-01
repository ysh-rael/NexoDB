// =============================================================================
// module.cpp
// Ponto de entrada do addon nativo. Registra as classes expostas ao
// JavaScript (hoje: Database) no objeto `exports` do modulo .node.
// Mantido enxuto de proposito: cada classe exportada tem seu proprio
// arquivo de binding (ex.: database_binding.cpp).
// =============================================================================
#include <napi.h>

#include "database_binding.hpp"

Napi::Object InitModule(Napi::Env env, Napi::Object exports) {
  nexodb::napi::DatabaseBinding::Init(env, exports);
  return exports;
}

NODE_API_MODULE(nexodb_native, InitModule)
