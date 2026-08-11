#pragma once

#include <string>
#include <memory>
#include "page.h"

namespace tinydb {
    class IOBackend {
        public:
            virtual ~IOBackend() = default;
            virtual void ReadPage(page_id_t page_id, Page& page) = 0;
            virtual void WritePage(page_id_t page_id, const Page& page) = 0;
            virtual void Sync() = 0;
            virtual uint64_t SizeInPages() const = 0;
            virtual void GrowTo(uint64_t num_pages) = 0;
    };

    enum class IOStrategy {
        K_DIRECT,
        K_BUFFERED
    };

    class IOBackendFactory {
        public:
            static std::unique_ptr<IOBackend> Create(IOStrategy strategy, const std::string& db_file_path);
    };
}