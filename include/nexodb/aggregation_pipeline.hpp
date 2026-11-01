// =============================================================================
// aggregation_pipeline.hpp
// Suporte ao metodo aggregate(): executa uma sequencia de estagios (ex.:
// $match, $group, $sort, $project, $limit) sobre os documentos da colecao,
// no mesmo espirito do pipeline de agregacao do MongoDB.
// =============================================================================
#pragma once

#include <vector>

#include "nexodb/common.hpp"
#include "nexodb/document.hpp"

namespace nexodb::aggregation {

// Um estagio do pipeline, ainda nao tipado (parser dedicado fica para uma
// proxima etapa, junto com o parser de filtros do query_engine).
struct Stage {
  document::Object raw;
};

class AggregationPipeline {
 public:
  explicit AggregationPipeline(std::vector<Stage> stages)
      : stages_(std::move(stages)) {}

  common::Result<std::vector<document::Document>> Run(
      const std::vector<document::Document>& input) const;

 private:
  std::vector<Stage> stages_;
};

}  // namespace nexodb::aggregation
