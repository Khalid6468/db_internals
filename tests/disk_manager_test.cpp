#include "tinydb/disk_manager.h"
#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace tinydb;

class DiskManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        path_ = ::testing::TempDir() + "tinydb_disk_manager_" +
                std::to_string(reinterpret_cast<uintptr_t>(this)) + ".db";
        std::remove(path_.c_str());
    }

    void TearDown() override {
        std::remove(path_.c_str());
    }

    std::unique_ptr<DiskManager> MakeDiskManager() {
        return std::make_unique<DiskManager>(path_, IOStrategy::K_BUFFERED);
    }

    std::string path_;
};

TEST_F(DiskManagerTest, FreshDiskManagerHasNothingAllocated) {
    auto dm = MakeDiskManager();
    EXPECT_EQ(dm->NumPagesOnDisk(), 0u);
    EXPECT_EQ(dm->NumFreePages(), 0u);
}

TEST_F(DiskManagerTest, AllocatePageIncreasesPagesOnDisk) {
    auto dm = MakeDiskManager();
    page_id_t id = dm->AllocatePage();
    EXPECT_EQ(id, 0u);
    EXPECT_EQ(dm->NumPagesOnDisk(), 1u);

    page_id_t id2 = dm->AllocatePage();
    EXPECT_EQ(id2, 1u);
    EXPECT_EQ(dm->NumPagesOnDisk(), 2u);
}

TEST_F(DiskManagerTest, AllocatedPageIsStampedAndZeroedOnDisk) {
    auto dm = MakeDiskManager();
    page_id_t id = dm->AllocatePage();

    Page page;
    dm->ReadPage(id, page);
    EXPECT_EQ(page.GetPageId(), id);
    EXPECT_EQ(page.GetChecksum(), 0u);
    for (size_t i = 0; i < Page::payload_size(); i++) {
        ASSERT_EQ(static_cast<uint8_t>(page.payload()[i]), 0) << "payload byte " << i;
    }
}

TEST_F(DiskManagerTest, WritePageThenReadPageRoundTrips) {
    auto dm = MakeDiskManager();
    page_id_t id = dm->AllocatePage();

    Page page;
    page.SetPageId(id);
    page.SetChecksum(0xABCD1234);
    std::memset(page.payload(), 0x3C, Page::payload_size());
    dm->WritePage(page);

    Page read_back;
    dm->ReadPage(id, read_back);
    EXPECT_EQ(read_back.GetPageId(), id);
    EXPECT_EQ(read_back.GetChecksum(), 0xABCD1234u);
    EXPECT_EQ(static_cast<uint8_t>(read_back.payload()[0]), 0x3C);
}

TEST_F(DiskManagerTest, DeallocatedPageIsReflectedInFreeCount) {
    auto dm = MakeDiskManager();
    page_id_t id = dm->AllocatePage();
    EXPECT_EQ(dm->NumFreePages(), 0u);

    dm->DeallocatePage(id);
    EXPECT_EQ(dm->NumFreePages(), 1u);
}

TEST_F(DiskManagerTest, DeallocatedPageIdIsReusedOnNextAllocate) {
    auto dm = MakeDiskManager();
    page_id_t first = dm->AllocatePage();
    page_id_t second = dm->AllocatePage();
    dm->DeallocatePage(first);

    page_id_t reused = dm->AllocatePage();
    EXPECT_EQ(reused, first);
    EXPECT_EQ(dm->NumFreePages(), 0u);
    // total on-disk page count shouldn't have grown for the reuse
    EXPECT_EQ(dm->NumPagesOnDisk(), 2u);
    (void)second;
}

TEST_F(DiskManagerTest, DoubleDeallocateThrows) {
    auto dm = MakeDiskManager();
    page_id_t id = dm->AllocatePage();
    dm->DeallocatePage(id);
    EXPECT_THROW(dm->DeallocatePage(id), std::runtime_error);
}

TEST_F(DiskManagerTest, DeallocatingAnUnallocatedPageIdThrows) {
    auto dm = MakeDiskManager();
    dm->AllocatePage(); // only page id 0 exists
    EXPECT_THROW(dm->DeallocatePage(5), std::runtime_error);
}

TEST_F(DiskManagerTest, ReadPageDetectsOnDiskCorruptionAgainstRequestedId) {
    auto dm = MakeDiskManager();
    page_id_t id = dm->AllocatePage();
    dm->Sync();

    // corrupt the on-disk page_id field directly, bypassing DiskManager entirely,
    // to simulate a torn write / offset bug / disk corruption scenario.
    std::fstream f(path_, std::ios::in | std::ios::out | std::ios::binary);
    ASSERT_TRUE(f.is_open());
    f.seekp(static_cast<std::streamoff>(id) * PAGE_SIZE);
    const char corrupt_id[4] = {0, 0, 0, 99}; // big-endian page_id = 99, not `id`
    f.write(corrupt_id, 4);
    ASSERT_TRUE(f.good());
    f.close();

    Page page;
    EXPECT_THROW(dm->ReadPage(id, page), std::runtime_error);
}

TEST_F(DiskManagerTest, ReopeningPreservesPageCountButNotFreeList) {
    page_id_t first, second;
    {
        auto dm = MakeDiskManager();
        first = dm->AllocatePage();
        second = dm->AllocatePage();
        dm->DeallocatePage(first);
        dm->Sync();
    } // dm destroyed -- underlying file closed

    auto dm2 = MakeDiskManager();
    // on-disk page count survives a reopen...
    EXPECT_EQ(dm2->NumPagesOnDisk(), 2u);
    // ...but the free list is a known, deliberate gap: it does not.
    EXPECT_EQ(dm2->NumFreePages(), 0u);
    (void)second;
}

TEST_F(DiskManagerTest, MultipleAllocateDeallocateCyclesStayConsistent) {
    auto dm = MakeDiskManager();
    std::vector<page_id_t> ids;
    for (int i = 0; i < 5; i++) {
        ids.push_back(dm->AllocatePage());
    }
    for (page_id_t id : ids) {
        dm->DeallocatePage(id);
    }
    EXPECT_EQ(dm->NumFreePages(), 5u);
    EXPECT_EQ(dm->NumPagesOnDisk(), 5u);

    // re-allocating 5 more should reuse all 5 freed slots, not grow the file
    for (int i = 0; i < 5; i++) {
        dm->AllocatePage();
    }
    EXPECT_EQ(dm->NumFreePages(), 0u);
    EXPECT_EQ(dm->NumPagesOnDisk(), 5u);
}
