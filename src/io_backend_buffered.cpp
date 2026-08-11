#include "tinydb/io_backend_buffered.h"
#include "tinydb/page.h"
#include <cstring>
#include <stdexcept>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <cerrno>
#include <cstring>


namespace tinydb {
    BufferedIOBackend::BufferedIOBackend(const std::string& file_path): db_file_path_(file_path) {
        fd_ = open(db_file_path_.c_str(), O_CREAT | O_RDWR, 0644);
        if (fd_ < 0) {
            throw std::runtime_error("BufferedIOBackend::BufferedIOBackend - Failed to open the given file path " + file_path + " error: " + std::strerror(errno));
        }
    }

    BufferedIOBackend::~BufferedIOBackend() {
        if(fd_ >= 0) {
            close(fd_);
        }
    }

    void BufferedIOBackend::ReadPage(page_id_t page_id, Page& page) {
        uint64_t offset = static_cast<uint32_t>(page_id) * PAGE_SIZE;

        size_t total = 0;

        // retry incase of EINTR due to interrupt signals
        while (total < PAGE_SIZE) {
            ssize_t n = pread(fd_, page.raw() + total, PAGE_SIZE - total, offset + total);
            if (n < 0) {
                if (errno == EINTR) continue;   // no progress lost, retry the whole remaining call
                
                throw std::runtime_error("BufferedIOBackend::ReadPage - Failed to read page via pread " 
                    + std::string(std::strerror(errno)));   // a real error
            }
            if (n == 0) {
                throw std::runtime_error("BufferedIOBackend::ReadPage - EOF before reading " + std::to_string(PAGE_SIZE) + " bytes");   // EOF before PAGE_SIZE bytes -- genuine corruption/short file
            }
            total += static_cast<size_t>(n);
        }

    }

    void BufferedIOBackend::WritePage(page_id_t page_id, const Page& page) {
        off_t offset = static_cast<off_t>(page_id) * PAGE_SIZE;
        
        size_t total = 0;
        // retry incase of EINTR due to interrupt signals
        while (total < PAGE_SIZE) {
            ssize_t n = pwrite(fd_, page.raw() + total, PAGE_SIZE - total, offset + total);
            if (n < 0) {
                if (errno == EINTR) continue;   // no progress lost, retry the whole remaining call
                
                throw std::runtime_error("BufferedIOBackend::WritePage - Failed to write page via pwrite " 
                    + std::string(std::strerror(errno)));   // a real error
            }
            if (n == 0) {
                throw std::runtime_error("BufferedIOBackend::WritePage - EOF before writing " + std::to_string(PAGE_SIZE) + " bytes");   // EOF before PAGE_SIZE bytes -- genuine corruption/short file
            }
            total += static_cast<size_t>(n);
        }
        
    }
     
    void BufferedIOBackend::Sync() {

        while (fsync(fd_) != 0) {
            if (errno == EINTR) continue;
            throw std::runtime_error("BufferedIOBackend::Sync: fsync failed: " +
                                      std::string(std::strerror(errno)));
        }
    
    }
     
    uint64_t BufferedIOBackend::SizeInPages() const {
        struct stat st{};

        
        if (fstat(fd_, &st) != 0) {
            throw std::runtime_error("BufferedIOBackend::SizeInPages: fstat failed: " +
                                      std::string(std::strerror(errno)));
        }
        return static_cast<uint64_t>(st.st_size) / PAGE_SIZE;
    }
     
    void BufferedIOBackend::GrowTo(uint64_t num_pages) {

        if (num_pages < SizeInPages()) {
            throw std::runtime_error("BufferedIOBackend::GrowTo - trying to shrink the file: " 
                + std::to_string(num_pages) 
                + " are given which is less the current pages: " 
                + std::to_string(SizeInPages()));
        }

        off_t target_size = static_cast<off_t>(num_pages) * PAGE_SIZE;
        
        while (ftruncate(fd_, target_size) != 0) {
            // retry in case of interrupts(non-fatal)
            if (errno == EINTR) continue;
            throw std::runtime_error("BufferedIOBackend::GrowTo: ftruncate failed: " +
                                      std::string(std::strerror(errno)));
        }
    }
}