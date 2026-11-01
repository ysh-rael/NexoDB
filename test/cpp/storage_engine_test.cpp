// =============================================================================
// storage_engine_test.cpp
// TODO: substituir pelos testes reais assim que FileStorageEngine estiver
// implementado (Insert/Read/Update/Remove). Hoje apenas garante que Open()
// nao quebra e retorna um Status util.
// =============================================================================
#include <gtest/gtest.h>

#include "nexodb/storage_engine.hpp"

using nexodb::storage::StorageEngine;
using nexodb::storage::StorageOptions;

TEST(StorageEngine, OpenReturnsAnEngineInstance) {
  StorageOptions options;
  options.file_path = "/tmp/nexodb-unit-test.nxdb";

  auto result = StorageEngine::Open(options);
  ASSERT_TRUE(result.ok());
  EXPECT_NE(result.value(), nullptr);
}
