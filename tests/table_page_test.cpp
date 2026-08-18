#include "tinydb/table_page.h"
#include <gtest/gtest.h>
#include <memory>
#include <span>
#include <string>
#include <vector>

using namespace tinydb;

namespace {
std::span<const char> AsSpan(const std::string& s) {
    return std::span<const char>(s.data(), s.size());
}
} // namespace

class TablePageTest : public ::testing::Test {
protected:
    void SetUp() override {
        table_page_ = std::make_unique<TablePage>(page_);
        table_page_->Init();
    }

    Page page_;
    std::unique_ptr<TablePage> table_page_;
};

TEST_F(TablePageTest, InitStartsWithFullFreeSpace) {
    // header (PageHeader::SIZE) + num_slots (2 bytes) + free_space_start_offset
    // (2 bytes) are the only overhead on a freshly-initialized page.
    EXPECT_EQ(table_page_->FreeSpace(), PAGE_SIZE - PageHeader::SIZE - 4);
}

TEST_F(TablePageTest, InsertTupleReturnsSequentialSlotIds) {
    auto s0 = table_page_->InsertTuple(AsSpan("a"));
    auto s1 = table_page_->InsertTuple(AsSpan("bb"));
    auto s2 = table_page_->InsertTuple(AsSpan("ccc"));
    ASSERT_TRUE(s0.has_value());
    ASSERT_TRUE(s1.has_value());
    ASSERT_TRUE(s2.has_value());
    EXPECT_EQ(*s0, 0);
    EXPECT_EQ(*s1, 1);
    EXPECT_EQ(*s2, 2);
}

TEST_F(TablePageTest, InsertedTupleRoundTripsThroughGetTuple) {
    std::string data = "hello world";
    auto slot = table_page_->InsertTuple(AsSpan(data));
    ASSERT_TRUE(slot.has_value());

    auto tuple = table_page_->GetTuple(*slot);
    ASSERT_TRUE(tuple.has_value());
    EXPECT_EQ(std::string(tuple->data(), tuple->size()), data);
}

TEST_F(TablePageTest, MultipleTuplesAreIndependentlyRetrievable) {
    std::string t0 = "hello";
    std::string t1 = "world!!";
    std::string t2 = "third-tuple-data";

    auto s0 = table_page_->InsertTuple(AsSpan(t0));
    auto s1 = table_page_->InsertTuple(AsSpan(t1));
    auto s2 = table_page_->InsertTuple(AsSpan(t2));
    ASSERT_TRUE(s0 && s1 && s2);

    auto g0 = table_page_->GetTuple(*s0);
    auto g1 = table_page_->GetTuple(*s1);
    auto g2 = table_page_->GetTuple(*s2);
    ASSERT_TRUE(g0 && g1 && g2);
    EXPECT_EQ(std::string(g0->data(), g0->size()), t0);
    EXPECT_EQ(std::string(g1->data(), g1->size()), t1);
    EXPECT_EQ(std::string(g2->data(), g2->size()), t2);
}

TEST_F(TablePageTest, FreeSpaceDecreasesByTupleSizePlusSlotEntrySize) {
    uint16_t before = table_page_->FreeSpace();
    std::string data = "0123456789"; // 10 bytes
    auto slot = table_page_->InsertTuple(AsSpan(data));
    ASSERT_TRUE(slot.has_value());
    uint16_t after = table_page_->FreeSpace();
    EXPECT_EQ(before - after, data.size() + sizeof(SlotEntry));
}

TEST_F(TablePageTest, InsertingPastCapacityFailsCleanlyWithoutCorruptingExistingTuples) {
    std::string tuple(100, 'x');
    std::vector<slot_id_t> slots;
    for (;;) {
        auto s = table_page_->InsertTuple(AsSpan(tuple));
        if (!s.has_value()) break;
        slots.push_back(*s);
        ASSERT_LT(slots.size(), 1000u) << "safety bailout -- suspected infinite loop";
    }
    ASSERT_FALSE(slots.empty());

    // one more insert must still cleanly fail, not crash or corrupt state
    EXPECT_FALSE(table_page_->InsertTuple(AsSpan(tuple)).has_value());

    // every previously inserted tuple must still be intact
    for (slot_id_t id : slots) {
        auto g = table_page_->GetTuple(id);
        ASSERT_TRUE(g.has_value());
        EXPECT_EQ(g->size(), tuple.size());
        for (char c : *g) {
            EXPECT_EQ(c, 'x');
        }
    }
}

TEST_F(TablePageTest, DeleteTupleTombstonesTheSlot) {
    auto slot = table_page_->InsertTuple(AsSpan("goner"));
    ASSERT_TRUE(slot.has_value());
    EXPECT_TRUE(table_page_->DeleteTuple(*slot));
    EXPECT_FALSE(table_page_->GetTuple(*slot).has_value());
}

TEST_F(TablePageTest, DeletingATupleDoesNotAffectSiblings) {
    auto s0 = table_page_->InsertTuple(AsSpan("first"));
    auto s1 = table_page_->InsertTuple(AsSpan("second"));
    auto s2 = table_page_->InsertTuple(AsSpan("third"));
    ASSERT_TRUE(s0 && s1 && s2);

    EXPECT_TRUE(table_page_->DeleteTuple(*s1));

    auto g0 = table_page_->GetTuple(*s0);
    auto g2 = table_page_->GetTuple(*s2);
    ASSERT_TRUE(g0.has_value());
    ASSERT_TRUE(g2.has_value());
    EXPECT_EQ(std::string(g0->data(), g0->size()), "first");
    EXPECT_EQ(std::string(g2->data(), g2->size()), "third");
}

TEST_F(TablePageTest, DoubleDeleteThrows) {
    auto slot = table_page_->InsertTuple(AsSpan("data"));
    ASSERT_TRUE(slot.has_value());
    ASSERT_TRUE(table_page_->DeleteTuple(*slot));
    EXPECT_THROW(table_page_->DeleteTuple(*slot), std::runtime_error);
}

TEST_F(TablePageTest, DeletingANeverInsertedSlotThrows) {
    EXPECT_THROW(table_page_->DeleteTuple(42), std::runtime_error);
}

TEST_F(TablePageTest, GetTupleOnANeverInsertedSlotReturnsNullopt) {
    EXPECT_FALSE(table_page_->GetTuple(0).has_value());
    EXPECT_FALSE(table_page_->GetTuple(7).has_value());
}

TEST_F(TablePageTest, DeletedTupleSpaceIsNotReclaimedForReuse) {
    // documents current (deliberate) behavior: deletes are tombstone-only, no
    // compaction, so the freed bytes aren't available to later inserts yet.
    std::string tuple(100, 'x');
    std::vector<slot_id_t> slots;
    for (;;) {
        auto s = table_page_->InsertTuple(AsSpan(tuple));
        if (!s.has_value()) break;
        slots.push_back(*s);
    }
    ASSERT_FALSE(slots.empty());

    ASSERT_TRUE(table_page_->DeleteTuple(slots.front()));
    EXPECT_FALSE(table_page_->InsertTuple(AsSpan(tuple)).has_value())
        << "deleted tuple's space should not be reusable without compaction";
}
