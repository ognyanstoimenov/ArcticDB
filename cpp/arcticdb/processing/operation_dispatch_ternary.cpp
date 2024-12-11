/*
 * Copyright 2024 Man Group Operations Limited
 *
 * Use of this software is governed by the Business Source License 1.1 included in the file licenses/BSL.txt.
 *
 * As of the Change Date specified in that file, in accordance with the Business Source License, use of this software will be governed by the Apache License, version 2.0.
 */

#include <arcticdb/processing/operation_dispatch_ternary.hpp>

namespace arcticdb {

VariantData ternary_operator(const util::BitSet& condition, const util::BitSet& left, const util::BitSet& right) {
    util::BitSet output_bitset;
    // TODO: relax condition when adding sparse support
    auto output_size = condition.size();
    internal::check<ErrorCode::E_ASSERTION_FAILURE>(left.size() == output_size && right.size() == output_size, "Mismatching bitset sizes");
    output_bitset = right;
    auto end_bit = condition.end();
    for (auto set_bit = condition.first(); set_bit < end_bit; ++set_bit) {
        output_bitset[*set_bit] = left[*set_bit];
    }
    return VariantData{std::move(output_bitset)};
}

VariantData ternary_operator(const util::BitSet& condition, const ColumnWithStrings& left, const ColumnWithStrings& right) {
    schema::check<ErrorCode::E_UNSUPPORTED_COLUMN_TYPE>(
            !is_empty_type(left.column_->type().data_type()) && !is_empty_type(right.column_->type().data_type()),
            "Empty column provided to ternary operator");
    std::unique_ptr<Column> output_column;
    std::shared_ptr<StringPool> string_pool;

    details::visit_type(left.column_->type().data_type(), [&](auto left_tag) {
        using left_type_info = ScalarTypeInfo<decltype(left_tag)>;
        details::visit_type(right.column_->type().data_type(), [&](auto right_tag) {
            using right_type_info = ScalarTypeInfo<decltype(right_tag)>;
            if constexpr(is_sequence_type(left_type_info::data_type) && is_sequence_type(right_type_info::data_type)) {
                if constexpr(left_type_info::data_type == right_type_info::data_type && is_dynamic_string_type(left_type_info::data_type)) {
                    output_column = std::make_unique<Column>(make_scalar_type(left_type_info::data_type), Sparsity::PERMITTED);
                    string_pool = std::make_shared<StringPool>();
                    // TODO: Could this be more efficient?
                    size_t idx{0};
                    Column::transform<typename left_type_info::TDT, typename right_type_info::TDT, typename left_type_info::TDT>(
                            *(left.column_),
                            *(right.column_),
                            *output_column,
                            [&condition, &idx, &string_pool, &left, &right](auto left_value, auto right_value) -> typename left_type_info::RawType {
                                std::optional<std::string_view> string_at_offset;
                                if (condition[idx]) {
                                    string_at_offset = left.string_at_offset(left_value);
                                } else {
                                    string_at_offset = right.string_at_offset(right_value);
                                }
                                if (string_at_offset.has_value()) {
                                    ++idx;
                                    auto offset_string = string_pool->get(*string_at_offset);
                                    return offset_string.offset();
                                } else {
                                    return condition[idx++] ? left_value : right_value;
                                }
                            });
                }
                // TODO: Handle else?
            } else if constexpr ((is_numeric_type(left_type_info::data_type) && is_numeric_type(right_type_info::data_type)) ||
                                 (is_bool_type(left_type_info::data_type) && is_bool_type(right_type_info::data_type))) {
                // TODO: Hacky to reuse type_arithmetic_promoted_type with IsInOperator, and incorrect when there is a uint64_t and an int64_t column
                using TargetType = typename type_arithmetic_promoted_type<typename left_type_info::RawType, typename right_type_info::RawType, IsInOperator>::type;
                constexpr auto output_data_type = data_type_from_raw_type<TargetType>();
                output_column = std::make_unique<Column>(make_scalar_type(output_data_type), Sparsity::PERMITTED);
                // TODO: Could this be more efficient?
                size_t idx{0};
                Column::transform<typename left_type_info::TDT, typename right_type_info::TDT, ScalarTagType<DataTypeTag<output_data_type>>>(
                        *(left.column_),
                        *(right.column_),
                        *output_column,
                        [&condition, &idx](auto left_value, auto right_value) -> TargetType {
                            return condition[idx++] ? left_value : right_value;
                        });
            } else {
                // TODO: Add equivalent of binary_operation_with_types_to_string for ternary
                user_input::raise<ErrorCode::E_INVALID_USER_ARGUMENT>("Invalid comparison");
            }
        });
    });
    // TODO: add equivalent of binary_operation_column_name for ternary operator
    return {ColumnWithStrings(std::move(output_column), string_pool, "some string")};
}

VariantData visit_ternary_operator(const VariantData& condition, const VariantData& left, const VariantData& right) {
    if(std::holds_alternative<EmptyResult>(left) || std::holds_alternative<EmptyResult>(right))
        return EmptyResult{};

    auto transformed_condition = transform_to_bitset(condition);

    return std::visit(util::overload {
            [] (const util::BitSet& c, const util::BitSet& l, const util::BitSet& r) -> VariantData {
                auto result = ternary_operator(c, l, r);
                return transform_to_placeholder(result);
            },
            [] (const util::BitSet& c, const ColumnWithStrings& l, const ColumnWithStrings& r) -> VariantData {
                auto result = ternary_operator(c, l, r);
                return transform_to_placeholder(result);
            },
            [](const auto &, const auto&, const auto&) -> VariantData {
                user_input::raise<ErrorCode::E_INVALID_USER_ARGUMENT>("Invalid input types to ternary operator");
                return EmptyResult{};
            }
    }, transformed_condition, left, right);
}

VariantData dispatch_ternary(const VariantData& condition, const VariantData& left, const VariantData& right, OperationType operation) {
    switch(operation) {
        case OperationType::TERNARY:
            return visit_ternary_operator(condition, left, right);
        default:
            util::raise_rte("Unknown operation {}", int(operation));
    }
}

}
