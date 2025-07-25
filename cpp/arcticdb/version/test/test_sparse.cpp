/* Copyright 2023 Man Group Operations Limited
 *
 * Use of this software is governed by the Business Source License 1.1 included in the file licenses/BSL.txt.
 *
 * As of the Change Date specified in that file, in accordance with the Business Source License, use of this software will be governed by the Apache License, version 2.0.
 */

#include <gtest/gtest.h>
#include <util/test/gtest_utils.hpp>

#include <arcticdb/stream/index.hpp>
#include <arcticdb/stream/schema.hpp>
#include <arcticdb/stream/aggregator.hpp>
#include <arcticdb/util/test/generators.hpp>
#include <arcticdb/stream/test/stream_test_common.hpp>
#include <arcticdb/python/python_to_tensor_frame.hpp>
#include <arcticdb/python/python_handlers.hpp>
#include <arcticdb/python/python_types.hpp>
#include <arcticdb/util/native_handler.hpp>

struct SparseTestStore : arcticdb::TestStore {
protected:
    std::string get_name() override {
        return "test.sparse";
    }
};

TEST(Sparse, Simple) {
    using namespace arcticdb;
    using namespace arcticdb::stream;
    using DynamicAggregator =  Aggregator<TimeseriesIndex, DynamicSchema, stream::NeverSegmentPolicy, stream::SparseColumnPolicy>;
    using DynamicSinkWrapper = SinkWrapperImpl<DynamicAggregator>;

    const std::string stream_id("test_sparse");

    DynamicSinkWrapper wrapper(stream_id, {});
    auto& aggregator = wrapper.aggregator_;

    aggregator.start_row(timestamp{0})([](auto& rb) {
        rb.set_scalar_by_name("first", uint32_t(5), DataType::UINT32);
    });

    aggregator.start_row(timestamp{1})([](auto& rb) {
        rb.set_scalar_by_name("second", uint64_t(6), DataType::UINT64);
    });

    wrapper.aggregator_.commit();

    auto segment = wrapper.segment();
    ASSERT_EQ(segment.row_count(), 2);
    auto val1 = segment.scalar_at<uint32_t>(0, 1);
    ASSERT_EQ(val1, 5);
    auto val2 = segment.scalar_at<uint32_t>(0, 2);
    ASSERT_EQ(static_cast<bool>(val2), false);
    auto val3 = segment.scalar_at<uint32_t>(1, 1);
    ASSERT_EQ(static_cast<bool>(val3), false);
    auto val4 = segment.scalar_at<uint32_t>(1, 2);
    ASSERT_EQ(val4, 6);
}

TEST_F(SparseTestStore, SimpleRoundtrip) {
    using namespace arcticdb;
    using namespace arcticdb::stream;
    using DynamicAggregator =  Aggregator<TimeseriesIndex, DynamicSchema, stream::NeverSegmentPolicy, stream::SparseColumnPolicy>;
    using DynamicSinkWrapper = SinkWrapperImpl<DynamicAggregator>;

    const std::string stream_id("test_sparse");

    DynamicSinkWrapper wrapper(stream_id, {});
    auto& aggregator = wrapper.aggregator_;

    aggregator.start_row(timestamp{0})([](auto& rb) {
        rb.set_scalar_by_name("first",  uint32_t(5), DataType::UINT32);
    });

    aggregator.start_row(timestamp{1})([](auto& rb) {
        rb.set_scalar_by_name("second",  uint64_t(6), DataType::UINT64);
    });

    wrapper.aggregator_.commit();

    auto segment = wrapper.segment();
    test_store_->append_incomplete_segment(stream_id, std::move(segment));

    ReadOptions read_options;
    read_options.set_dynamic_schema(true);
    read_options.set_incompletes(true);
    auto read_query = std::make_shared<ReadQuery>();
    read_query->row_filter = universal_range();
    register_native_handler_data_factory();
    auto handler_data = TypeHandlerRegistry::instance()->get_handler_data(OutputFormat::NATIVE);
    auto read_result = test_store_->read_dataframe_version(stream_id, pipelines::VersionQuery{}, read_query, read_options, handler_data);
    const auto& frame =std::get<PandasOutputFrame>(read_result.frame_data).frame();;

    ASSERT_EQ(frame.row_count(), 2);
    auto val1 = frame.scalar_at<uint32_t>(0, 1);
    ASSERT_EQ(val1, 5);
    auto val2 = frame.scalar_at<uint64_t>(0, 2);
    ASSERT_EQ(val2, 0);
    auto val3 = frame.scalar_at<uint32_t>(1, 1);
    ASSERT_EQ(val3, 0);
    auto val4 = frame.scalar_at<uint64_t>(1, 2);
    ASSERT_EQ(val4, 6);
}

// Just write a dummy StreamDescriptor to the TimeseriesDescriptor to avoid duplicating it
// as the same information is available in the segment header. This is how arcticc behaves so the test is important to
// check compat with existing data.
//
// This functions helps to test that readers look for the StreamDescriptor on the header, not in the
// TimeseriesDescriptor.
//
// Compare with `append_incomplete_segment`. Keep this function even if it duplicates `append_incomplete_segment` so
// that we have protection against `append_incomplete_segment` changing how it writes the descriptors in future.
void append_incomplete_segment_backwards_compat(
        const std::shared_ptr<arcticdb::Store>& store,
        const arcticdb::StreamId& stream_id,
        arcticdb::SegmentInMemory &&seg) {
    using namespace arcticdb::proto::descriptors;
    using namespace arcticdb::stream;

    auto [next_key, total_rows] = read_head(store, stream_id);

    auto start_index = TimeseriesIndex::start_value_for_segment(seg);
    auto end_index = TimeseriesIndex::end_value_for_segment(seg);
    auto seg_row_count = seg.row_count();

    // Dummy StreamDescriptor
    auto desc = stream_descriptor(stream_id, RowCountIndex{}, {});

    auto tsd = arcticdb::make_timeseries_descriptor(
            seg_row_count,
            std::move(desc),
            NormalizationMetadata{},
            std::nullopt,
            std::nullopt,
            std::move(next_key),
            false);

    seg.set_timeseries_descriptor(std::move(tsd));
    auto new_key = store->write(
            arcticdb::stream::KeyType::APPEND_DATA,
            0,
            stream_id,
            start_index,
            end_index,
            std::move(seg)).get();

    total_rows += seg_row_count;
    write_head(store, to_atom(std::move(new_key)), total_rows);
}

TEST_F(SparseTestStore, SimpleRoundtripBackwardsCompat) {
    using namespace arcticdb;
    using namespace arcticdb::stream;
    using DynamicAggregator =  Aggregator<TimeseriesIndex, DynamicSchema, stream::NeverSegmentPolicy, stream::SparseColumnPolicy>;
    using DynamicSinkWrapper = SinkWrapperImpl<DynamicAggregator>;

    const std::string stream_id("test_sparse");

    DynamicSinkWrapper wrapper(stream_id, {});
    auto& aggregator = wrapper.aggregator_;

    aggregator.start_row(timestamp{0})([](auto& rb) {
        rb.set_scalar_by_name("first",  uint32_t(5), DataType::UINT32);
    });

    aggregator.start_row(timestamp{1})([](auto& rb) {
        rb.set_scalar_by_name("second",  uint64_t(6), DataType::UINT64);
    });

    wrapper.aggregator_.commit();

    auto segment = wrapper.segment();
    append_incomplete_segment_backwards_compat(test_store_->_test_get_store(), stream_id, std::move(segment));

    ReadOptions read_options;
    read_options.set_dynamic_schema(true);
    read_options.set_incompletes(true);
    auto read_query = std::make_shared<ReadQuery>();
    read_query->row_filter = universal_range();
    register_native_handler_data_factory();
    auto handler_data = TypeHandlerRegistry::instance()->get_handler_data(OutputFormat::NATIVE);
    auto read_result = test_store_->read_dataframe_version(stream_id, pipelines::VersionQuery{}, read_query, read_options, handler_data);
    const auto& frame =std::get<PandasOutputFrame>(read_result.frame_data).frame();;

    ASSERT_EQ(frame.row_count(), 2);
    auto val1 = frame.scalar_at<uint32_t>(0, 1);
    ASSERT_EQ(val1, 5);
    auto val2 = frame.scalar_at<uint64_t>(0, 2);
    ASSERT_EQ(val2, 0);
    auto val3 = frame.scalar_at<uint32_t>(1, 1);
    ASSERT_EQ(val3, 0);
    auto val4 = frame.scalar_at<uint64_t>(1, 2);
    ASSERT_EQ(val4, 6);
}

TEST_F(SparseTestStore, DenseToSparse) {
    using namespace arcticdb;
    using namespace arcticdb::stream;
    using DynamicAggregator =  Aggregator<TimeseriesIndex, DynamicSchema, stream::NeverSegmentPolicy, stream::SparseColumnPolicy>;
    using DynamicSinkWrapper = SinkWrapperImpl<DynamicAggregator>;

    const std::string stream_id("test_sparse");

    DynamicSinkWrapper wrapper(stream_id, {});
    auto& aggregator = wrapper.aggregator_;

    for(auto i = 0; i < 5; ++i) {
        aggregator.start_row(timestamp{i})([&](auto &rb) {
            rb.set_scalar_by_name("first",  uint32_t(i + 1), DataType::UINT32);
        });
    }

    aggregator.start_row(timestamp{5})([](auto& rb) {
        rb.set_scalar_by_name("second",  uint64_t(6), DataType::UINT64);
    });

    aggregator.start_row(timestamp{6})([](auto& rb) {
        rb.set_scalar_by_name("first",  uint32_t(7), DataType::UINT32);
    });

    wrapper.aggregator_.commit();

    auto segment = wrapper.segment();
    test_store_->append_incomplete_segment(stream_id, std::move(segment));

    ReadOptions read_options;
    read_options.set_dynamic_schema(true);
    read_options.set_incompletes(true);
    auto read_query = std::make_shared<ReadQuery>();
    read_query->row_filter = universal_range();
    register_native_handler_data_factory();
    auto handler_data = TypeHandlerRegistry::instance()->get_handler_data(OutputFormat::NATIVE);
    auto read_result = test_store_->read_dataframe_version(stream_id, pipelines::VersionQuery{}, read_query, read_options, handler_data);
    const auto& frame =std::get<PandasOutputFrame>(read_result.frame_data).frame();;


    ASSERT_EQ(frame.row_count(), 7);
    for(int i = 0; i < 5; ++i) {
        auto val1 = frame.scalar_at<uint32_t>(i, 1);
        ASSERT_EQ(val1, i + 1);
    }
    auto val2 = frame.scalar_at<uint64_t>(5, 2);
    ASSERT_EQ(val2, 6);
    auto val3 = frame.scalar_at<uint32_t>(6, 1);
    ASSERT_EQ(val3, 7);
}

TEST_F(SparseTestStore, SimpleRoundtripStrings) {
    using namespace arcticdb;
    using namespace arcticdb::stream;
    std::optional<ScopedGILLock> scoped_gil_lock;
    register_python_string_types();
    register_python_handler_data_factory();
    using DynamicAggregator =  Aggregator<TimeseriesIndex, DynamicSchema, stream::NeverSegmentPolicy, stream::SparseColumnPolicy>;
    using DynamicSinkWrapper = SinkWrapperImpl<DynamicAggregator>;

    const std::string stream_id("test_sparse");

    DynamicSinkWrapper wrapper(stream_id, {});
    auto& aggregator = wrapper.aggregator_;

    aggregator.start_row(timestamp{0})([](auto& rb) {
        rb.set_scalar_by_name(std::string_view{"first"}, std::string_view{"five"}, DataType::UTF_DYNAMIC64);
    });

    aggregator.start_row(timestamp{1})([](auto& rb) {
        rb.set_scalar_by_name(std::string_view{"second"}, std::string_view{"six"}, DataType::UTF_FIXED64);
    });

    wrapper.aggregator_.commit();

    auto segment = wrapper.segment();
    test_store_->append_incomplete_segment(stream_id, std::move(segment));

    ReadOptions read_options;
    read_options.set_dynamic_schema(true);
    read_options.set_incompletes(true);
    auto read_query = std::make_shared<ReadQuery>();
    read_query->row_filter = universal_range();
    auto handler_data = TypeHandlerRegistry::instance()->get_handler_data(OutputFormat::PANDAS);
    auto read_result = test_store_->read_dataframe_version(stream_id, pipelines::VersionQuery{}, read_query, read_options, handler_data);
    const auto& frame = std::get<PandasOutputFrame>(read_result.frame_data).frame();
    apply_global_refcounts(handler_data, OutputFormat::PANDAS);

    ASSERT_EQ(frame.row_count(), 2);
    auto val1 = frame.scalar_at<PyObject*>(0, 1);
    auto str_wrapper = convert::py_unicode_to_buffer(val1.value(), scoped_gil_lock);
    ASSERT_TRUE(std::holds_alternative<convert::PyStringWrapper>(str_wrapper));
    ASSERT_EQ(strcmp(std::get<convert::PyStringWrapper>(str_wrapper).buffer_, "five"), 0);

    auto val2 = frame.string_at(0, 2);
    ASSERT_EQ(val2.value()[0], char{0});

    auto val3 = frame.scalar_at<PyObject*>(1, 1);
    ASSERT_TRUE(is_py_none(val3.value()));
    auto val4 = frame.string_at(1, 2);
    ASSERT_EQ(val4, "six");
}

TEST_F(SparseTestStore, Multiblock) {
    SKIP_WIN("Works OK but fills up LMDB");
    using namespace arcticdb;
    using namespace arcticdb::stream;
    using DynamicAggregator =  Aggregator<TimeseriesIndex, DynamicSchema, stream::NeverSegmentPolicy, stream::SparseColumnPolicy>;
    using DynamicSinkWrapper = SinkWrapperImpl<DynamicAggregator>;

    const std::string stream_id("test_sparse");

    DynamicSinkWrapper wrapper(stream_id, {});
    auto& aggregator = wrapper.aggregator_;

    constexpr size_t num_rows = 1000000;

    for(size_t i = 0; i < num_rows; i += 2) {
        aggregator.start_row(timestamp(i))([&](auto &rb) {
            rb.set_scalar_by_name(std::string_view{"first"},  uint32_t(i + 1), DataType::UINT32);
        });

        aggregator.start_row(timestamp(i + 1))([&](auto &rb) {
            rb.set_scalar_by_name(std::string_view{"second"},  uint64_t(i + 2), DataType::UINT64);
        });
    }

    wrapper.aggregator_.commit();

    auto segment = wrapper.segment();
    test_store_->append_incomplete_segment(stream_id, std::move(segment));

    ReadOptions read_options;
    read_options.set_dynamic_schema(true);
    read_options.set_incompletes(true);
    auto read_query = std::make_shared<ReadQuery>();
    read_query->row_filter = universal_range();
    register_native_handler_data_factory();
    auto handler_data = TypeHandlerRegistry::instance()->get_handler_data(OutputFormat::NATIVE);
    auto read_result = test_store_->read_dataframe_version(stream_id, pipelines::VersionQuery{}, read_query, read_options, handler_data);
    const auto& frame =std::get<PandasOutputFrame>(read_result.frame_data).frame();;

    for(size_t i = 0; i < num_rows; i += 2) {
        ASSERT_EQ(frame.row_count(), num_rows);
        auto val1 = frame.scalar_at<uint32_t>(i, 1);
        ASSERT_EQ(val1, i + 1);
        auto val2 = frame.scalar_at<uint64_t>(i, 2);
        ASSERT_EQ(val2, 0);
        auto val3 = frame.scalar_at<uint32_t>(i+ 1, 1);
        ASSERT_EQ(val3, 0);
        auto val4 = frame.scalar_at<uint64_t>(i + 1, 2);
        ASSERT_EQ(val4, i + 2);
    }
}

TEST_F(SparseTestStore, Segment) {
    SKIP_WIN("Works OK but fills up LMDB");
    using namespace arcticdb;
    using namespace arcticdb::stream;
    using DynamicAggregator =  Aggregator<TimeseriesIndex, DynamicSchema, stream::RowCountSegmentPolicy, stream::SparseColumnPolicy>;

    const std::string stream_id("test_sparse");
    const auto index = TimeseriesIndex::default_index();
    DynamicSchema schema{
        index.create_stream_descriptor(stream_id, {}), index
    };

    DynamicAggregator aggregator(std::move(schema), [&](SegmentInMemory &&segment) {
        test_store_->append_incomplete_segment(stream_id, std::move(segment));
    }, RowCountSegmentPolicy{1000});

    constexpr size_t num_rows = 1000000;

    for(size_t i = 0; i < num_rows; i += 2) {
        aggregator.start_row(timestamp(i))([&](auto &rb) {
            rb.set_scalar_by_name(std::string_view{"first"},  uint32_t(i + 1), DataType::UINT32);
        });

        aggregator.start_row(timestamp(i + 1))([&](auto &rb) {
            rb.set_scalar_by_name(std::string_view{"second"},  uint64_t(i + 2), DataType::UINT64);
        });
    }

    aggregator.commit();

    ReadOptions read_options;
    read_options.set_dynamic_schema(true);
    read_options.set_incompletes(true);
    auto read_query = std::make_shared<ReadQuery>();
    read_query->row_filter = universal_range();
    register_native_handler_data_factory();
    auto handler_data = TypeHandlerRegistry::instance()->get_handler_data(OutputFormat::NATIVE);
    auto read_result = test_store_->read_dataframe_version(stream_id, pipelines::VersionQuery{}, read_query, read_options, handler_data);
    const auto& frame =std::get<PandasOutputFrame>(read_result.frame_data).frame();;

    for(size_t i = 0; i < num_rows; i += 2) {
        ASSERT_EQ(frame.row_count(), num_rows);
        auto val1 = frame.scalar_at<uint32_t>(i, 1);
        ASSERT_EQ(val1, i + 1);
        auto val2 = frame.scalar_at<uint64_t>(i, 2);
        ASSERT_EQ(val2, 0);
        auto val3 = frame.scalar_at<uint32_t>(i+ 1, 1);
        ASSERT_EQ(val3, 0);
        auto val4 = frame.scalar_at<uint64_t>(i + 1, 2);
        ASSERT_EQ(val4, i + 2);
    }
}

TEST_F(SparseTestStore, SegmentWithExistingIndex) {
    using namespace arcticdb;
    using namespace arcticdb::stream;
    using DynamicAggregator =  Aggregator<TimeseriesIndex, DynamicSchema, stream::RowCountSegmentPolicy, stream::SparseColumnPolicy>;

    const std::string stream_id("test_sparse");

    const auto index = TimeseriesIndex::default_index();
    DynamicSchema schema{
        index.create_stream_descriptor(stream_id, {}), index
    };

    bool written = false;
    DynamicAggregator aggregator(std::move(schema), [&](SegmentInMemory &&segment) {
        if(!written) {
            test_store_->write_individual_segment(stream_id, std::move(segment), false);
            written = true;
        }
        else {
            test_store_->append_incomplete_segment(stream_id, std::move(segment));
        }
    }, RowCountSegmentPolicy{1000});

    constexpr size_t num_rows = 100000;

    for(size_t i = 0; i < num_rows; i += 2) {
        aggregator.start_row(timestamp(i))([&](auto &rb) {
            rb.set_scalar_by_name(std::string_view{"first"},  uint32_t(i + 1), DataType::UINT32);
        });

        aggregator.start_row(timestamp(i + 1))([&](auto &rb) {
            rb.set_scalar_by_name(std::string_view{"second"},  uint64_t(i + 2), DataType::UINT64);
        });
    }

    aggregator.commit();

    ReadOptions read_options;
    read_options.set_dynamic_schema(true);
    read_options.set_incompletes(true);
    auto read_query = std::make_shared<ReadQuery>();
    read_query->row_filter = universal_range();
    register_native_handler_data_factory();
    auto handler_data = TypeHandlerRegistry::instance()->get_handler_data(OutputFormat::NATIVE);
    auto read_result = test_store_->read_dataframe_version(stream_id, pipelines::VersionQuery{}, read_query, read_options, handler_data);
    const auto& frame =std::get<PandasOutputFrame>(read_result.frame_data).frame();;

    ASSERT_EQ(frame.row_count(), num_rows);
    for(size_t i = 0; i < num_rows; i += 2) {
        auto val1 = frame.scalar_at<uint32_t>(i, 1);
        check_value(val1, i + 1);
        auto val2 = frame.scalar_at<uint64_t>(i, 2);
        check_value(val2, 0);
        auto val3 = frame.scalar_at<uint32_t>(i+ 1, 1);
        check_value(val3, 0);
        auto val4 = frame.scalar_at<uint64_t>(i + 1, 2);
        check_value(val4, i + 2);
    }
}

TEST_F(SparseTestStore, SegmentAndFilterColumn) {
    using namespace arcticdb;
    using namespace arcticdb::stream;
    using DynamicAggregator =  Aggregator<TimeseriesIndex, DynamicSchema, stream::RowCountSegmentPolicy, stream::SparseColumnPolicy>;

    const std::string stream_id("test_sparse");

    const auto index = TimeseriesIndex::default_index();
    DynamicSchema schema{
        index.create_stream_descriptor(stream_id, {}), index
    };

    bool written = false;
    DynamicAggregator aggregator(std::move(schema), [&](SegmentInMemory &&segment) {
        if(!written) {
            test_store_->write_individual_segment(stream_id, std::move(segment), false);
            written = true;
        }
        else {
            test_store_->append_incomplete_segment(stream_id, std::move(segment));
        }
    }, RowCountSegmentPolicy{1000});

    constexpr size_t num_rows = 100000;

    for(size_t i = 0; i < num_rows; i += 2) {
        aggregator.start_row(timestamp(i))([&](auto &rb) {
            rb.set_scalar_by_name(std::string_view{"first"},  uint32_t(i + 1), DataType::UINT32);
        });

        aggregator.start_row(timestamp(i + 1))([&](auto &rb) {
            rb.set_scalar_by_name(std::string_view{"second"},  uint64_t(i + 2), DataType::UINT64);
        });
    }

    aggregator.commit();

    ReadOptions read_options;
    read_options.set_dynamic_schema(true);
    read_options.set_incompletes(true);
    auto read_query = std::make_shared<ReadQuery>();
    read_query->columns = {"time", "first"};
    read_query->row_filter = universal_range();
    register_native_handler_data_factory();
    auto handler_data = TypeHandlerRegistry::instance()->get_handler_data(OutputFormat::NATIVE);
    auto read_result = test_store_->read_dataframe_version(stream_id, pipelines::VersionQuery{}, read_query, read_options, handler_data);
    const auto& frame =std::get<PandasOutputFrame>(read_result.frame_data).frame();;
    ASSERT_EQ(frame.row_count(), num_rows);
    ASSERT_EQ(frame.descriptor().field_count(), 2);

    for(size_t i = 0; i < num_rows; i += 2) {
        auto val1 = frame.scalar_at<uint32_t>(i, 1);
        ASSERT_EQ(val1, i + 1);
        auto val3 = frame.scalar_at<uint32_t>(i+ 1, 1);
        ASSERT_EQ(val3, 0);
    }
}

TEST_F(SparseTestStore, SegmentWithRangeFilter) {
    using namespace arcticdb;
    using namespace arcticdb::stream;
    using DynamicAggregator =  Aggregator<TimeseriesIndex, DynamicSchema, stream::RowCountSegmentPolicy, stream::SparseColumnPolicy>;

    const std::string stream_id("test_sparse");
    const auto index = TimeseriesIndex::default_index();
    DynamicSchema schema{
        index.create_stream_descriptor(stream_id, {}), index
    };

    bool written = false;
    DynamicAggregator aggregator(std::move(schema), [&](SegmentInMemory &&segment) {
        if(!written) {
            test_store_->write_individual_segment(stream_id, std::move(segment), false);
            written = true;
        }
        else {
            test_store_->append_incomplete_segment(stream_id, std::move(segment));
        }
    }, RowCountSegmentPolicy{1000});

    constexpr size_t num_rows = 10000;

    for(size_t i = 0; i < num_rows; i += 2) {
        aggregator.start_row(timestamp(i))([&](auto &rb) {
            rb.set_scalar_by_name(std::string_view{"first"},  uint32_t(i + 1), DataType::UINT32);
        });

        aggregator.start_row(timestamp(i + 1))([&](auto &rb) {
            rb.set_scalar_by_name(std::string_view{"second"},  uint64_t(i + 2), DataType::UINT64);
        });
    }

    aggregator.commit();

    ReadOptions read_options;
    read_options.set_dynamic_schema(true);
    read_options.set_incompletes(true);
    auto read_query = std::make_shared<ReadQuery>();
    read_query->row_filter = IndexRange(timestamp{3000}, timestamp{6999});
    register_native_handler_data_factory();
    auto handler_data = TypeHandlerRegistry::instance()->get_handler_data(OutputFormat::NATIVE);
    auto read_result = test_store_->read_dataframe_version(stream_id, pipelines::VersionQuery{}, read_query, read_options, handler_data);
    const auto& frame =std::get<PandasOutputFrame>(read_result.frame_data).frame();;

    ASSERT_EQ(frame.row_count(), 4000);
    for(size_t i = 0; i < frame.row_count(); i += 2) {
        auto val1 = frame.scalar_at<uint32_t>(i, 1);
        ASSERT_EQ(val1, i + 3001);
        auto val2 = frame.scalar_at<uint64_t>(i, 2);
        ASSERT_EQ(val2, 0);
        auto val3 = frame.scalar_at<uint32_t>(i+ 1, 1);
        ASSERT_EQ(val3, 0);
        auto val4 = frame.scalar_at<uint64_t>(i + 1, 2);
        ASSERT_EQ(val4, i + 3002);
    }
}

TEST_F(SparseTestStore, Compact) {
    using namespace arcticdb;
    using namespace arcticdb::stream;
    using DynamicAggregator =  Aggregator<TimeseriesIndex, DynamicSchema, stream::RowCountSegmentPolicy, stream::SparseColumnPolicy>;

    const std::string stream_id("test_sparse");
    const auto index = TimeseriesIndex::default_index();
    DynamicSchema schema{
        index.create_stream_descriptor(stream_id, {}), index
    };

    DynamicAggregator aggregator(std::move(schema), [&](SegmentInMemory &&segment) {
        ASSERT_TRUE(segment.is_index_sorted());
        segment.descriptor().set_sorted(SortedValue::ASCENDING);
        test_store_->append_incomplete_segment(stream_id, std::move(segment));
    }, RowCountSegmentPolicy{10});

    constexpr size_t num_rows = 100;

    for(size_t i = 0; i < num_rows; i += 2) {
        aggregator.start_row(timestamp(i))([&](auto &rb) {
            rb.set_scalar_by_name(std::string_view{"first"}, uint32_t(i + 1), DataType::UINT32);
        });

        aggregator.start_row(timestamp(i + 1))([&](auto &rb) {
            rb.set_scalar_by_name(std::string_view{"second"}, uint64_t(i + 2), DataType::UINT64);
        });
    }

    aggregator.commit();
    test_store_->compact_incomplete(stream_id, false, false, false, true);

    ReadOptions read_options;
    read_options.set_dynamic_schema(true);
    auto read_query = std::make_shared<ReadQuery>();
    read_query->row_filter = universal_range();
    register_native_handler_data_factory();
    auto handler_data = TypeHandlerRegistry::instance()->get_handler_data(OutputFormat::NATIVE);
    auto read_result = test_store_->read_dataframe_version(stream_id, pipelines::VersionQuery{}, read_query, read_options, handler_data);
    const auto& frame = std::get<PandasOutputFrame>(read_result.frame_data).frame();

    ASSERT_EQ(frame.row_count(), num_rows);
    for(size_t i = 0; i < num_rows; i += 2) {
        auto val1 = frame.scalar_at<uint32_t>(i, 1);
        ASSERT_EQ(val1, i + 1);
        auto val2 = frame.scalar_at<uint64_t>(i, 2);
        ASSERT_EQ(val2, 0);
        auto val3 = frame.scalar_at<uint32_t>(i+ 1, 1);
        ASSERT_EQ(val3, 0);
        auto val4 = frame.scalar_at<uint64_t>(i + 1, 2);
        ASSERT_EQ(val4, i + 2);
    }
}

TEST_F(SparseTestStore, CompactWithStrings) {
    using namespace arcticdb;
    using namespace arcticdb::stream;
    using DynamicAggregator =  Aggregator<TimeseriesIndex, DynamicSchema, stream::RowCountSegmentPolicy, stream::SparseColumnPolicy>;

    register_python_string_types();
    register_python_handler_data_factory();
    const std::string stream_id("test_sparse");

    const auto index = TimeseriesIndex::default_index();
    DynamicSchema schema{
        index.create_stream_descriptor(stream_id, {}), index
    };

    DynamicAggregator aggregator(std::move(schema), [&](SegmentInMemory &&segment) {
        ASSERT_TRUE(segment.is_index_sorted());
        segment.descriptor().set_sorted(SortedValue::ASCENDING);
        test_store_->append_incomplete_segment(stream_id, std::move(segment));
    }, RowCountSegmentPolicy{10});

    constexpr size_t num_rows = 100;

    for(size_t i = 0; i < num_rows; i += 2) {
        aggregator.start_row(timestamp(i))([&](auto &rb) {
            auto val = fmt::format("{}", i + 1);
            rb.set_scalar_by_name(std::string_view{"first"}, std::string_view{val}, DataType::UTF_DYNAMIC64);
        });

        aggregator.start_row(timestamp(i + 1))([&](auto &rb) {
            auto val = fmt::format("{}", i + 2);
            rb.set_scalar_by_name(std::string_view{"second"}, std::string_view{val}, DataType::UTF_FIXED64);
        });
    }

    aggregator.commit();
    test_store_->compact_incomplete(stream_id, false, false, false, true);

    ReadOptions read_options;
    read_options.set_dynamic_schema(true);
    auto read_query = std::make_shared<ReadQuery>();
    read_query->row_filter = universal_range();
    auto handler_data = TypeHandlerRegistry::instance()->get_handler_data(OutputFormat::PANDAS);
    auto read_result = test_store_->read_dataframe_version(stream_id, VersionQuery{}, read_query, read_options, handler_data);
    apply_global_refcounts(handler_data, OutputFormat::PANDAS);
    const auto& frame = std::get<PandasOutputFrame>(read_result.frame_data).frame();
    ASSERT_EQ(frame.row_count(), num_rows);

    for(size_t i = 0; i < num_rows; i += 2) {
        auto val1 = frame.scalar_at<PyObject*>(i, 1);
        std::optional<ScopedGILLock> scoped_gil_lock;
        auto str_wrapper = convert::py_unicode_to_buffer(val1.value(), scoped_gil_lock);
        auto expected = fmt::format("{}", i + 1);
        ASSERT_TRUE(std::holds_alternative<convert::PyStringWrapper>(str_wrapper));
        ASSERT_EQ(strcmp(std::get<convert::PyStringWrapper>(str_wrapper).buffer_, expected.data()), 0);

        auto val2 = frame.string_at(i, 2);
        ASSERT_EQ(val2.value()[0], char{0});

        auto val3 = frame.scalar_at<PyObject*>(i + 1, 1);
        ASSERT_TRUE(is_py_none(val3.value()));

        auto val4 = frame.string_at(i + 1, 2);

        expected = fmt::format("{}", i + 2);
        ASSERT_EQ(strncmp(val4.value().data(), expected.data(), expected.size()), 0);
    }
}
