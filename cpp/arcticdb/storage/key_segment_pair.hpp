/* Copyright 2023 Man Group Operations Limited
 *
 * Use of this software is governed by the Business Source License 1.1 included in the file licenses/BSL.txt.
 *
 * As of the Change Date specified in that file, in accordance with the Business Source License, use of this software will be governed by the Apache License, version 2.0.
 */

#pragma once

#include <arcticdb/entity/key.hpp>
#include <arcticdb/codec/segment.hpp>

namespace arcticdb::storage {
    using namespace entity;

    /*
     * KeySegmentPair contains compressed data as returned from storage. Unlike FrameSlice, this does
     * not contain any positioning information for the contained data.
     */
    class KeySegmentPair {
    public:
        // TODO tidy up these millions of constructors
        KeySegmentPair() : key_(std::make_shared<VariantKey>()), segment_(std::make_shared<Segment>()) {}
        explicit KeySegmentPair(VariantKey &&key)
        : key_(std::make_shared<VariantKey>(std::move(key))),
          segment_(std::make_shared<Segment>()) {}

        KeySegmentPair(VariantKey &&key, Segment&& segment)
      : key_(std::make_shared<VariantKey>(std::move(key))),
        segment_(std::make_shared<Segment>(std::move(segment))) {}

        KeySegmentPair(VariantKey&& key, std::shared_ptr<Segment> segment)
        : key_(std::make_shared<VariantKey>(std::move(key))),
        segment_(segment)
        {}

        KeySegmentPair(const VariantKey& key, Segment&& segment)
        : key_(std::make_shared<VariantKey>(key)), segment_(std::make_shared<Segment>(std::move(segment))) {}

        ARCTICDB_MOVE_COPY_DEFAULT(KeySegmentPair)

        Segment& segment() {
            // TODO this is dangerous
            return *segment_;
        }

        std::shared_ptr<Segment> segment_ptr() {
          return segment_;
        }

        // TODO is there really any point to this API?
        std::shared_ptr<Segment> release_segment() {
            auto tmp = segment_;
            segment_ = std::make_shared<Segment>();
            return tmp;
        }

        [[nodiscard]] const AtomKey &atom_key() const {
            util::check(std::holds_alternative<AtomKey>(variant_key()), "Expected atom key access");
            return std::get<AtomKey>(variant_key());
        }

        [[nodiscard]] const RefKey& ref_key() const {
            util::check(std::holds_alternative<RefKey>(variant_key()), "Expected ref key access");
            return std::get<RefKey>(variant_key());
        }

        template<typename T>
        void set_key(T&& key) {
          key_ = std::make_shared<VariantKey>(key);
        }

        [[nodiscard]] const VariantKey& variant_key() const {
            return *key_;
        }

        [[nodiscard]] const Segment& segment() const {
            return *segment_;
        }

        [[nodiscard]] bool has_segment() const {
            return !segment().is_empty();
        }

        [[nodiscard]] std::string_view key_view() const {
            return variant_key_view(variant_key());
        }

        [[nodiscard]] KeyType key_type() const {
            return variant_key_type(variant_key());
        }

    private:
        std::shared_ptr<VariantKey> key_;
        std::shared_ptr<Segment> segment_;
    };

    template<class T, std::enable_if_t<std::is_same_v<T, EnvironmentName> || std::is_same_v<T, StorageName>, int> = 0>
    bool operator==(const T &l, const T &r) {
        return l.value == r.value;
    }



} //namespace arcticdb