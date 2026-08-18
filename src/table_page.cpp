#include "tinydb/table_page.h"
#include "tinydb/byte_order.h"
#include "tinydb/page.h"
#include <span>
#include <string>
#include <stdexcept>

namespace tinydb {
    
    TablePage::TablePage(Page& page): page_(page) {}

    uint16_t TablePage::GetNumSlots() const {
        uint16_t num_slots;
        byte_order::ReadU16BigEndian(num_slots, page_.raw() + PageHeader::SIZE);
        return num_slots;
    }

    uint16_t TablePage::GetFreeSpaceStartOffset() const {
        uint16_t free_space_start_offset;
        byte_order::ReadU16BigEndian(free_space_start_offset, page_.raw() + PageHeader::SIZE + NUM_SLOT_BYTES);
        return free_space_start_offset;
    }

    uint16_t TablePage::GetSlotEntryEndOffset() const {
        uint16_t num_slots = GetNumSlots();
        return static_cast<uint16_t>(PageHeader::SIZE + NUM_SLOT_BYTES + FREE_SPACE_START_OFFSET_BYTES + num_slots * sizeof(SlotEntry));
    }

    SlotEntry TablePage::GetSlotEntry(slot_id_t slot_id) const {
        uint16_t num_slots = GetNumSlots();
        
        if(slot_id >= num_slots) 
            return SlotEntry{};

        uint16_t offset, length;
        byte_order::ReadU16BigEndian(offset, page_.raw() + PageHeader::SIZE + 4 + slot_id * sizeof(SlotEntry));
        byte_order::ReadU16BigEndian(length, page_.raw() + PageHeader::SIZE + 4 + slot_id * sizeof(SlotEntry) + 2);

        return SlotEntry{offset, length};
    } 

    void TablePage::Init() {
        // tuple_count = 0, free_space_start_offset = PAGE_SIZE
        byte_order::WriteU16BigEndian(0, page_.raw() + PageHeader::SIZE);
        byte_order::WriteU16BigEndian(PAGE_SIZE, page_.raw() + PageHeader::SIZE + NUM_SLOT_BYTES);
    }

    uint16_t TablePage::FreeSpace() const {
        uint16_t free_space_start_offset = GetFreeSpaceStartOffset();
        return static_cast<uint16_t>(free_space_start_offset - GetSlotEntryEndOffset());
    }

    std::optional<slot_id_t> TablePage::InsertTuple(std::span<const char> tuple) {
        uint16_t free_space_start_offset;
        uint16_t num_slots_;
        byte_order::ReadU16BigEndian(num_slots_, page_.raw() + PageHeader::SIZE);
        byte_order::ReadU16BigEndian(free_space_start_offset, page_.raw() + PageHeader::SIZE + NUM_SLOT_BYTES);

        uint16_t slot_end = GetSlotEntryEndOffset();

        if (slot_end > free_space_start_offset) 
            return std::nullopt;

        uint16_t potentially_available_bytes = free_space_start_offset - (PageHeader::SIZE + num_slots_ * sizeof(SlotEntry) + 4);

        if(potentially_available_bytes >= sizeof(SlotEntry) + tuple.size()) {
            uint16_t tuple_length = tuple.size();
            uint16_t tuple_offset = free_space_start_offset - tuple.size();
            // write tuple_offset to slot entry
            byte_order::WriteU16BigEndian(tuple_offset, page_.raw() + GetSlotEntryEndOffset());
            // write tuple_length to slot entry
            byte_order::WriteU16BigEndian(tuple_length, page_.raw() +  GetSlotEntryEndOffset() + 2);
            // write tuple.size() bytes in page
            std::memcpy(page_.raw() + tuple_offset, tuple.data(), tuple.size());
                // update free_space_start_offset, num_slots_
            free_space_start_offset -= tuple.size();
            num_slots_ += 1;
            byte_order::WriteU16BigEndian(num_slots_, page_.raw() + PageHeader::SIZE);
            byte_order::WriteU16BigEndian(free_space_start_offset, page_.raw() + PageHeader::SIZE + NUM_SLOT_BYTES);
            return std::make_optional(num_slots_-1);
        }

        return std::nullopt;
    }

    std::optional<std::span<const char>> TablePage::GetTuple(slot_id_t slot_id) const {
        SlotEntry slot_entry = GetSlotEntry(slot_id);
        uint16_t offset = slot_entry.offset_;
        uint16_t length = slot_entry.length_;

        if(length == 0) {
            return std::nullopt;
        }

        std::span<const char> ans{page_.raw() + offset, length};
        return ans;
    }

    bool TablePage::DeleteTuple(slot_id_t slot_id) {
        SlotEntry slot_entry = GetSlotEntry(slot_id);
        uint16_t offset = slot_entry.offset_;
        uint16_t length = slot_entry.length_;

        if(offset == 0 && length == 0) {
            throw std::runtime_error("[TablePage::DeleteTuple] Couldn't find any slot entry for the id = " + std::to_string(slot_id));
        }

        if(length == 0) {
            throw std::runtime_error("[TablePage::DeleteTuple] Tuple with slot_id = " + std::to_string(slot_id) + " seem to have been deleted already");
        }

        // set length to be 0 => tombstone
        byte_order::WriteU16BigEndian(0, page_.raw() + PageHeader::SIZE + 4 + slot_id * sizeof(SlotEntry) + 2);
        return true;
    }
    
}