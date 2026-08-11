#include "tinydb/io_backend.h"
#include "tinydb/io_backend_buffered.h"
#include "tinydb/io_backend_direct.h"

namespace tinydb {
    std::unique_ptr<IOBackend> IOBackendFactory::Create(IOStrategy strategy, const std::string& db_file_path) {
        switch(strategy) {
            case tinydb::IOStrategy::K_BUFFERED:
                return std::make_unique<BufferedIOBackend>(db_file_path);
            case tinydb::IOStrategy::K_DIRECT:
                return std::make_unique<DirectIOBackend>(db_file_path);
        }

        throw std::runtime_error("Unknown IOStrategy provided");
    }
}