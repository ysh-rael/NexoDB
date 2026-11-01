// =============================================================================
// json_io.hpp
// Importacao/exportacao de documentos em JSON puro (NDJSON ou array JSON),
// usada para migrar dados de/para outras fontes e para backups legiveis por
// humanos. Separado do formato interno do storage engine, que e binario.
// =============================================================================
#pragma once

#include <string>
#include <vector>

#include "nexodb/common.hpp"
#include "nexodb/document.hpp"

namespace nexodb::io::json {

class JsonImporter {
 public:
  // Le um arquivo .json ou .ndjson e retorna os documentos decodificados.
  static common::Result<std::vector<document::Document>> ImportFile(
      const std::string& path);
};

class JsonExporter {
 public:
  // Escreve os documentos no caminho informado. pretty controla indentacao.
  static common::Status ExportFile(
      const std::vector<document::Document>& docs, const std::string& path,
      bool pretty = false);
};

}  // namespace nexodb::io::json
