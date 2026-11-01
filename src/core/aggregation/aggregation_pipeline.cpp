// =============================================================================
// aggregation_pipeline.cpp
// TODO(core/aggregation): implementar os estagios $match, $group, $sort,
// $project, $limit e $skip, reaproveitando o parser de filtros do
// query_engine para $match sempre que possivel.
// =============================================================================
#include "nexodb/aggregation_pipeline.hpp"

namespace nexodb::aggregation {

common::Result<std::vector<document::Document>> AggregationPipeline::Run(
    const std::vector<document::Document>& /*input*/) const {
  return common::Status::Error(common::StatusCode::kUnsupported,
                                "AggregationPipeline::Run: TODO");
}

}  // namespace nexodb::aggregation
