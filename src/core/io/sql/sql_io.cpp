// =============================================================================
// sql_io.cpp
// TODO(core/io/sql): comecar com um parser de dump SQL (CREATE TABLE +
// INSERT INTO) suficiente para SQLite/MySQL/Postgres basico, antes de
// considerar conexao direta a um servidor SQL via driver nativo.
// =============================================================================
#include "nexodb/sql_io.hpp"

namespace nexodb::io::sql {

common::Result<std::vector<document::Document>> SqlImporter::Import(
    const SqlImportOptions& /*options*/) {
  return common::Status::Error(common::StatusCode::kUnsupported,
                                "SqlImporter::Import: TODO");
}

common::Status SqlExporter::Export(
    const std::vector<document::Document>& /*docs*/,
    const SqlExportOptions& /*options*/) {
  return common::Status::Error(common::StatusCode::kUnsupported,
                                "SqlExporter::Export: TODO");
}

}  // namespace nexodb::io::sql
