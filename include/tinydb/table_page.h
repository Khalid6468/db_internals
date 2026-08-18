#pragma once

#include "page.h"
#include <span>
#include <optional>
#include <cstdint>
#include <cstring>

namespace tinydb {

    using slot_id_t = uint16_t;

    constexpr uint16_t NUM_SLOT_BYTES = 2;
    constexpr uint16_t FREE_SPACE_START_OFFSET_BYTES = 2;

    struct RID {
        page_id_t page_id;
        slot_id_t slot_id;
    };

    struct SlotEntry {
        uint16_t offset_;
        uint16_t length_;

        SlotEntry() {
            offset_ = 0;
            length_ = 0;
        }

        SlotEntry(uint16_t offset, uint16_t length) {
            offset_ = offset;
            length_ = length;
        }
    };

    class TablePage {
        private:
            Page& page_;
            uint16_t GetFreeSpaceStartOffset() const;
            uint16_t GetNumSlots() const;
            uint16_t GetSlotEntryEndOffset() const;
            SlotEntry GetSlotEntry(slot_id_t slot_id) const;
        
        public:
            explicit TablePage(Page& page);
            void Init();
            std::optional<slot_id_t> InsertTuple(std::span<const char> data);
            bool DeleteTuple(slot_id_t slot_id);
            std::optional<std::span<const char>> GetTuple(slot_id_t slot_id) const;
            uint16_t FreeSpace() const;

    };
}