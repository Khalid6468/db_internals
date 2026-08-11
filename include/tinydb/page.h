#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <array>

#include "byte_order.h"

namespace tinydb {
    constexpr size_t PAGE_SIZE = 16 * 1024;
    using page_id_t = uint32_t;
    constexpr page_id_t INVALID_PAGE_ID = static_cast<page_id_t>(-1);

    struct PageHeader {
        page_id_t page_id;
        uint32_t checksum;
        uint32_t lsn_hi;
        uint32_t lsn_lo;
        uint16_t page_type;
        uint16_t reserved;

        static constexpr size_t SIZE = sizeof(page_id_t) + sizeof(uint32_t) + sizeof(uint64_t)
            + sizeof(uint16_t) * 2;

    };

    static_assert(PageHeader::SIZE == 20, "PageHeader size is not 20 bytes");


    class Page {
        private:
            alignas(4096) std::array<char, PAGE_SIZE> data_;
        public:
            Page() {}

            // delete copy constructor and move constructor to avoid copying 16kb of data in future
            Page(const Page&) = delete;
            Page(Page&&) = delete;
            Page& operator=(const Page&) = delete;
            Page& operator=(Page&&) = delete;

            char* raw() {
                return data_.data();
            }

            const char* raw() const { 
                return data_.data(); 
            }

            static constexpr size_t size() { 
                return PAGE_SIZE; 
            }

            char* payload() {
                return data_.data() + PageHeader::SIZE;
            }

            const char* payload() const {
                return data_.data() + PageHeader::SIZE;
            }

            void SetPageType(uint16_t page_type) {
                byte_order::WriteU16BigEndian(page_type, data_.data() + 16);
            }

            uint16_t GetPageType() const {
                uint16_t page_type;
                byte_order::ReadU16BigEndian(page_type, data_.data() + 16);
                return page_type;
            } 

            uint32_t GetChecksum() const {
                uint32_t checksum;
                byte_order::ReadU32BigEndian(checksum, data_.data() + 4);
                return checksum;
            }

            void SetChecksum(uint32_t checksum) {
                byte_order::WriteU32BigEndian(checksum, data_.data() + 4);
            }

            static constexpr size_t payload_size() {
                return PAGE_SIZE - PageHeader::SIZE;
            }

            uint64_t GetLsn() const {
                uint32_t lsn_hi, lsn_lo;
                byte_order::ReadU32BigEndian(lsn_hi, data_.data() + 8);
                byte_order::ReadU32BigEndian(lsn_lo, data_.data() + 12);
                return static_cast<uint64_t>(lsn_hi) << 32 | lsn_lo;
            }
    
            void SetLsn(uint64_t lsn) {
                byte_order::WriteU32BigEndian(static_cast<uint32_t>(lsn >> 32), data_.data() + 8);
                byte_order::WriteU32BigEndian(static_cast<uint32_t>(lsn), data_.data() + 12);
            }

            page_id_t GetPageId() const {
                page_id_t page_id;
                byte_order::ReadU32BigEndian(page_id, data_.data());
                return page_id;
            }

            void SetPageId(page_id_t page_id) {
                byte_order::WriteU32BigEndian(page_id, data_.data());
            }

            void WritePageHeader(const PageHeader& header) {
                byte_order::WriteU32BigEndian(header.page_id, raw());
                byte_order::WriteU32BigEndian(header.checksum, raw() + 4);
                byte_order::WriteU32BigEndian(header.lsn_hi, raw() + 8);
                byte_order::WriteU32BigEndian(header.lsn_lo, raw() + 12);
                byte_order::WriteU16BigEndian(header.page_type, raw() + 16);
                byte_order::WriteU16BigEndian(header.reserved, raw() + 18);
            }

            void ReadPageHeader(PageHeader& header) const {
                byte_order::ReadU32BigEndian(header.page_id, raw());
                byte_order::ReadU32BigEndian(header.checksum, raw() + 4);
                byte_order::ReadU32BigEndian(header.lsn_hi, raw() + 8);
                byte_order::ReadU32BigEndian(header.lsn_lo, raw() + 12);
                byte_order::ReadU16BigEndian(header.page_type, raw() + 16);
                byte_order::ReadU16BigEndian(header.reserved, raw() + 18);
            }

            static PageHeader DeserializeFrom(const char* buffer) {
                PageHeader header;
                byte_order::ReadU32BigEndian(header.page_id, buffer);
                byte_order::ReadU32BigEndian(header.checksum, buffer + 4);
                byte_order::ReadU32BigEndian(header.lsn_hi, buffer + 8);
                byte_order::ReadU32BigEndian(header.lsn_lo, buffer + 12);
                byte_order::ReadU16BigEndian(header.page_type, buffer + 16);
                byte_order::ReadU16BigEndian(header.reserved, buffer + 18);
                return header;
            }
    };
}