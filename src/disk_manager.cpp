
#include "tinydb/disk_manager.h"
#include <stdexcept>

namespace tinydb {
    DiskManager::DiskManager(const std::string& file_path, IOStrategy strategy)
        :  io_backend_(IOBackendFactory::Create(strategy, file_path)) {
            next_page_id_ = static_cast<page_id_t>(io_backend_->SizeInPages());
            is_free_.resize(next_page_id_);
    }

    void DiskManager::ReadPage(page_id_t page_id, Page& page) {
        io_backend_ -> ReadPage(page_id, page);
        if(page.GetPageId() != page_id) {
            throw std::runtime_error(
                "DiskManager::ReadPage: page id mismatch, expected " +
                std::to_string(page_id) + " but page header says " +
                std::to_string(page.GetPageId()) +
                " -- likely corruption or an offset bug");
        } 
    }

    void DiskManager::WritePage(const Page& page) {
        io_backend_ -> WritePage(page.GetPageId(), page);
    }

    page_id_t DiskManager::AllocatePage() {
        page_id_t id;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if(!free_pages_.empty()) {
                id = free_pages_.back();
                if(is_free_[id]) {
                    free_pages_.pop_back();
                    is_free_[id] = false;
                } else {
                    throw std::runtime_error("Something is wrong during page allocation, free_pages_ and is_free_ aren't consistent");
                }
            } else {
                id = next_page_id_++;
                is_free_.push_back(false);
                io_backend_ -> GrowTo(next_page_id_);
            }
        }

        Page page;
        // memset'ing the page bytes, as the cost of this compared to the whole page getting written to disk is negligible
        std::memset(page.raw(), 0, Page::size());
        page.SetPageId(id);
        io_backend_ -> WritePage(id, page);
        return id;

    }

    void DiskManager::DeallocatePage(page_id_t page_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(is_free_.size() <= page_id) {
            throw std::runtime_error("page id is greater than the number of entries in is_free_");
        }
        if(!is_free_[page_id]) {
            free_pages_.push_back(page_id);
            is_free_[page_id] = true;
        } else {
            throw std::runtime_error("Something is wrong during page deallocation, seems like the page is already deallocated");
        }
    }


}