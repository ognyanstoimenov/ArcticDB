#pragma once

#include <arcticdb/util/preconditions.hpp>
#include <arcticdb/util/pb_util.hpp>
#include <arcticdb/log/log.hpp>
#include <arcticdb/util/buffer_pool.hpp>

#include <google/protobuf/io/zero_copy_stream_impl_lite.h>
#include <folly/gen/Base.h>
#include <arcticdb/storage/object_store_utils.hpp>
#include <arcticdb/storage/storage_options.hpp>
#include <arcticdb/storage/storage_utils.hpp>
#include <arcticdb/storage/s3/s3_client_wrapper.hpp>
#include <arcticdb/entity/serialized_key.hpp>
#include <arcticdb/util/exponential_backoff.hpp>
#include <arcticdb/util/configs_map.hpp>
#include <arcticdb/util/composite.hpp>

#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/PutObjectRequest.h>
#include <aws/s3/model/DeleteObjectRequest.h>
#include <aws/s3/model/DeleteObjectsRequest.h>
#include <aws/s3/model/ListObjectsV2Request.h>
#include <aws/s3/model/HeadObjectRequest.h>
#include <aws/s3/model/Object.h>
#include <aws/s3/model/Delete.h>
#include <aws/s3/model/ObjectIdentifier.h>

#include <boost/interprocess/streams/bufferstream.hpp>

#undef GetMessage

namespace arcticdb::storage {

using namespace object_store_utils;

namespace s3 {

    namespace fg = folly::gen;
    namespace detail {

        static const size_t DELETE_OBJECTS_LIMIT = 1000;

        template<class It>
        using Range = folly::Range<It>;

        template<class KeyBucketizer>
        void do_write_impl(
                Composite<KeySegmentPair> &&kvs,
                const std::string &root_folder,
                const std::string &bucket_name,
                S3ClientWrapper &s3_client,
                KeyBucketizer &&bucketizer) {
            ARCTICDB_SAMPLE(S3StorageWrite, 0)
            auto fmt_db = [](auto &&kv) { return kv.key_type(); };

            (fg::from(kvs.as_range()) | fg::move | fg::groupBy(fmt_db)).foreach(
                    [&s3_client, &bucket_name, &root_folder, b = std::move(bucketizer)](auto &&group) {
                        auto key_type_dir = key_type_folder(root_folder, group.key());
                        ARCTICDB_TRACE(log::storage(), "S3 key_type_folder is {}", key_type_dir);

                        ARCTICDB_SUBSAMPLE(S3StorageWriteValues, 0)
                        for (auto &kv: group.values()) {
                            auto &k = kv.variant_key();
                            auto s3_object_name = object_path(b.bucketize(key_type_dir, k), k);
                            auto &seg = kv.segment();

                            auto put_object_result = s3_client.put_object(s3_object_name, std::move(seg), bucket_name);

                            if (!put_object_result.is_success()) {
                                auto &error = put_object_result.get_error();
                                util::raise_rte("Failed to write s3 with key '{}' {}: {}",
                                                k,
                                                error.GetExceptionName().c_str(),
                                                error.GetMessage().c_str());
                            }
                        }
                    });
        }

        template<class KeyBucketizer>
        void do_update_impl(
                Composite<KeySegmentPair> &&kvs,
                const std::string &root_folder,
                const std::string &bucket_name,
                S3ClientWrapper &s3_client,
                KeyBucketizer &&bucketizer) {
            // s3 updates the key if it already exists (our buckets don't have versioning)
            do_write_impl(std::move(kvs), root_folder, bucket_name, s3_client, std::move(bucketizer));
        }

        class UnexpectedS3ErrorException : public std::exception {
        public:
            UnexpectedS3ErrorException(const std::string& message):message(message){}
            const char* what() const noexcept override {
                return message.c_str();
            }
        private:
            std::string message;
        };

        inline bool is_expected_error_type(Aws::S3::S3Errors err) {
            return err == Aws::S3::S3Errors::NO_SUCH_KEY
                   || err == Aws::S3::S3Errors::NO_SUCH_BUCKET
                   || err == Aws::S3::S3Errors::INVALID_ACCESS_KEY_ID
                   || err == Aws::S3::S3Errors::ACCESS_DENIED
                   || err == Aws::S3::S3Errors::RESOURCE_NOT_FOUND;
        }

        inline void raise_if_unexpected_error(const Aws::S3::S3Error& err){
            if (!is_expected_error_type(err.GetErrorType())) {
                std::string error_message;
                // We create a more detailed error explanation in case of NETWORK_CONNECTION errors to remedy #880.
                if (err.GetErrorType() == Aws::S3::S3Errors::NETWORK_CONNECTION){
                    error_message = fmt::format("Got an unexpected network error: {}. "
                                                "This could be due to a connectivity issue or too many open Arctic instances. "
                                                "Having more than one open Arctic instance is not advised, you should reuse them. "
                                                "If you absolutely need many open Arctic instances, consider increasing `ulimit -n`.",
                                                err.GetMessage().c_str());
                }
                else {
                    error_message = fmt::format("Got unexpected error: '{}' {}: {}",
                                                int(err.GetErrorType()),
                                                err.GetExceptionName().c_str(),
                                                err.GetMessage().c_str());
                }

                log::storage().error(error_message);
                throw UnexpectedS3ErrorException(error_message);
            }
        }

        template<class KeyBucketizer>
        void do_read_impl(Composite<VariantKey> &&ks,
                          const ReadVisitor &visitor,
                          const std::string &root_folder,
                          const std::string &bucket_name,
                          const S3ClientWrapper &s3_client,
                          KeyBucketizer &&bucketizer,
                          ReadKeyOpts opts) {
            ARCTICDB_SAMPLE(S3StorageRead, 0)
            auto fmt_db = [](auto &&k) { return variant_key_type(k); };
            std::vector<VariantKey> failed_reads;

            (fg::from(ks.as_range()) | fg::move | fg::groupBy(fmt_db)).foreach(
                    [&s3_client, &bucket_name, &root_folder, b = std::move(bucketizer), &visitor, &failed_reads,
                            opts = opts](auto &&group) {

                        for (auto &k: group.values()) {
                            auto key_type_dir = key_type_folder(root_folder, variant_key_type(k));
                            auto s3_object_name = object_path(b.bucketize(key_type_dir, k), k);

                            auto get_object_result = s3_client.get_object(
                                    s3_object_name,
                                    bucket_name);

                            if (get_object_result.is_success()) {
                                ARCTICDB_SUBSAMPLE(S3StorageVisitSegment, 0)

                                visitor(k, std::move(get_object_result.get_output()));

                                ARCTICDB_DEBUG(log::storage(), "Read key {}: {}", variant_key_type(k),
                                               variant_key_view(k));
                            } else {
                                auto &error = get_object_result.get_error();
                                raise_if_unexpected_error(error);

                                log::storage().log(
                                    opts.dont_warn_about_missing_key ? spdlog::level::debug : spdlog::level::warn,
                                    "Failed to find segment for key '{}' {}: {}",
                                    variant_key_view(k),
                                    error.GetExceptionName().c_str(),
                                    error.GetMessage().c_str());

                                failed_reads.push_back(k);
                            }
                        }
                    });
            if (!failed_reads.empty())
                throw KeyNotFoundException(Composite<VariantKey>{std::move(failed_reads)});
        }

        template<class KeyBucketizer>
        void do_remove_impl(Composite<VariantKey> &&ks,
                            const std::string &root_folder,
                            const std::string &bucket_name,
                            S3ClientWrapper &s3_client,
                            KeyBucketizer &&bucketizer) {
            ARCTICDB_SUBSAMPLE(S3StorageDeleteBatch, 0)
            auto fmt_db = [](auto &&k) { return variant_key_type(k); };
            std::vector<std::string> to_delete;
            std::vector<VariantKey> failed_deletes;
            static const size_t delete_object_limit =
                    std::min(DELETE_OBJECTS_LIMIT,
                             static_cast<size_t>(ConfigsMap::instance()->get_int("S3Storage.DeleteBatchSize", 1000)));

            (fg::from(ks.as_range()) | fg::move | fg::groupBy(fmt_db)).foreach(
                    [&s3_client, &root_folder, &bucket_name, &to_delete, b = std::move(
                            bucketizer), &failed_deletes](auto &&group) {
                        auto key_type_dir = key_type_folder(root_folder, group.key());
                        for (auto k: folly::enumerate(group.values())) {
                            auto s3_object_name = object_path(b.bucketize(key_type_dir, *k), *k);
                            to_delete.emplace_back(std::move(s3_object_name));

                            if (to_delete.size() == delete_object_limit || k.index + 1 == group.size()) {
                                auto delete_object_result = s3_client.delete_objects(to_delete, bucket_name);
                                if (delete_object_result.is_success()) {
                                    ARCTICDB_RUNTIME_DEBUG(log::storage(), "Deleted object with key '{}'",
                                                           variant_key_view(*k));
                                } else {
                                    // TODO: Fix error handling. The failed deletes should be managed in the success case.
                                    // This is the case for an entire batch request failing (e.g. due to network error)
                                    for (auto& bad_key: delete_object_result.get_output().failed_deletes) {
                                        auto bad_key_name = bad_key.substr(key_type_dir.size(),
                                                                                    std::string::npos);
                                        failed_deletes.emplace_back(
                                                from_tokenized_variant_key(
                                                        reinterpret_cast<const uint8_t *>(bad_key_name.data()),
                                                        bad_key_name.size(), group.key()));
                                    }
                                }
                                to_delete.clear();
                            }
                        }
                    });

            util::check(to_delete.empty(), "Have {} segment that have not been removed",
                        to_delete.size());
            if (!failed_deletes.empty())
                throw KeyNotFoundException(Composite<VariantKey>(std::move(failed_deletes)));
        }

        inline auto default_prefix_handler() {
            return [](const std::string &prefix, const std::string &key_type_dir, const KeyDescriptor &key_descriptor,
                      KeyType) {
                return !prefix.empty() ? fmt::format("{}/{}*{}", key_type_dir, key_descriptor, prefix) : key_type_dir;
            };
        }

        template<class Visitor, class KeyBucketizer, class PrefixHandler>
        void do_iterate_type_impl(KeyType key_type,
                                  Visitor &&visitor,
                                  const std::string &root_folder,
                                  const std::string &bucket_name,
                                  const S3ClientWrapper &s3_client,
                                  KeyBucketizer &&bucketizer,
                                  PrefixHandler &&prefix_handler = default_prefix_handler(),
                                  const std::string &prefix = std::string{}
        ) {
            ARCTICDB_SAMPLE(S3StorageIterateType, 0)
            auto key_type_dir = key_type_folder(root_folder, key_type);

            // Generally we get the key descriptor from the AtomKey, but in the case of iterating version journals
            // where we want to have a narrower prefix, we can use the info that it's a version journal and derive
            // the Descriptor.
            // TODO: Set the IndexDescriptor correctly
            KeyDescriptor key_descriptor(prefix,
                                         is_ref_key_class(key_type) ? IndexDescriptor::UNKNOWN
                                                                    : IndexDescriptor::TIMESTAMP,
                                         FormatType::TOKENIZED);
            auto key_prefix = prefix_handler(prefix, key_type_dir, key_descriptor, key_type);
            ARCTICDB_RUNTIME_DEBUG(log::storage(), "Searching for objects in bucket {} with prefix {}", bucket_name,
                                   key_prefix);

            auto continuation_token = std::optional<std::string>();
            do {
                auto list_objects_result = s3_client.list_objects(key_prefix, bucket_name, continuation_token);

                if (list_objects_result.is_success()) {
                    auto& output = list_objects_result.get_output();

                    const auto root_folder_size = key_type_dir.size() + 1 + bucketizer.bucketize_length(key_type);
                    ARCTICDB_RUNTIME_DEBUG(log::storage(), "Received object list");

                    for (auto& s3_object_name: output.s3_object_names) {
                        auto key = s3_object_name.substr(root_folder_size);
                        ARCTICDB_TRACE(log::version(), "Got object_list: {}, key: {}", s3_object_name, key);
                        auto k = variant_key_from_bytes(
                                reinterpret_cast<uint8_t *>(key.data()),
                                key.size(),
                                key_type);

                        ARCTICDB_DEBUG(log::storage(), "Iterating key {}: {}", variant_key_type(k),
                                       variant_key_view(k));
                        ARCTICDB_SUBSAMPLE(S3StorageVisitKey, 0)
                        visitor(std::move(k));
                        ARCTICDB_SUBSAMPLE(S3StorageCursorNext, 0)
                    }

                    continuation_token = output.next_continuation_token;
                } else {
                    // TODO: We should raise here if we encounter an unexpected error, because otherwise
                    // we'll likely return incomplete results
                    const auto &error = list_objects_result.get_error();
                    log::storage().warn("Failed to iterate key type with key '{}' {}: {}",
                                        key_type,
                                        error.GetExceptionName().c_str(),
                                        error.GetMessage().c_str());
                    return;
                }
            } while (continuation_token.has_value());
        }

        template<class KeyBucketizer>
        bool do_key_exists_impl(
                const VariantKey &key,
                const std::string &root_folder,
                const std::string &bucket_name,
                const S3ClientWrapper &s3_client,
                KeyBucketizer &&b
        ) {
            auto key_type_dir = key_type_folder(root_folder, variant_key_type(key));
            auto s3_object_name = object_path(b.bucketize(key_type_dir, key), key);

            auto head_object_result = s3_client.head_object(
                    s3_object_name,
                    bucket_name);

            if (!head_object_result.is_success()) {
                auto &error = head_object_result.get_error();
                raise_if_unexpected_error(error);

                ARCTICDB_DEBUG(log::storage(), "Head object returned false for key {} {} {}:{}",
                               variant_key_view(key),
                               int(error.GetErrorType()),
                               error.GetExceptionName().c_str(),
                               error.GetMessage().c_str());
            }

            return head_object_result.is_success();
        }

        } // namespace detail
    } // namespace s3
} // namespace arcticdb::storage