/* Copyright 2023 Man Group Operations Limited
 *
 * Use of this software is governed by the Business Source License 1.1 included in the file licenses/BSL.txt.
 *
 * As of the Change Date specified in that file, in accordance with the Business Source License, use of this software will be governed by the Apache License, version 2.0.
 */

#pragma once

#include <arcticdb/storage/storage_factory.hpp>
#include <arcticdb/storage/open_mode.hpp>
#include <arcticdb/entity/performance_tracing.hpp>
#include <arcticdb/util/composite.hpp>
#include <arcticdb/util/configs_map.hpp>
#include <arcticdb/storage/single_file_storage.hpp>

#include <memory>
#include <vector>

namespace arcticdb::storage {

/*
 * The Storages class abstracts over multiple physical stores and controls how ArcticDB read and write to multiple
 * ones.
 *
 * Possible future use-case for Storages:
 *  - Disaster recovery for Storages: if the first storage fails we can fall back to the second one, etc.
 *  - Tiered storage: recent data goes to a fast, comparatively expensive storage and then is gradually moved
 *  into a slower, cheaper one.
 */
class Storages {
  public:
    Storages(const Storages&) = delete;
    Storages(Storages&&) = default;
    Storages& operator=(const Storages&) = delete;
    Storages& operator=(Storages&&) = delete;

    using StorageVector = std::vector<std::shared_ptr<Storage>>;

    Storages(StorageVector&& storages, OpenMode mode) :
        storages_(std::move(storages)), mode_(mode) {
    }

    void write(Composite<KeySegmentPair>&& kvs) {
        ARCTICDB_SAMPLE(StoragesWrite, 0)
        primary().write(std::move(kvs));
    }

    void write_if_none(KeySegmentPair&& kv) {
        primary().write_if_none(std::move(kv));
    }

    void update(Composite<KeySegmentPair>&& kvs, storage::UpdateOpts opts) {
        ARCTICDB_SAMPLE(StoragesUpdate, 0)
        primary().update(std::move(kvs), opts);
    }

    bool supports_prefix_matching() const {
        return primary().supports_prefix_matching();
    }

    bool supports_atomic_writes() const {
        return primary().supports_atomic_writes();
    }

    bool fast_delete() {
        return primary().fast_delete();
    }

    void cleanup() {
        primary().cleanup();
    }

    bool key_exists(const VariantKey& key) {
        return primary().key_exists(key);
    }

    bool is_path_valid(const std::string_view path) const {
        return primary().is_path_valid(path);
    }

    auto read(Composite<VariantKey>&& ks, const ReadVisitor& visitor, ReadKeyOpts opts, bool primary_only=true) {
        ARCTICDB_RUNTIME_SAMPLE(StoragesRead, 0)
        if(primary_only)
            return primary().read(std::move(ks), visitor, opts);

        if(auto rg = ks.as_range(); !std::all_of(std::begin(rg), std::end(rg), [] (const auto& vk) {
            return variant_key_type(vk) == KeyType::TABLE_DATA;
        })) {
            return primary().read(std::move(ks), visitor, opts);
        }

        for(const auto& storage : storages_) {
            try {
                return storage->read(std::move(ks), visitor, opts);
            } catch (typename storage::KeyNotFoundException& ex) {
                ARCTICDB_DEBUG(log::version(), "Keys not found in storage, continuing to next storage");
                ks = std::move(ex.keys());
            }
        }
        throw storage::KeyNotFoundException(std::move(ks));
    }

    void iterate_type(KeyType key_type, const IterateTypeVisitor& visitor, const std::string &prefix=std::string{}, bool primary_only=true) {
        ARCTICDB_SAMPLE(StoragesIterateType, RMTSF_Aggregate)
        if(primary_only) {
            primary().iterate_type(key_type, visitor, prefix);
        } else {
            for(const auto& storage : storages_) {
                storage->iterate_type(key_type, visitor, prefix);
            }
        }
    }

    bool scan_for_matching_key(KeyType key_type, const IterateTypePredicate& predicate, bool primary_only=true) {
        if (primary_only) {
            return primary().scan_for_matching_key(key_type, predicate);
        }

        return std::any_of(std::begin(storages_), std::end(storages_),
            [key_type, &predicate](const auto& storage) {
               return storage->scan_for_matching_key(key_type, predicate);
            });
    }

    /** Calls Storage::do_key_path on the primary storage. Remember to check the open mode. */
    std::string key_path(const VariantKey& key) const {
        return primary().key_path(key);
    }

    void remove(Composite<VariantKey>&& ks, storage::RemoveOpts opts) {
        primary().remove(std::move(ks), opts);
    }

    [[nodiscard]] OpenMode open_mode() const { return mode_; }

    void move_storage(KeyType key_type, timestamp horizon, size_t storage_index = 0) {
        util::check(storage_index + 1 < storages_.size(), "Cannot move from storage {} to storage {} as only {} storages defined");
        auto& source = *storages_[storage_index];
        auto& target = *storages_[storage_index + 1];

        const IterateTypeVisitor& visitor = [&source, &target, horizon] (VariantKey &&vk) {
            auto key = std::forward<VariantKey>(vk);
            if (to_atom(key).creation_ts() < horizon) {
                try {
                    auto key_seg = source.read(VariantKey{key}, ReadKeyOpts{});
                    target.write(std::move(key_seg));
                    source.remove(std::move(key), storage::RemoveOpts{});
                } catch (const std::exception& ex) {
                    log::storage().warn("Failed to move key to next storage: {}", ex.what());
                }
            } else {
                ARCTICDB_DEBUG(log::storage(), "Not moving key {} as it is too recent", key);
            }
        };

        source.iterate_type(key_type, visitor);
   }
    std::optional<std::shared_ptr<SingleFileStorage>> get_single_file_storage() const {
        if (dynamic_cast<SingleFileStorage*>(storages_[0].get()) != nullptr) {
            return std::dynamic_pointer_cast<SingleFileStorage>(storages_[0]);
        } else {
            return std::nullopt;
        }
    }
    std::string name() const {
        return primary().name();
    }

  private:
    Storage& primary() {
        util::check(!storages_.empty(), "No storages configured");
        return *storages_[0];
    }

    const Storage& primary() const {
        util::check(!storages_.empty(), "No storages configured");
        return *storages_[0];
    }

    std::vector<std::shared_ptr<Storage>> storages_;
    OpenMode mode_;
};

inline std::shared_ptr<Storages> create_storages(const LibraryPath& library_path,OpenMode mode, decltype(std::declval<arcticc::pb2::storage_pb2::LibraryConfig>().storage_by_id())& storage_configs, const NativeVariantStorage& native_storage_config) {
    static_assert(std::is_const_v<std::remove_reference_t<decltype(storage_configs)>>);
    Storages::StorageVector storages;
    for (const auto& [storage_id, storage_config]: storage_configs) {
        util::variant_match(native_storage_config.variant(),
            [&storage_config, &storages, &library_path, mode] (const s3::S3Settings& settings) {
                util::check(storage_config.config().Is<arcticdb::proto::s3_storage::Config>(), "Only support S3 native settings");
                arcticdb::proto::s3_storage::Config s3_storage;
                storage_config.config().UnpackTo(&s3_storage);
                storages.push_back(create_storage(library_path, mode, s3::S3Settings(settings).update(s3_storage)));
            },
            [&storage_config, &storages, &library_path, mode](const auto &) {
                storages.push_back(create_storage(library_path, mode, storage_config));
            }
        );
    }
    return std::make_shared<Storages>(std::move(storages), mode);
}

inline std::shared_ptr<Storages> create_storages(const LibraryPath& library_path, OpenMode mode, const std::vector<arcticdb::proto::storage::VariantStorage> &storage_configs) {
    Storages::StorageVector storages;
    for (const auto& storage_config: storage_configs) {
        storages.push_back(create_storage(library_path, mode, storage_config));
    }
    return std::make_shared<Storages>(std::move(storages), mode);
}

} //namespace arcticdb::storage
