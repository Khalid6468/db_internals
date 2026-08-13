
#include <vector>
#include <unordered_map>
#include "tinydb/page.h"
#include "tinydb/replacer.h"
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
            std::vector<page_id_t> frame_to_page_;
            std::unordered_map<page_id_t, frame_id_t> page_table_;
            std::list<frame_id_t> free_list_;
            std::unique_ptr<Replacer> replacer_;
            DiskManager disk_manager_;
            mutable std::mutex mutex_;
            size_t pool_size_;
        
        public:
            explicit BufferPoolManager(const std::string& db_file_path, IOStrategy strategy, size_t pool_size, std::unique_ptr<Replacer> replacer);
            BufferPoolManager& operator=(const BufferPoolManager&) = delete;
            Page* FetchPage(page_id_t page_id);
            bool UnPinPage(page_id_t page_id, bool is_dirty);
            bool FlushPage(page_id_t page_id);
            Page* NewPage(page_id_t* page_id);
            bool DeletePage(page_id_t page_id);
    };
}