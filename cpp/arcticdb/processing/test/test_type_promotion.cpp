/* Copyright 2023 Man Group Operations Limited
 *
 * Use of this software is governed by the Business Source License 1.1 included in the file licenses/BSL.txt.
 *
 * As of the Change Date specified in that file, in accordance with the Business Source License, use of this software will be governed by the Apache License, version 2.0.
 */

#include <gtest/gtest.h>
#include <arcticdb/entity/types_proto.hpp>
#include <arcticdb/entity/type_utils.hpp>

TEST(HasValidTypePromotion, DifferentDimensions) {
    using namespace arcticdb;
    using namespace arcticdb::entity;
    TypeDescriptor source(ValueType::UNKNOWN_VALUE_TYPE, SizeBits::UNKNOWN_SIZE_BITS, Dimension::Dim0);
    TypeDescriptor target(ValueType::UNKNOWN_VALUE_TYPE, SizeBits::UNKNOWN_SIZE_BITS, Dimension::Dim1);
    auto result = is_valid_type_promotion_to_target(source, target);
    EXPECT_FALSE(result);
    EXPECT_FALSE(has_valid_common_type(source, target));
}

TEST(HasValidTypePromotion, NonNumericTypes) {
    using namespace arcticdb;
    using namespace arcticdb::entity;
    TypeDescriptor non_numeric_source(ValueType::ASCII_FIXED, SizeBits::UNKNOWN_SIZE_BITS, Dimension::Dim0);
    TypeDescriptor numeric_target(ValueType::FLOAT, SizeBits::S64, Dimension::Dim0);
    auto result = is_valid_type_promotion_to_target(non_numeric_source, numeric_target);
    EXPECT_FALSE(result);
    EXPECT_FALSE(has_valid_common_type(non_numeric_source, numeric_target));
    TypeDescriptor numeric_source(ValueType::FLOAT, SizeBits::S64, Dimension::Dim0);
    TypeDescriptor non_numeric_target(ValueType::ASCII_FIXED, SizeBits::UNKNOWN_SIZE_BITS, Dimension::Dim0);
    result = is_valid_type_promotion_to_target(numeric_source, non_numeric_target);
    EXPECT_FALSE(result);
    EXPECT_FALSE(has_valid_common_type(numeric_source, non_numeric_target));
}

TEST(HasValidTypePromotion, UintUint) {
    using namespace arcticdb;
    using namespace arcticdb::entity;
    TypeDescriptor source(ValueType::UINT, SizeBits::S16, Dimension::Dim0);
    TypeDescriptor target_narrower(ValueType::UINT, SizeBits::S8, Dimension::Dim0);
    TypeDescriptor target_same_width(ValueType::UINT, SizeBits::S16, Dimension::Dim0);
    TypeDescriptor target_wider(ValueType::UINT, SizeBits::S32, Dimension::Dim0);
    EXPECT_FALSE(is_valid_type_promotion_to_target(source, target_narrower));
    EXPECT_TRUE(is_valid_type_promotion_to_target(source, target_same_width));
    ASSERT_EQ(has_valid_common_type(source, target_same_width), target_same_width);
    ASSERT_EQ(has_valid_common_type(source, target_wider), target_wider);
}

TEST(HasValidTypePromotion, UintInt) {
    using namespace arcticdb;
    using namespace arcticdb::entity;
    TypeDescriptor source(ValueType::UINT, SizeBits::S16, Dimension::Dim0);
    TypeDescriptor target_narrower(ValueType::INT, SizeBits::S8, Dimension::Dim0);
    TypeDescriptor target_same_width(ValueType::INT, SizeBits::S16, Dimension::Dim0);
    TypeDescriptor target_wider(ValueType::INT, SizeBits::S32, Dimension::Dim0);
    EXPECT_FALSE(is_valid_type_promotion_to_target(source, target_narrower));
    EXPECT_FALSE(is_valid_type_promotion_to_target(source, target_same_width));
    ASSERT_EQ(has_valid_common_type(source, target_wider), target_wider);
}

TEST(HasValidTypePromotion, UintFloat) {
    using namespace arcticdb;
    using namespace arcticdb::entity;
    TypeDescriptor source(ValueType::UINT, SizeBits::S64, Dimension::Dim0);
    TypeDescriptor float32(ValueType::FLOAT, SizeBits::S32, Dimension::Dim0);
    TypeDescriptor float64(ValueType::FLOAT, SizeBits::S64, Dimension::Dim0);
    EXPECT_FALSE(is_valid_type_promotion_to_target(source, float32));
    EXPECT_TRUE(is_valid_type_promotion_to_target(source, float64));
    ASSERT_EQ(has_valid_common_type(source, float32), float64);
    ASSERT_EQ(has_valid_common_type(source, float64), float64);
}

TEST(HasValidTypePromotion, IntUint) {
    using namespace arcticdb;
    using namespace arcticdb::entity;
    TypeDescriptor source(ValueType::INT, SizeBits::S8, Dimension::Dim0);
    TypeDescriptor target(ValueType::UINT, SizeBits::S64, Dimension::Dim0);
    EXPECT_FALSE(has_valid_common_type(source, target));
    EXPECT_FALSE(is_valid_type_promotion_to_target(source, target));
}

TEST(HasValidTypePromotion, IntInt) {
    using namespace arcticdb;
    using namespace arcticdb::entity;
    TypeDescriptor source(ValueType::INT, SizeBits::S16, Dimension::Dim0);
    TypeDescriptor target_narrower(ValueType::INT, SizeBits::S8, Dimension::Dim0);
    TypeDescriptor target_same_width(ValueType::INT, SizeBits::S16, Dimension::Dim0);
    TypeDescriptor target_wider(ValueType::INT, SizeBits::S32, Dimension::Dim0);
    EXPECT_FALSE(is_valid_type_promotion_to_target(source, target_narrower));
    EXPECT_TRUE(is_valid_type_promotion_to_target(source, target_same_width));
    ASSERT_EQ(has_valid_common_type(source, target_same_width), target_same_width);
    ASSERT_EQ(has_valid_common_type(source, target_wider), target_wider);
}

TEST(HasValidTypePromotion, IntFloat) {
    using namespace arcticdb;
    using namespace arcticdb::entity;
    TypeDescriptor source(ValueType::INT, SizeBits::S64, Dimension::Dim0);
    TypeDescriptor float32(ValueType::FLOAT, SizeBits::S32, Dimension::Dim0);
    TypeDescriptor float64(ValueType::FLOAT, SizeBits::S64, Dimension::Dim0);
    EXPECT_FALSE(is_valid_type_promotion_to_target(source, float32));
    EXPECT_TRUE(is_valid_type_promotion_to_target(source, float64));
    ASSERT_EQ(has_valid_common_type(source, float32), float64);
    ASSERT_EQ(has_valid_common_type(source, float64), float64);
}

TEST(HasValidTypePromotion, FloatUint) {
    using namespace arcticdb;
    using namespace arcticdb::entity;
    TypeDescriptor source(ValueType::FLOAT, SizeBits::S32, Dimension::Dim0);
    TypeDescriptor target(ValueType::UINT, SizeBits::S64, Dimension::Dim0);
    EXPECT_FALSE(is_valid_type_promotion_to_target(source, target));
    EXPECT_TRUE(has_valid_common_type(source, target));
}

TEST(HasValidTypePromotion, FloatInt) {
    using namespace arcticdb;
    using namespace arcticdb::entity;
    TypeDescriptor source(ValueType::FLOAT, SizeBits::S32, Dimension::Dim0);
    TypeDescriptor target(ValueType::INT, SizeBits::S64, Dimension::Dim0);
    EXPECT_FALSE(is_valid_type_promotion_to_target(source, target));
    TypeDescriptor float64(ValueType::FLOAT, SizeBits::S64, Dimension::Dim0);
    EXPECT_EQ(has_valid_common_type(source, target).value(), float64);
}

TEST(HasValidTypePromotion, FloatFloat) {
    using namespace arcticdb;
    using namespace arcticdb::entity;
    TypeDescriptor float32(ValueType::FLOAT, SizeBits::S32, Dimension::Dim0);
    TypeDescriptor float64(ValueType::FLOAT, SizeBits::S64, Dimension::Dim0);
    ASSERT_EQ(has_valid_common_type(float32, float64), float64);
    ASSERT_EQ(has_valid_common_type(float32, float32), float32);
    EXPECT_FALSE(is_valid_type_promotion_to_target(float64, float32));
    EXPECT_TRUE(has_valid_common_type(float64, float32));
}

TEST(HasValidTypePromotion, EmptyToEverything) {
    using namespace arcticdb;
    using namespace arcticdb::entity;
    TypeDescriptor source(ValueType::EMPTY, SizeBits::S64, Dimension::Dim0);
    for(int value_type = int(ValueType::UNKNOWN_VALUE_TYPE); value_type < int(ValueType::COUNT); ++value_type) {
        for(int size_bits = int(SizeBits::UNKNOWN_SIZE_BITS); size_bits < int(SizeBits::COUNT); ++size_bits) {
            const TypeDescriptor target(ValueType(value_type), SizeBits(size_bits), Dimension::Dim0);
            ASSERT_TRUE(is_valid_type_promotion_to_target(source, target));
            ASSERT_EQ(has_valid_common_type(source, target), target);
        }
    }
}

TEST(HasValidTypePromotion, EverythingToEmpty) {
    using namespace arcticdb;
    using namespace arcticdb::entity;
    const TypeDescriptor target(ValueType::EMPTY, SizeBits::S64, Dimension::Dim0);
    for(int value_type = int(ValueType::UNKNOWN_VALUE_TYPE); value_type < int(ValueType::COUNT); ++value_type) {
        for(int size_bits = int(SizeBits::UNKNOWN_SIZE_BITS); size_bits < int(SizeBits::COUNT); ++size_bits) {
            const TypeDescriptor source(ValueType(value_type), SizeBits(size_bits), Dimension::Dim0);
            if(!is_empty_type(source.data_type())) {
                ASSERT_FALSE(is_valid_type_promotion_to_target(source, target));
            } else {
                ASSERT_TRUE(is_valid_type_promotion_to_target(source, target));
                ASSERT_EQ(has_valid_common_type(source, target), target);
            }
        }
    }
}

TEST(HasValidCommonType, UintInt) {
    using namespace arcticdb;
    using namespace arcticdb::entity;
    TypeDescriptor uint32(ValueType::UINT, SizeBits::S32, Dimension::Dim0);
    TypeDescriptor uint64(ValueType::UINT, SizeBits::S64, Dimension::Dim0);
    TypeDescriptor int16(ValueType::INT, SizeBits::S16, Dimension::Dim0);
    TypeDescriptor int32(ValueType::INT, SizeBits::S32, Dimension::Dim0);
    TypeDescriptor int64(ValueType::INT, SizeBits::S64, Dimension::Dim0);
    ASSERT_EQ(has_valid_common_type(uint32, int16), int64);
    ASSERT_EQ(has_valid_common_type(uint32, int32), int64);
    ASSERT_EQ(has_valid_common_type(uint32, int64), int64);
    EXPECT_FALSE(has_valid_common_type(uint64, int64));
    // has_valid_common_type should be symmetric, these assertions are as above with the arguments reversed
    ASSERT_EQ(has_valid_common_type(int16, uint32), int64);
    ASSERT_EQ(has_valid_common_type(int32, uint32), int64);
    ASSERT_EQ(has_valid_common_type(int64, uint32), int64);
    EXPECT_FALSE(has_valid_common_type(int64, uint64));
}
