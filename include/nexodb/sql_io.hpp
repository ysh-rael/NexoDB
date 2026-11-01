// =============================================================================
// sql_io.hpp
// Importacao/exportacao entre NexoDB e bancos SQL: le um dump .sql (ou
// conecta a um banco via driver externo, a definir) e converte linhas de
// tabela em documentos, e vice-versa, com um mapeamento de schema simples
// (tabela -> colecao, coluna -> campo).
// =============================================================================
#pragma once

#include <string>
#include <vector>

#include "nexodb/common.hpp"
#include "nexodb/document.hpp"

namespace nexodb::io::sql {

struct SqlImportOptions {
  std::string sql_dump_path;
  std::string table_name;
};

struct SqlExportOptions {
  std::string output_path;
  std::string table_name;
};

class SqlImporter {
 public:
  static common::Result<std::vector<document::Document>> Import(
      const SqlImportOptions& options);
};

class SqlExporter {
 public:
  static common::Status Export(const std::vector<document::Document>& docs,
                                const SqlExportOptions& options);
};

}  // namespace nexodb::io::sql
