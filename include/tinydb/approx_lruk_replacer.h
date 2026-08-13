#pragma once

#include "tinydb/replacer.h"
#include <list>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace tinydb {
    // Segmented-LRU-ish eviction policy -- "Approximate LRU-K" using an old/young
    // split.
    class ApproxLRUKReplacer : public Replacer {
        public:
            ApproxLRUKReplacer() = default;
            explicit ApproxLRUKReplacer(size_t promotion_threshold) : promotion_threshold_(promotion_threshold) {}
            ~ApproxLRUKReplacer() override = default;

            void RecordAccess(frame_id_t frame_id) override;
            void Pin(frame_id_t frame_id) override;
            void UnPin(frame_id_t frame_id) override;
            bool Victim(frame_id_t *frame_id) override;
            bool Remove(frame_id_t frame_id) override;
            size_t Size() const override;

        private:
            mutable std::mutex mutex_;
            std::list<frame_id_t> young_;
            std::list<frame_id_t> old_;
            std::unordered_map<frame_id_t, std::list<frame_id_t>::iterator> young_map_;
            std::unordered_map<frame_id_t, std::list<frame_id_t>::iterator> old_map_;
            std::unordered_map<frame_id_t, size_t> old_entry_count_;
            std::unordered_set<frame_id_t> evictable_;
            size_t global_access_count_ = 0;
            size_t promotion_threshold_ = 200;
    };
}
