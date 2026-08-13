
#include <vector>
#include <unordered_map>
#include "tinydb/page.h"
#include "tinydb/replacer.h"
#include "tinydb/storage_manager.h"
#include "tinydb/disk_manager.h"
#include <mutex>
#include <list>
#include <memory>


namespace tinydb {
    class BufferPoolManager {
        private:
            std::vector<Page> pages_;
            std::vector<size_t> pin_count_;
            std::vector<bool> is_dirty_;
            std::vector<PageAddress> frame_to_page_;
            std::unordered_map<PageAddress, frame_id_t, PageAddressHasher> page_table_;
            std::list<frame_id_t> free_list_;
            std::unique_ptr<Replacer> replacer_;
            StorageManager storage_manager_;
            mutable std::mutex mutex_;
            size_t pool_size_;
        
        public:
            explicit BufferPoolManager(const std::string& base_directory, IOStrategy strategy, size_t pool_size, std::unique_ptr<Replacer> replacer);
            BufferPoolManager& operator=(const BufferPoolManager&) = delete;
            Page* FetchPage(PageAddress page_id);
            bool UnPinPage(PageAddress page_id, bool is_dirty);
            bool FlushPage(PageAddress page_id);
            Page* NewPage(PageAddress* page_id);
            bool DeletePage(PageAddress page_id);
    };
}