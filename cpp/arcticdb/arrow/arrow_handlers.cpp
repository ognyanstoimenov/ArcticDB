/* Copyright 2023 Man Group Operations Limited
 *
 * Use of this software is governed by the Business Source License 1.1 included in the file licenses/BSL.txt.
 *
 * As of the Change Date specified in that file, in accordance with the Business Source License, use of this software will be governed by the Apache License, version 2.0.
 */
#include <arrow/arrow_handlers.hpp>
#include <arcticdb/codec/slice_data_sink.hpp>
#include <arcticdb/codec/encoding_sizes.hpp>
#include <arcticdb/codec/codec.hpp>
#include <arcticdb/util/buffer_holder.hpp>
#include <arcticdb/pipeline/column_mapping.hpp>
#include <arcticdb/util/sparse_utils.hpp>

namespace arcticdb {

void ArrowStringHandler::handle_type(
    const uint8_t *&data,
    Column& dest_column,
    const EncodedFieldImpl &field,
    const ColumnMapping& m,
    const DecodePathData& shared_data,
    std::any& handler_data,
    EncodingVersion encoding_version,
    const std::shared_ptr<StringPool>& string_pool) {
    ARCTICDB_SAMPLE(ArrowHandleString, 0)
    util::check(field.has_ndarray(), "String handler expected array");
    ARCTICDB_DEBUG(log::version(), "String handler got encoded field: {}", field.DebugString());
    const auto &ndarray = field.ndarray();
    const auto bytes = encoding_sizes::data_uncompressed_size(ndarray);

    auto decoded_data = [&m, &ndarray, bytes, &dest_column]() {
        if(ndarray.sparse_map_bytes() > 0) {
            return Column(m.source_type_desc_, bytes / get_type_size(m.source_type_desc_.data_type()), AllocationType::DYNAMIC, Sparsity::PERMITTED);
        } else {
            Column column(m.source_type_desc_, Sparsity::NOT_PERMITTED);
            column.buffer().add_external_block(dest_column.bytes_at(m.offset_bytes_), bytes, 0UL);
            return column;
        }
    }();

    data += decode_field(m.source_type_desc_, field, data, decoded_data, decoded_data.opt_sparse_map(), encoding_version);

    if(is_dynamic_string_type(m.dest_type_desc_.data_type())) {
        convert_type(
            decoded_data,
            dest_column,
            m.num_rows_,
            m.offset_bytes_,
            m.source_type_desc_,
            m.dest_type_desc_,
            shared_data,
            handler_data,
            string_pool);
    }
}

void ArrowStringHandler::convert_type(
    const Column& source_column,
    Column& dest_column,
    size_t num_rows,
    size_t offset_bytes,
    TypeDescriptor source_type_desc,
    TypeDescriptor dest_type_desc,
    const DecodePathData& shared_data,
    std::any& handler_data,
    const std::shared_ptr<StringPool>& string_pool) {

}

int ArrowStringHandler::type_size() const {
    return sizeof(uint32_t);
}

} // namespace arcticdb