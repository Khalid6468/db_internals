#pragma once
#include <vector>
#include <mutex>
#include "io_backend.h"


namespace tinydb {

    class DiskManager {
        private:
            std::unique_ptr<IOBackend> io_backend_;
            std::vector<page_id_t> free_pages_;
            std::vector<bool> is_free_;
            mutable std::mutex mutex_;
            page_id_t next_page_id_;

        public:
            explicit DiskManager(const std::string& db_file_path, IOStrategy strategy);
            DiskManager(const DiskManager&) = delete;
            DiskManager& operator=(const DiskManager&) = delete;

            void ReadPage(page_id_t page_id, Page& page);
            void WritePage(const Page& page);
            page_id_t AllocatePage();
            void DeallocatePage(page_id_t page_id);
            void Sync() { io_backend_ -> Sync(); };
            uint64_t NumPagesOnDisk() const { return io_backend_ -> SizeInPages(); }
            uint64_t NumFreePages() const { return free_pages_.size(); }

    };
}