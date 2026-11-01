// =============================================================================
// document_test.cpp
// Testes unitarios de nexodb::document::Document / ObjectId.
// =============================================================================
#include <gtest/gtest.h>

#include "nexodb/document.hpp"

using nexodb::document::Document;
using nexodb::document::ObjectId;
using nexodb::document::Value;

TEST(Document, EnsureIdAddsIdWhenMissing) {
  Document doc;
  EXPECT_EQ(doc.fields().count("_id"), 0u);
  doc.EnsureId();
  EXPECT_EQ(doc.fields().count("_id"), 1u);
}

TEST(ObjectId, ToHexStringHasExpectedLength) {
  ObjectId id = ObjectId::Generate();
  EXPECT_EQ(id.ToHexString().size(), 24u);
}
