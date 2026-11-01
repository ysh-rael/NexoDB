// =============================================================================
// json_io.cpp
// TODO(core/io/json): implementar parsing/serializacao JSON proprios (ou via
// biblioteca header-only vendorizada) com streaming para nao carregar
// arquivos gigantes inteiramente em memoria durante import/export.
// =============================================================================
#include "nexodb/json_io.hpp"

namespace nexodb::io::json {

common::Result<std::vector<document::Document>> JsonImporter::ImportFile(
    const std::string& /*path*/) {
  return common::Status::Error(common::StatusCode::kUnsupported,
                                "JsonImporter::ImportFile: TODO");
}

common::Status JsonExporter::ExportFile(
    const std::vector<document::Document>& /*docs*/,
    const std::string& /*path*/, bool /*pretty*/) {
  return common::Status::Error(common::StatusCode::kUnsupported,
                                "JsonExporter::ExportFile: TODO");
}

}  // namespace nexodb::io::json
