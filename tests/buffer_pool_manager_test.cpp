#include "tinydb/buffer_pool_manager.h"
#include "tinydb/disk_manager.h"
#include "tinydb/approx_lruk_replacer.h"
#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

using namespace tinydb;

class BufferPoolManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        path_ = ::testing::TempDir() + "tinydb_bpm_" +
                std::to_string(reinterpret_cast<uintptr_t>(this)) + ".db";
        std::remove(path_.c_str());
    }

    void TearDown() override {
        std::remove(path_.c_str());
    }

    std::unique_ptr<BufferPoolManager> MakeBpm(size_t pool_size, size_t promotion_threshold = 200) {
        return std::make_unique<BufferPoolManager>(path_, IOStrategy::K_BUFFERED, pool_size,
            std::make_unique<ApproxLRUKReplacer>(promotion_threshold));
    }

    std::string path_;
};

TEST_F(BufferPoolManagerTest, NewPageReturnsAZeroedPageWithItsIdStamped) {
    auto bpm = MakeBpm(3);
    page_id_t id;
    Page* page = bpm->NewPage(&id);
    ASSERT_NE(page, nullptr);
    EXPECT_EQ(page->GetPageId(), id);
    for (size_t i = 0; i < Page::payload_size(); i++) {
        ASSERT_EQ(static_cast<uint8_t>(page->payload()[i]), 0) << "payload byte " << i;
    }
}

TEST_F(BufferPoolManagerTest, NewPageAssignsDistinctPageIds) {
    auto bpm = MakeBpm(3);
    page_id_t id1, id2, id3;
    bpm->NewPage(&id1);
    bpm->NewPage(&id2);
    bpm->NewPage(&id3);
    EXPECT_NE(id1, id2);
    EXPECT_NE(id2, id3);
    EXPECT_NE(id1, id3);
}

TEST_F(BufferPoolManagerTest, NewPageThrowsWhenPoolIsFullAndEverythingIsPinned) {
    auto bpm = MakeBpm(2);
    page_id_t id1, id2, id3;
    bpm->NewPage(&id1);
    bpm->NewPage(&id2);
    EXPECT_THROW(bpm->NewPage(&id3), std::runtime_error);
}

TEST_F(BufferPoolManagerTest, UnpinningMakesRoomForEvictionWhenPoolIsFull) {
    auto bpm = MakeBpm(2);
    page_id_t id1, id2, id3;
    bpm->NewPage(&id1);
    bpm->NewPage(&id2);
    ASSERT_TRUE(bpm->UnPinPage(id1, false));
    ASSERT_TRUE(bpm->UnPinPage(id2, false));

    Page* page3 = bpm->NewPage(&id3);
    EXPECT_NE(page3, nullptr);
    EXPECT_NE(id3, id1);
    EXPECT_NE(id3, id2);
}

TEST_F(BufferPoolManagerTest, PinCountSurvivesMultipleFetchesUntilFullyUnpinned) {
    auto bpm = MakeBpm(2);
    page_id_t p1, p2, p3;
    bpm->NewPage(&p1);                // p1 pinned once (via NewPage)
    ASSERT_NE(bpm->FetchPage(p1), nullptr); // p1 pinned twice now
    bpm->NewPage(&p2);                // pool full: p1 (x2 pins), p2 (x1 pin)

    ASSERT_TRUE(bpm->UnPinPage(p1, false)); // p1 down to 1 pin -- still not evictable
    EXPECT_THROW(bpm->NewPage(&p3), std::runtime_error)
        << "p1 should still be pinned once, p2 fully pinned -- no room";

    ASSERT_TRUE(bpm->UnPinPage(p2, false)); // p2 now evictable
    Page* page3 = bpm->NewPage(&p3);
    EXPECT_NE(page3, nullptr);
    EXPECT_NE(p3, p1) << "p1 still had an outstanding pin and must not have been evicted";
}

TEST_F(BufferPoolManagerTest, FetchPageReturnsTheSameCachedFrameContents) {
    auto bpm = MakeBpm(3);
    page_id_t id;
    Page* created = bpm->NewPage(&id);
    created->payload()[0] = 'Z';
    ASSERT_TRUE(bpm->UnPinPage(id, true));

    Page* fetched = bpm->FetchPage(id);
    ASSERT_NE(fetched, nullptr);
    EXPECT_EQ(fetched->GetPageId(), id);
    EXPECT_EQ(fetched->payload()[0], 'Z');
}

TEST_F(BufferPoolManagerTest, FetchPageOnUnknownPageThrows) {
    auto bpm = MakeBpm(2);
    EXPECT_THROW(bpm->FetchPage(999), std::runtime_error);
}

TEST_F(BufferPoolManagerTest, UnpinningAnUnknownPageThrows) {
    auto bpm = MakeBpm(2);
    EXPECT_THROW(bpm->UnPinPage(999, false), std::runtime_error);
}

TEST_F(BufferPoolManagerTest, UnpinningAnAlreadyFullyUnpinnedPageThrows) {
    auto bpm = MakeBpm(2);
    page_id_t id;
    bpm->NewPage(&id);
    ASSERT_TRUE(bpm->UnPinPage(id, false));
    EXPECT_THROW(bpm->UnPinPage(id, false), std::runtime_error);
}

TEST_F(BufferPoolManagerTest, FlushPageOnUnknownPageReturnsFalse) {
    auto bpm = MakeBpm(2);
    EXPECT_FALSE(bpm->FlushPage(999));
}

TEST_F(BufferPoolManagerTest, FlushPageWritesDirtyDataThroughToDisk) {
    page_id_t id;
    {
        auto bpm = MakeBpm(2);
        Page* page = bpm->NewPage(&id);
        page->payload()[0] = 'Q';
        ASSERT_TRUE(bpm->UnPinPage(id, /*is_dirty=*/true));
        ASSERT_TRUE(bpm->FlushPage(id));
    } // bpm destroyed; its own fd closed

    // verify independently, bypassing BufferPoolManager entirely
    DiskManager dm(path_, IOStrategy::K_BUFFERED);
    Page verify;
    dm.ReadPage(id, verify);
    EXPECT_EQ(verify.payload()[0], 'Q');
}

TEST_F(BufferPoolManagerTest, DeletePageRefusesToDeleteAPinnedPage) {
    auto bpm = MakeBpm(2);
    page_id_t id;
    bpm->NewPage(&id); // still pinned, never unpinned
    EXPECT_FALSE(bpm->DeletePage(id));
}

TEST_F(BufferPoolManagerTest, DeletePageOnUnknownPageThrows) {
    auto bpm = MakeBpm(2);
    EXPECT_THROW(bpm->DeletePage(999), std::runtime_error);
}

TEST_F(BufferPoolManagerTest, DeletePageFreesTheFrameForReuseWithoutEviction) {
    auto bpm = MakeBpm(1); // pool of exactly one frame
    page_id_t id1;
    bpm->NewPage(&id1);
    ASSERT_TRUE(bpm->UnPinPage(id1, false));
    ASSERT_TRUE(bpm->DeletePage(id1));

    // the only frame is now free; a new page must succeed without needing eviction
    // (with pool_size 1 and nothing else evictable, NewPage would throw if it had
    // to fall through to Replacer::Victim instead of reusing the freed frame).
    // DiskManager is free to reuse the same underlying page_id here -- that's
    // separately verified in disk_manager_test.cpp -- so it isn't checked here.
    page_id_t id2;
    EXPECT_NE(bpm->NewPage(&id2), nullptr);
}

TEST_F(BufferPoolManagerTest, PageDataSurvivesEvictionAndRefetch) {
    auto bpm = MakeBpm(3);
    page_id_t p1, p2, p3, p4;

    Page* page1 = bpm->NewPage(&p1);
    page1->payload()[0] = 'A';
    Page* page2 = bpm->NewPage(&p2);
    page2->payload()[0] = 'B';
    Page* page3 = bpm->NewPage(&p3);
    page3->payload()[0] = 'C';

    ASSERT_TRUE(bpm->UnPinPage(p1, true));
    ASSERT_TRUE(bpm->UnPinPage(p2, true));
    ASSERT_TRUE(bpm->UnPinPage(p3, true));

    // pool is full (3/3) but all unpinned -- this forces an eviction
    Page* page4 = bpm->NewPage(&p4);
    page4->payload()[0] = 'D';
    ASSERT_TRUE(bpm->UnPinPage(p4, true));

    // whichever page got evicted, refetching it must transparently reload it
    // from disk with its data intact
    Page* refetched1 = bpm->FetchPage(p1);
    EXPECT_EQ(refetched1->payload()[0], 'A');
    ASSERT_TRUE(bpm->UnPinPage(p1, false));

    Page* refetched2 = bpm->FetchPage(p2);
    EXPECT_EQ(refetched2->payload()[0], 'B');
    ASSERT_TRUE(bpm->UnPinPage(p2, false));
}

TEST_F(BufferPoolManagerTest, EvictingADirtyPageFlushesItBeforeReuse) {
    auto bpm = MakeBpm(1);
    page_id_t p1, p2;

    Page* page1 = bpm->NewPage(&p1);
    page1->payload()[0] = 'X';
    ASSERT_TRUE(bpm->UnPinPage(p1, /*is_dirty=*/true));

    // forces p1 out of the only frame, which must flush it first since it's dirty
    Page* page2 = bpm->NewPage(&p2);
    page2->payload()[0] = 'Y';
    ASSERT_TRUE(bpm->UnPinPage(p2, true));

    Page* refetched = bpm->FetchPage(p1);
    EXPECT_EQ(refetched->payload()[0], 'X') << "dirty page must have been flushed before its frame was reused";
}
