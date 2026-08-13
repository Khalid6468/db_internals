#include "tinydb/buffer_pool_manager.h"
#include "tinydb/replacer.h"
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

namespace tinydb {

    BufferPoolManager::BufferPoolManager(const std::string& base_directory, IOStrategy strategy, size_t pool_size, std::unique_ptr<Replacer> replacer)
    : storage_manager_(base_directory, strategy) {
        replacer_ = std::move(replacer);
        pool_size_ = pool_size;
        pages_ = std::vector<Page>(pool_size_);
        pin_count_ = std::vector<size_t>(pool_size_);
        is_dirty_ = std::vector<bool>(pool_size_);
        frame_to_page_ = std::vector<PageAddress>(pool_size_);
        for (frame_id_t i = 0; i < pool_size_; ++i) { 
            free_list_.push_back(i); 
        }
    }

    Page* BufferPoolManager::FetchPage(PageAddress addr) {

        std::lock_guard<std::mutex> lock(mutex_);
        page_id_t page_id = addr.page_id;
        frame_id_t frame_id = INVALID_FRAME_ID;
        if(page_table_.contains(addr)) {
            frame_id = page_table_[addr];
            size_t current_pin_count = pin_count_[frame_id]++;
            if(current_pin_count == 0) {
                replacer_->Pin(frame_id);
            }
            replacer_->RecordAccess(frame_id);
            if(pages_[frame_id].GetPageId() != page_id) {
                throw std::runtime_error("[BufferPoolManager::FetchPage] page_table_ points at frame " +
                    std::to_string(frame_id) + " for page " + std::to_string(page_id) +
                    ", but that frame holds page " + std::to_string(pages_[frame_id].GetPageId()) +
                    " -- page table/frame contents are out of sync");
            }
        } else {
            if (free_list_.size() > 0) { 
                frame_id = free_list_.front();
                free_list_.pop_front();
            }
            if (frame_id == INVALID_FRAME_ID) {
                bool frame_evicted = replacer_->Victim(&frame_id);
                if(!frame_evicted) {
                    throw std::runtime_error("[BufferPoolManager::FetchPage] Couldn't fetch page " +
                        std::to_string(page_id) + ": no free frame and every frame is pinned");
                } else {
                    if(is_dirty_[frame_id]) {
                        storage_manager_.WritePage(frame_to_page_[frame_id],pages_[frame_id]);
                    }
                    page_table_.erase(frame_to_page_[frame_id]);
                }
            }
            is_dirty_[frame_id] = false;
            storage_manager_.ReadPage(addr, pages_[frame_id]);
            if(page_id != pages_[frame_id].GetPageId()) {
                throw std::runtime_error("[BufferPoolManager::FetchPage] Something is wrong, the page_id of the page loaded and requested dont match");
            }
            page_table_[addr] = frame_id;
            frame_to_page_[frame_id] = addr;
            pin_count_[frame_id] = 1;
            replacer_->Pin(frame_id);
            replacer_->RecordAccess(frame_id);
        }
        return &pages_[frame_id];
    }


    bool BufferPoolManager::UnPinPage(PageAddress addr, bool is_dirty) {
        std::lock_guard<std::mutex> lock(mutex_);
        page_id_t page_id = addr.page_id;
        if(!page_table_.contains(addr)) {
            throw std::runtime_error("[BufferPoolManager::UnPinPage] Couldn't find an entry for the page with id " + std::to_string(page_id) + " in page_table_");
        }
        frame_id_t frame_id = page_table_[addr];
        if(pin_count_[frame_id] == 0) {
            throw std::runtime_error("[BufferPoolManager::UnPinPage] Pin count for the page with id " + std::to_string(page_id) + " is already 0, Can't UnPinPage");
        } 
        pin_count_[frame_id]--;
        if (pin_count_[frame_id] == 0) {
            replacer_->UnPin(frame_id);
        }
        if(is_dirty) {
            is_dirty_[frame_id] = true;
        }
        return true;
    }

    bool BufferPoolManager::FlushPage(PageAddress addr) {
        std::lock_guard<std::mutex> lock(mutex_);
        frame_id_t frame_id;
        if(page_table_.contains(addr)) {
            frame_id = page_table_[addr];
            if(is_dirty_[frame_id]) {
                storage_manager_.WritePage(addr, pages_[frame_id]);
            }
            is_dirty_[frame_id] = false;
            return true;
        } else {
            return false;
        }
    }

    Page* BufferPoolManager::NewPage(PageAddress* addr) {
        std::lock_guard<std::mutex> lock(mutex_);
        frame_id_t frame_id = INVALID_FRAME_ID;
        file_id_t file_id = addr->file_id;
        if(free_list_.size() > 0) {
            frame_id = free_list_.front();
            free_list_.pop_front();
        }
        if(frame_id == INVALID_FRAME_ID) {
            if(!replacer_->Victim(&frame_id)) {
                throw std::runtime_error("[BufferPoolManager::NewPage] Couldn't create a new page as all of the frames are pinned");
            }
            if(is_dirty_[frame_id]) {
                storage_manager_.WritePage(frame_to_page_[frame_id], pages_[frame_id]);
            }
            page_table_.erase(frame_to_page_[frame_id]);
        }
        page_id_t page_id = storage_manager_.AllocatePage(file_id);
        std::memset(pages_[frame_id].raw(), 0, Page::size());
        pages_[frame_id].SetPageId(page_id);
       
        pin_count_[frame_id] = 1;
        replacer_->Pin(frame_id);
        is_dirty_[frame_id] = false;
        PageAddress newPageAddr = PageAddress{file_id, page_id};
        frame_to_page_[frame_id] = newPageAddr;
        page_table_[newPageAddr] = frame_id;
        *addr = newPageAddr;
        replacer_->RecordAccess(frame_id);
        return &pages_[frame_id];
    }

    bool BufferPoolManager::DeletePage(PageAddress addr) {
        std::lock_guard<std::mutex> lock(mutex_);
        frame_id_t frame_id = INVALID_FRAME_ID;
        if(page_table_.contains(addr)) {
            frame_id = page_table_[addr];
            if(pin_count_[frame_id] > 0) {
                return false;
            }
            is_dirty_[frame_id] = false;
            free_list_.push_back(frame_id);
        } else {
            throw std::runtime_error("[BufferPoolManager::DeletePage] Couldn't find the page in page_table to delete");
        }
        storage_manager_.DeallocatePage(addr);
        page_table_.erase(addr);
        replacer_->Remove(frame_id);
        return true;
    }
}