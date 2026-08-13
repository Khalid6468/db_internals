#pragma once

#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <cstdint>
#include "tinydb/page.h"
#include "tinydb/disk_manager.h"
#include "tinydb/io_backend.h"



namespace tinydb {
    using file_id_t = uint32_t;

    struct PageAddress {
        file_id_t file_id;
        page_id_t page_id;
        bool operator==(const PageAddress&) const = default;
    };

    struct PageAddressHasher {
        std::size_t operator()(const PageAddress& addr) const {
            std::size_t h1 = std::hash<file_id_t>{}(addr.file_id);
            std::size_t h2 = std::hash<page_id_t>{}(addr.page_id);
            // boost::hash_combine-style mixing -- plain h1 ^ (h2 << 1) collapses
            // to near-identical hashes for small, densely-packed ids like these.
            return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
        }
    };

    class StorageManager {
        private:
            // disk_managers_ is a bounded cache (capped at max_open_files_, itself
            // capped at the OS's actual fd limit): once at capacity, GetOrOpen evicts
            // an entry with no other outstanding holders (shared_ptr::use_count() == 1)
            // to make room. shared_ptr, not unique_ptr, is what makes this safe under
            // concurrent access -- a DiskManager stays alive until every caller still
            // using it (holding their own shared_ptr copy from a prior GetOrOpen call)
            // is done with it, even after its map entry is erased. Erasing a raw
            // reference/unique_ptr instead would risk a use-after-free on a fd another
            // thread is mid-operation on.
            std::unordered_map<file_id_t, std::shared_ptr<DiskManager>> disk_managers_;
            std::mutex mutex_;
            std::string base_directory_;
            IOStrategy strategy_;
            size_t max_open_files_;

            std::shared_ptr<DiskManager> GetOrOpen(file_id_t file_id);

        public:
            // max_open_files: 0 (default) means "no cap beyond the OS's own limit".
            // Whatever is requested is clamped to the OS's actual RLIMIT_NOFILE, so
            // this can never be configured to a value that would defeat the point of
            // having a cap at all.
            explicit StorageManager(const std::string& base_directory, IOStrategy io_strategy, size_t max_open_files = 0);
            StorageManager(const StorageManager&) = delete;
            StorageManager& operator=(const StorageManager&) = delete;
            void ReadPage(const PageAddress& addr, Page& page);
            void WritePage(const PageAddress& addr, const Page& page);
            page_id_t AllocatePage(file_id_t file_id);
            void DeallocatePage(const PageAddress& addr);
            std::string PathForFile(file_id_t file_id) const;

            // Test-only. Opens (or reuses) file_id's DiskManager, calls on_acquired
            // once the shared_ptr is held (so a test can synchronize precisely on
            // "this file is now busy" instead of guessing with a sleep), then blocks
            // until release becomes ready before letting the shared_ptr go. Exists
            // because the production API alone can't deterministically create a
            // use_count() > 1 window: every real method only holds its shared_ptr for
            // the duration of one synchronous call, so there's no way to exercise
            // GetOrOpen's no-idle-victim-available throw path without this.
            void HoldFileOpenForTesting(file_id_t file_id, const std::function<void()>& on_acquired,
                std::shared_future<void> release);
    };
}
