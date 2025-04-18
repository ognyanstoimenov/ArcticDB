/* Copyright 2023 Man Group Operations Limited
 *
 * Use of this software is governed by the Business Source License 1.1 included in the file licenses/BSL.txt.
 *
 * As of the Change Date specified in that file, in accordance with the Business Source License, use of this software will be governed by the Apache License, version 2.0.
 */

#pragma once

#include <aws/s3/S3Client.h>

#include <arcticdb/storage/s3/s3_client_interface.hpp>
#include <arcticdb/storage/s3/s3_client_impl.hpp>

#include <arcticdb/util/preconditions.hpp>
#include <arcticdb/util/pb_util.hpp>
#include <arcticdb/log/log.hpp>
#include <arcticdb/util/buffer_pool.hpp>

#include <arcticdb/storage/object_store_utils.hpp>
#include <arcticdb/storage/storage_utils.hpp>
#include <arcticdb/entity/serialized_key.hpp>
#include <arcticdb/util/configs_map.hpp>
#include <arcticdb/util/composite.hpp>

namespace arcticdb::storage::s3 {

// A wrapper around the actual S3 client which can simulate failures based on the configuration.
// The S3ClientTestWrapper delegates to the real client by default, but can intercept operations
// to simulate failures or track operations for testing purposes.
class S3ClientTestWrapper : public S3ClientInterface {
public:
    explicit S3ClientTestWrapper(std::unique_ptr<S3ClientInterface> actual_client) : 
        actual_client_(std::move(actual_client)) {
    }

    ~S3ClientTestWrapper() override = default; 

    [[nodiscard]] S3Result<std::monostate> head_object(
        const std::string& s3_object_name,
        const std::string& bucket_name) const override;

    [[nodiscard]] S3Result<Segment> get_object(
        const std::string& s3_object_name,
        const std::string& bucket_name) const override;

    [[nodiscard]] folly::Future<S3Result<Segment>> get_object_async(
        const std::string& s3_object_name,
        const std::string& bucket_name) const override;

    S3Result<std::monostate> put_object(
        const std::string& s3_object_name,
        Segment& segment,
        const std::string& bucket_name,
        PutHeader header = PutHeader::NONE) override;

    S3Result<DeleteObjectsOutput> delete_objects(
        const std::vector<std::string>& s3_object_names,
        const std::string& bucket_name) override;

    folly::Future<S3Result<std::monostate>> delete_object(
        const std::string& s3_object_names,
        const std::string& bucket_name) override;

    S3Result<ListObjectsOutput> list_objects(
        const std::string& prefix,
        const std::string& bucket_name,
        const std::optional<std::string>& continuation_token) const override;

private:
    // Returns error if failures are enabled for the given bucket
    std::optional<Aws::S3::S3Error> has_failure_trigger(const std::string& bucket_name) const;

    std::unique_ptr<S3ClientInterface> actual_client_;
};

}
