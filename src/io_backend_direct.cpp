#include "tinydb/io_backend_direct.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdexcept>
#include <cerrno>
#include <cstring>


namespace tinydb {

    // O_DIRECT requires the buffer address, transfer length, and file offset of
    // every request to be a multiple of the underlying block device's alignment
    // requirement. 4096 covers both classic 512-byte sectors and modern 4Kn
    // drives, and matches Page's own alignas(4096) buffer.
    constexpr size_t DIRECT_IO_ALIGNMENT = 4096;

    DirectIOBackend::DirectIOBackend(const std::string& path) : db_file_path_(path) {
        #ifdef __APPLE__
            // macOS does not have O_DIRECT; open the file normally
            fd_ = open(path.c_str(), O_RDWR | O_CREAT, 0644);
        #else
            fd_ = open(path.c_str(), O_RDWR | O_CREAT | O_DIRECT, 0644);
        #endif

        if (fd_ < 0) {
            throw std::runtime_error(
                "DirectIOBackend: open(" + path + ", O_DIRECT) failed: " +
                std::string(std::strerror(errno)) +
                " -- the underlying filesystem may not support O_DIRECT "
                "(common on overlay/tmpfs filesystems used by containers)");
        }

        #ifdef __APPLE__
            if (fcntl(fd_, F_NOCACHE, 1) == -1) {
                perror("Failed to set F_NOCACHE on macOS");
                close(fd_);
                fd_ = -1;
            }
        #endif
    }
    
    DirectIOBackend::~DirectIOBackend() {
        if (fd_ >= 0) close(fd_);
    }
    
    void DirectIOBackend::ReadPage(page_id_t page_id, Page& page) {
        off_t offset = static_cast<off_t>(page_id) * PAGE_SIZE;
        size_t total = 0;

        // retry incase of EINTR due to interrupt signals
        while (total < PAGE_SIZE) {
            ssize_t n = pread(fd_, page.raw() + total, PAGE_SIZE - total, offset + total);
            if (n < 0) {
                if (errno == EINTR) continue;   // no progress lost, retry the whole remaining call

                throw std::runtime_error("DirectIOBackend::ReadPage - Failed to read page via pread "
                    + std::string(std::strerror(errno)));   // a real error
            }
            if (n == 0) {
                throw std::runtime_error("DirectIOBackend::ReadPage - EOF before reading " + std::to_string(PAGE_SIZE) + " bytes");   // EOF before PAGE_SIZE bytes -- genuine corruption/short file
            }
            total += static_cast<size_t>(n);

            // We assume a single-page O_DIRECT transfer completes in full or fails
            // outright. If a genuine partial completion ever leaves `total`
            // misaligned, the next iteration's request would itself violate
            // O_DIRECT's alignment rules -- fail loudly here instead of letting
            // the kernel reject the retry with an opaque EINVAL.
            if (total < PAGE_SIZE && total % DIRECT_IO_ALIGNMENT != 0) {
                throw std::runtime_error(
                    "DirectIOBackend::ReadPage - partial O_DIRECT read left an "
                    "unaligned offset (" + std::to_string(total) + " of " +
                    std::to_string(PAGE_SIZE) + " bytes) at page " +
                    std::to_string(page_id) + " -- aborting rather than retry "
                    "with a misaligned request");
            }
        }
    }
    
    void DirectIOBackend::WritePage(page_id_t page_id, const Page& page) {
        off_t offset = static_cast<off_t>(page_id) * PAGE_SIZE;
        
        size_t total = 0;
        // retry incase of EINTR due to interrupt signals
        while (total < PAGE_SIZE) {
            ssize_t n = pwrite(fd_, page.raw() + total, PAGE_SIZE - total, offset + total);
            if (n < 0) {
                if (errno == EINTR) continue;   // no progress lost, retry the whole remaining call

                throw std::runtime_error("DirectIOBackend::WritePage - Failed to write page via pwrite "
                    + std::string(std::strerror(errno)));   // a real error
            }
            if (n == 0) {
                throw std::runtime_error("DirectIOBackend::WritePage - EOF before writing " + std::to_string(PAGE_SIZE) + " bytes");
                // EOF before PAGE_SIZE bytes -- genuine corruption/short file
            }
            total += static_cast<size_t>(n);

            // Same reasoning as ReadPage: a genuine partial O_DIRECT completion
            // that leaves `total` misaligned would make the retry itself invalid
            // -- fail loudly here instead of an opaque EINVAL from the kernel.
            if (total < PAGE_SIZE && total % DIRECT_IO_ALIGNMENT != 0) {
                throw std::runtime_error(
                    "DirectIOBackend::WritePage - partial O_DIRECT write left an "
                    "unaligned offset (" + std::to_string(total) + " of " +
                    std::to_string(PAGE_SIZE) + " bytes) at page " +
                    std::to_string(page_id) + " -- aborting rather than retry "
                    "with a misaligned request");
            }
        }
    }
    
    void DirectIOBackend::Sync() {
        // Even with O_DIRECT, metadata (like file size after GrowTo) can still
        // need an explicit fsync -- O_DIRECT bypasses caching of *data*, not
        // necessarily all metadata operations, depending on the filesystem.
        while (fsync(fd_) != 0) {
            // retry in case of interrupts(non-fatal)
            if (errno == EINTR) continue;
            throw std::runtime_error("DirectIOBackend::Sync: fsync failed: " +
                                      std::string(std::strerror(errno)));
        }
    }
    
    uint64_t DirectIOBackend::SizeInPages() const {
        struct stat st{};
        if (fstat(fd_, &st) != 0) {
            throw std::runtime_error("DirectIOBackend::SizeInPages: fstat failed: " +
                                    std::string(std::strerror(errno)));
        }
        return static_cast<uint64_t>(st.st_size) / PAGE_SIZE;
    }
    
    void DirectIOBackend::GrowTo(uint64_t num_pages) {

        if (num_pages < SizeInPages()) {
            throw std::runtime_error("DirectIOBackend::GrowTo - trying to shrink the file: " 
                + std::to_string(num_pages) 
                + " are given which is less the current pages: " 
                + std::to_string(SizeInPages()));
        }

        off_t target_size = static_cast<off_t>(num_pages) * PAGE_SIZE;
        
        while (ftruncate(fd_, target_size) != 0) {
            // retry in case of interrupts(non-fatal)
            if (errno == EINTR) continue;
            throw std::runtime_error("DirectIOBackend::GrowTo: ftruncate failed: " +
                                      std::string(std::strerror(errno)));
        }
    }
}