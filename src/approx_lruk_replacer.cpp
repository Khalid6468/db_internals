#include "tinydb/approx_lruk_replacer.h"

namespace tinydb {

    void ApproxLRUKReplacer::RecordAccess(frame_id_t frame_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        global_access_count_++;
        if(!young_map_.contains(frame_id) && !old_map_.contains(frame_id)) {
            old_.push_front(frame_id);
            old_map_[frame_id] = old_.begin();
            old_entry_count_[frame_id] = global_access_count_;
        } else if(young_map_.contains(frame_id)) {
            std::list<frame_id_t>::iterator it = young_map_[frame_id];
            young_.erase(it);
            young_.push_front(frame_id);
            young_map_[frame_id] = young_.begin();
        } else {
            size_t delta = global_access_count_ - old_entry_count_[frame_id];
            if(delta >= promotion_threshold_) {
                std::list<frame_id_t>::iterator it = old_map_[frame_id];
                old_.erase(it);
                old_map_.erase(frame_id);
                old_entry_count_.erase(frame_id);
                young_.push_front(frame_id);
                young_map_[frame_id] = young_.begin();
            }
        }
    }

    void ApproxLRUKReplacer::Pin(frame_id_t frame_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        evictable_.erase(frame_id);
    }

    void ApproxLRUKReplacer::UnPin(frame_id_t frame_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        evictable_.insert(frame_id);
    }

    bool ApproxLRUKReplacer::Victim(frame_id_t* frame_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::list<frame_id_t>::reverse_iterator it = old_.rbegin();
        while(it != old_.rend() && !evictable_.contains(*it)) {
            it = next(it);
        }
        if(it != old_.rend()) {
            *frame_id = *it;
            old_map_.erase(*it);
            old_entry_count_.erase(*it);
            evictable_.erase(*it);
            old_.erase(next(it).base());
            return true;
        } else {
            it = young_.rbegin();
            while(it != young_.rend() && !evictable_.contains(*it)) {
                it = next(it);
            }
            if(it != young_.rend()) {
                *frame_id = *it;
                young_map_.erase(*it);
                evictable_.erase(*it);
                young_.erase(next(it).base());
                return true;
            }
        }

        return false;

    }

    bool ApproxLRUKReplacer::Remove(frame_id_t frame_id) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto old_it = old_map_.find(frame_id);
        if (old_it != old_map_.end()) {
            old_.erase(old_it->second);
            old_map_.erase(old_it);
            old_entry_count_.erase(frame_id);
            evictable_.erase(frame_id);
            return true;
        }
        auto young_it = young_map_.find(frame_id);
        if (young_it != young_map_.end()) {
            young_.erase(young_it->second);
            young_map_.erase(young_it);
            evictable_.erase(frame_id);
            return true;
        }
        return false;
    }

    size_t ApproxLRUKReplacer::Size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return evictable_.size();
    }
}
