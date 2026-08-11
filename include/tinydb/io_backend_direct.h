#pragma once

#include "io_backend.h"

namespace tinydb {
    class DirectIOBackend: public IOBackend {
        private:
            std::string db_file_path_;
            int fd_;
        public:
            explicit DirectIOBackend(const std::string& db_file_path);
            ~DirectIOBackend() override;
            DirectIOBackend(const DirectIOBackend&) = delete;
            DirectIOBackend& operator=(const DirectIOBackend&) = delete;
            void ReadPage(page_id_t page_id, Page& page) override;
            void WritePage(page_id_t page_id, const Page& page) override;
            void Sync() override;   
            uint64_t SizeInPages() const override;
            void GrowTo(uint64_t num_pages) override;
    };
}