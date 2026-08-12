#pragma once

#include <cstdint>
#include <mutex>
#include <list>
#include <unordered_set>
#include <unordered_map>

namespace tinydb {
    using frame_id_t = uint32_t;
    constexpr frame_id_t INVALID_FRAME_ID = static_cast<frame_id_t>(-1);
    class Replacer {
        public:
            virtual ~Replacer() = default;
            Replacer() = default;
            explicit Replacer(size_t promotion_threshold_) : promotion_threshold_(promotion_threshold_){}
            virtual void RecordAccess(frame_id_t frame_id);

            // Pin/UnPin are a boolean toggle, not a reference count: a frame is
            // either evictable or it isn't. If a frame can have multiple
            // concurrent pinners, the caller (e.g. a buffer pool manager) is
            // responsible for tracking its own pin count and calling Pin only on
            // the 0->1 transition and UnPin only on the 1->0 transition.
            // Calling UnPin while another pinner still considers the frame
            // pinned will make it evictable out from under that pinner.
            virtual void Pin(frame_id_t frame_id);
            virtual void UnPin(frame_id_t frame_id);
            virtual bool Victim(frame_id_t *frame_id);
            virtual size_t Size() const;

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