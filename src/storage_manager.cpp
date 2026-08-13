#include "tinydb/disk_manager.h"
#include "tinydb/io_backend.h"
#include "tinydb/storage_manager.h"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <sys/resource.h>


namespace tinydb {

    namespace {
        size_t OsFileDescriptorLimit() {
            struct rlimit rl{};
            if (getrlimit(RLIMIT_NOFILE, &rl) != 0) {
                throw std::runtime_error(
                    "[StorageManager] getrlimit(RLIMIT_NOFILE) failed: " + std::string(std::strerror(errno)));
            }
            return static_cast<size_t>(rl.rlim_cur);
        }
    }

    StorageManager::StorageManager(const std::string& base_directory, IOStrategy io_strategy, size_t max_open_files)
        : base_directory_(base_directory), strategy_(io_strategy) {
        size_t os_limit = OsFileDescriptorLimit();
        max_open_files_ = (max_open_files == 0) ? os_limit : std::min(max_open_files, os_limit);
    }

    std::string StorageManager::PathForFile(file_id_t file_id) const {
        return base_directory_ + "/" + std::to_string(file_id) + ".tdb";
    }

    std::shared_ptr<DiskManager> StorageManager::GetOrOpen(file_id_t file_id) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (auto it = disk_managers_.find(file_id); it != disk_managers_.end()) {
            return it->second;
        }

        if (disk_managers_.size() >= max_open_files_) {
            auto victim = disk_managers_.end();
            for (auto it = disk_managers_.begin(); it != disk_managers_.end(); ++it) {
                if (it->second.use_count() == 1) {
                    victim = it;
                    break;
                }
            }
            if (victim == disk_managers_.end()) {
                throw std::runtime_error(
                    "[StorageManager::GetOrOpen] at max_open_files_ (" + std::to_string(max_open_files_) +
                    ") and every open file is currently in use -- cannot open file_id " + std::to_string(file_id));
            }
            disk_managers_.erase(victim);
        }

        std::string db_file_path = PathForFile(file_id);
        auto dm = std::make_shared<DiskManager>(db_file_path, strategy_);
        disk_managers_[file_id] = dm;
        return dm;
    }

    // ReadPage/WritePage/AllocatePage/DeallocatePage intentionally hold no lock of
    // their own beyond what GetOrOpen already takes: GetOrOpen's lock only needs to
    // protect disk_managers_ itself (map insertion/eviction), and each DiskManager
    // already guards its own internal state independently. Wrapping these in
    // mutex_ too would both self-deadlock (mutex_ isn't reentrant, and GetOrOpen
    // locks it internally) and needlessly serialize unrelated files' I/O against
    // each other. Holding our own shared_ptr copy for the call's duration is what
    // keeps the DiskManager alive even if another thread evicts it from the map
    // concurrently.

    void StorageManager::ReadPage(const PageAddress& addr, Page& page) {
        auto dm = GetOrOpen(addr.file_id);
        dm->ReadPage(addr.page_id, page);
        if(page.GetPageId() != addr.page_id) {
            throw std::runtime_error("[StorageManager::ReadPage] Read page with id "
                + std::to_string(page.GetPageId())
                + " but was supposed to be page with id "
                + std::to_string(addr.page_id));
        }
    }

    void StorageManager::WritePage(const PageAddress& addr, const Page& page) {
        if(addr.page_id != page.GetPageId()) {
            throw std::runtime_error("[StorageManager::WritePage] page id from addr = "
                + std::to_string(addr.page_id)
                + " Page id from page is "
                + std::to_string(page.GetPageId())
            );
        }
        auto dm = GetOrOpen(addr.file_id);
        dm->WritePage(page);
    }

    page_id_t StorageManager::AllocatePage(file_id_t file_id) {
        auto dm = GetOrOpen(file_id);
        return dm->AllocatePage();
    }

    void StorageManager::DeallocatePage(const PageAddress& addr) {
        auto dm = GetOrOpen(addr.file_id);
        dm->DeallocatePage(addr.page_id);
    }

    void StorageManager::HoldFileOpenForTesting(file_id_t file_id, const std::function<void()>& on_acquired,
        std::shared_future<void> release) {
        auto dm = GetOrOpen(file_id);
        if (on_acquired) {
            on_acquired();
        }
        release.wait();
    }

}
