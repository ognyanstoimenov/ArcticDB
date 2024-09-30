/* Copyright 2023 Man Group Operations Limited
 *
 * Use of this software is governed by the Business Source License 1.1 included in the file licenses/BSL.txt.
 *
 * As of the Change Date specified in that file, in accordance with the Business Source License, use of this software will be governed by the Apache License, version 2.0.
 */

#pragma once

#include <optional>
#include <arcticdb/pipeline/index_segment_reader.hpp>
#include <arcticdb/pipeline/pipeline_context.hpp>

namespace arcticdb {

std::pair<int, int> row_range_to_positive_bounds(
    const std::optional<std::pair<std::optional<int>, std::optional<int>>>& row_range,
    int row_count) {
    if (!row_range) {
        return {0, row_count};
    }

    int row_range_start = row_range->first.value_or(0);
    int row_range_end = row_range->second.value_or(row_count);

    if (row_range_start < 0) {
        row_range_start += row_count;
    }
    if (row_range_end < 0) {
        row_range_end += row_count;
    }

    return {row_range_start, row_range_end};
}

class ChunkIterator {
    pipelines::index::IndexSegmentReader index_segment_reader_;
    std::shared_ptr<pipelines::PipelineContext> pipeline_context_;
    const std::optional<size_t> chunk_size_;
    const std::optional<size_t> num_chunks_;
    size_t chunk_pos_ = 0;
    size_t row_pos_ = 0;

    ChunkIterator(
        pipelines::index::IndexSegmentReader&& index_segment_reader,
        std::shared_ptr<pipelines::PipelineContext> pipeline_context,
        const std::optional<size_t> chunk_size,
        const std::optional<size_t> num_chunks) :
        index_segment_reader_(std::move(index_segment_reader)),
        pipeline_context_(std::move(pipeline_context)),
        chunk_size_(chunk_size),
        num_chunks_(num_chunks) {
    }



};

} // namespace arcticdb