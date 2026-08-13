#include "tinydb/buffer_pool_manager.h"
#include "tinydb/storage_manager.h"
#include "tinydb/approx_lruk_replacer.h"
#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>

using namespace tinydb;

namespace {
constexpr file_id_t kFileId = 0;
constexpr file_id_t kOtherFileId = 1;

PageAddress NewAddr(file_id_t file_id = kFileId) {
    return PageAddress{file_id, INVALID_PAGE_ID};
}
} // namespace

class BufferPoolManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        path_ = ::testing::TempDir() + "tinydb_bpm_" +
                std::to_string(reinterpret_cast<uintptr_t>(this));
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }

    void TearDown() override {
        std::filesystem::remove_all(path_);
    }

    std::unique_ptr<BufferPoolManager> MakeBpm(size_t pool_size, size_t promotion_threshold = 200) {
        return std::make_unique<BufferPoolManager>(path_, IOStrategy::K_BUFFERED, pool_size,
            std::make_unique<ApproxLRUKReplacer>(promotion_threshold));
    }

    std::string path_;
};

TEST_F(BufferPoolManagerTest, NewPageReturnsAZeroedPageWithItsIdStamped) {
    auto bpm = MakeBpm(3);
    PageAddress addr = NewAddr();
    Page* page = bpm->NewPage(&addr);
    ASSERT_NE(page, nullptr);
    EXPECT_EQ(page->GetPageId(), addr.page_id);
    for (size_t i = 0; i < Page::payload_size(); i++) {
        ASSERT_EQ(static_cast<uint8_t>(page->payload()[i]), 0) << "payload byte " << i;
    }
}

TEST_F(BufferPoolManagerTest, NewPageWritesTheAllocatedAddressBackToTheCaller) {
    auto bpm = MakeBpm(3);
    PageAddress addr = NewAddr();
    bpm->NewPage(&addr);
    EXPECT_EQ(addr.file_id, kFileId);
    EXPECT_NE(addr.page_id, INVALID_PAGE_ID) << "NewPage must write the allocated page_id back into *addr";
}

TEST_F(BufferPoolManagerTest, NewPageAssignsDistinctPageIds) {
    auto bpm = MakeBpm(3);
    PageAddress a1 = NewAddr(), a2 = NewAddr(), a3 = NewAddr();
    bpm->NewPage(&a1);
    bpm->NewPage(&a2);
    bpm->NewPage(&a3);
    EXPECT_NE(a1, a2);
    EXPECT_NE(a2, a3);
    EXPECT_NE(a1, a3);
}

TEST_F(BufferPoolManagerTest, DifferentFilesGetIndependentPageIdSpaces) {
    auto bpm = MakeBpm(3);
    PageAddress a1 = NewAddr(kFileId);
    PageAddress a2 = NewAddr(kOtherFileId);
    bpm->NewPage(&a1);
    bpm->NewPage(&a2);

    EXPECT_EQ(a1.page_id, 0u);
    EXPECT_EQ(a2.page_id, 0u) << "a fresh file's first page should also be page_id 0, independent of file 0's allocator";
    EXPECT_NE(a1, a2) << "same page_id but different file_id must still be distinct addresses";
}

TEST_F(BufferPoolManagerTest, FetchingFromTheWrongFileDoesNotReturnAnotherFilesPage) {
    auto bpm = MakeBpm(3);
    PageAddress a1 = NewAddr(kFileId);
    Page* page1 = bpm->NewPage(&a1);
    page1->payload()[0] = 'A';
    ASSERT_TRUE(bpm->UnPinPage(a1, true));

    // same page_id, different file_id -- must not be treated as a cache hit for a1
    PageAddress wrong_file{kOtherFileId, a1.page_id};
    EXPECT_THROW(bpm->FetchPage(wrong_file), std::runtime_error);
}

TEST_F(BufferPoolManagerTest, NewPageThrowsWhenPoolIsFullAndEverythingIsPinned) {
    auto bpm = MakeBpm(2);
    PageAddress a1 = NewAddr(), a2 = NewAddr(), a3 = NewAddr();
    bpm->NewPage(&a1);
    bpm->NewPage(&a2);
    EXPECT_THROW(bpm->NewPage(&a3), std::runtime_error);
}

TEST_F(BufferPoolManagerTest, UnpinningMakesRoomForEvictionWhenPoolIsFull) {
    auto bpm = MakeBpm(2);
    PageAddress a1 = NewAddr(), a2 = NewAddr(), a3 = NewAddr();
    bpm->NewPage(&a1);
    bpm->NewPage(&a2);
    ASSERT_TRUE(bpm->UnPinPage(a1, false));
    ASSERT_TRUE(bpm->UnPinPage(a2, false));

    Page* page3 = bpm->NewPage(&a3);
    EXPECT_NE(page3, nullptr);
    EXPECT_NE(a3, a1);
    EXPECT_NE(a3, a2);
}

TEST_F(BufferPoolManagerTest, PinCountSurvivesMultipleFetchesUntilFullyUnpinned) {
    auto bpm = MakeBpm(2);
    PageAddress p1 = NewAddr(), p2 = NewAddr(), p3 = NewAddr();
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
    PageAddress addr = NewAddr();
    Page* created = bpm->NewPage(&addr);
    created->payload()[0] = 'Z';
    ASSERT_TRUE(bpm->UnPinPage(addr, true));

    Page* fetched = bpm->FetchPage(addr);
    ASSERT_NE(fetched, nullptr);
    EXPECT_EQ(fetched->GetPageId(), addr.page_id);
    EXPECT_EQ(fetched->payload()[0], 'Z');
}

TEST_F(BufferPoolManagerTest, FetchPageOnUnknownPageThrows) {
    auto bpm = MakeBpm(2);
    EXPECT_THROW(bpm->FetchPage(PageAddress{kFileId, 999}), std::runtime_error);
}

TEST_F(BufferPoolManagerTest, UnpinningAnUnknownPageThrows) {
    auto bpm = MakeBpm(2);
    EXPECT_THROW(bpm->UnPinPage(PageAddress{kFileId, 999}, false), std::runtime_error);
}

TEST_F(BufferPoolManagerTest, UnpinningAnAlreadyFullyUnpinnedPageThrows) {
    auto bpm = MakeBpm(2);
    PageAddress addr = NewAddr();
    bpm->NewPage(&addr);
    ASSERT_TRUE(bpm->UnPinPage(addr, false));
    EXPECT_THROW(bpm->UnPinPage(addr, false), std::runtime_error);
}

TEST_F(BufferPoolManagerTest, FlushPageOnUnknownPageReturnsFalse) {
    auto bpm = MakeBpm(2);
    EXPECT_FALSE(bpm->FlushPage(PageAddress{kFileId, 999}));
}

TEST_F(BufferPoolManagerTest, FlushPageWritesDirtyDataThroughToDisk) {
    PageAddress addr = NewAddr();
    {
        auto bpm = MakeBpm(2);
        Page* page = bpm->NewPage(&addr);
        page->payload()[0] = 'Q';
        ASSERT_TRUE(bpm->UnPinPage(addr, /*is_dirty=*/true));
        ASSERT_TRUE(bpm->FlushPage(addr));
    } // bpm destroyed; its own fd closed

    // verify independently, bypassing BufferPoolManager entirely
    StorageManager sm(path_, IOStrategy::K_BUFFERED);
    Page verify;
    sm.ReadPage(addr, verify);
    EXPECT_EQ(verify.payload()[0], 'Q');
}

TEST_F(BufferPoolManagerTest, DeletePageRefusesToDeleteAPinnedPage) {
    auto bpm = MakeBpm(2);
    PageAddress addr = NewAddr();
    bpm->NewPage(&addr); // still pinned, never unpinned
    EXPECT_FALSE(bpm->DeletePage(addr));
}

TEST_F(BufferPoolManagerTest, DeletePageOnUnknownPageThrows) {
    auto bpm = MakeBpm(2);
    EXPECT_THROW(bpm->DeletePage(PageAddress{kFileId, 999}), std::runtime_error);
}

TEST_F(BufferPoolManagerTest, DeletePageFreesTheFrameForReuseWithoutEviction) {
    auto bpm = MakeBpm(1); // pool of exactly one frame
    PageAddress addr1 = NewAddr();
    bpm->NewPage(&addr1);
    ASSERT_TRUE(bpm->UnPinPage(addr1, false));
    ASSERT_TRUE(bpm->DeletePage(addr1));

    // the only frame is now free; a new page must succeed without needing eviction
    // (with pool_size 1 and nothing else evictable, NewPage would throw if it had
    // to fall through to Replacer::Victim instead of reusing the freed frame).
    // DiskManager is free to reuse the same underlying page_id here -- that's
    // separately verified in disk_manager_test.cpp -- so it isn't checked here.
    PageAddress addr2 = NewAddr();
    EXPECT_NE(bpm->NewPage(&addr2), nullptr);
}

TEST_F(BufferPoolManagerTest, PageDataSurvivesEvictionAndRefetch) {
    auto bpm = MakeBpm(3);
    PageAddress p1 = NewAddr(), p2 = NewAddr(), p3 = NewAddr(), p4 = NewAddr();

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
    PageAddress p1 = NewAddr(), p2 = NewAddr();

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
