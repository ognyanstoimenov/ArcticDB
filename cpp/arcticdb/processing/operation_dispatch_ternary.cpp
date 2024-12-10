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

VariantData visit_ternary_operator(const VariantData& condition, const VariantData& left, const VariantData& right) {
    if(std::holds_alternative<EmptyResult>(left) || std::holds_alternative<EmptyResult>(right))
        return EmptyResult{};

    auto transformed_condition = transform_to_bitset(condition);

    return std::visit(util::overload {
            [] (const util::BitSet& c, const util::BitSet& l, const util::BitSet& r) -> VariantData {
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
