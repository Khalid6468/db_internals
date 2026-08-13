#include "tinydb/storage_manager.h"
#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <future>
#include <string>
#include <thread>

using namespace tinydb;

class StorageManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = ::testing::TempDir() + "tinydb_sm_" +
               std::to_string(reinterpret_cast<uintptr_t>(this));
        std::filesystem::remove_all(dir_);
        std::filesystem::create_directories(dir_);
    }

    void TearDown() override {
        std::filesystem::remove_all(dir_);
    }

    std::string dir_;
};

TEST_F(StorageManagerTest, AllocateWriteReadRoundTrip) {
    StorageManager sm(dir_, IOStrategy::K_BUFFERED);
    page_id_t id = sm.AllocatePage(0);

    Page page;
    page.SetPageId(id);
    page.SetChecksum(0xABCD1234);
    std::memset(page.payload(), 0x3C, Page::payload_size());
    sm.WritePage(PageAddress{0, id}, page);

    Page read_back;
    sm.ReadPage(PageAddress{0, id}, read_back);
    EXPECT_EQ(read_back.GetPageId(), id);
    EXPECT_EQ(read_back.GetChecksum(), 0xABCD1234u);
    EXPECT_EQ(static_cast<uint8_t>(read_back.payload()[0]), 0x3C);
}

TEST_F(StorageManagerTest, DifferentFilesHaveIndependentPageIdSpaces) {
    StorageManager sm(dir_, IOStrategy::K_BUFFERED);
    page_id_t id_file0 = sm.AllocatePage(0);
    page_id_t id_file1 = sm.AllocatePage(1);
    EXPECT_EQ(id_file0, 0u);
    EXPECT_EQ(id_file1, 0u) << "a fresh file's first page should be 0, independent of other files";
}

TEST_F(StorageManagerTest, WritePageRejectsAMismatchedPageId) {
    StorageManager sm(dir_, IOStrategy::K_BUFFERED);
    page_id_t id = sm.AllocatePage(0);

    Page page;
    page.SetPageId(id + 1); // deliberately wrong
    EXPECT_THROW(sm.WritePage(PageAddress{0, id}, page), std::runtime_error);
}

TEST_F(StorageManagerTest, EvictionAtCapacityReopensWithCorrectState) {
    StorageManager sm(dir_, IOStrategy::K_BUFFERED, /*max_open_files=*/2);
    page_id_t id0 = sm.AllocatePage(0);
    page_id_t id1 = sm.AllocatePage(1);

    // at capacity (2/2); opening a third file must evict one of {0,1} (both idle)
    EXPECT_NO_THROW(sm.AllocatePage(2));

    // whichever got evicted, re-touching it must reopen cleanly and continue
    // from the correct on-disk state rather than restarting from scratch
    EXPECT_EQ(sm.AllocatePage(0), id0 + 1);
    EXPECT_EQ(sm.AllocatePage(1), id1 + 1);
}

TEST_F(StorageManagerTest, PageDataSurvivesEvictionAndReopen) {
    // capacity 1 makes the victim deterministic: file 0 is the only entry, so
    // touching a second file *must* evict file 0, not some other candidate.
    StorageManager sm(dir_, IOStrategy::K_BUFFERED, /*max_open_files=*/1);
    page_id_t id0 = sm.AllocatePage(0);
    Page page;
    page.SetPageId(id0);
    page.payload()[0] = 'Z';
    sm.WritePage(PageAddress{0, id0}, page);

    sm.AllocatePage(1); // evicts file 0, the only entry at capacity 1

    Page read_back;
    sm.ReadPage(PageAddress{0, id0}, read_back);
    EXPECT_EQ(read_back.payload()[0], 'Z');
}

TEST_F(StorageManagerTest, EvictionThrowsWhenNoIdleVictimExists) {
    StorageManager sm(dir_, IOStrategy::K_BUFFERED, /*max_open_files=*/1);
    sm.AllocatePage(0); // opens file 0; now at capacity (1/1)

    std::promise<void> acquired_promise;
    std::future<void> acquired = acquired_promise.get_future();
    std::promise<void> release_promise;
    std::shared_future<void> release = release_promise.get_future().share();

    std::thread holder([&]{
        sm.HoldFileOpenForTesting(0, [&]{ acquired_promise.set_value(); }, release);
    });
    acquired.wait(); // block until file 0 is genuinely held busy (use_count() == 2)

    // capacity is 1, file 0 is the only entry, and it's busy -- opening a new
    // file must throw rather than block forever or silently exceed the cap
    EXPECT_THROW(sm.AllocatePage(1), std::runtime_error);

    release_promise.set_value();
    holder.join();

    // file 0 is idle again now -- opening a new file should succeed by evicting it
    EXPECT_NO_THROW(sm.AllocatePage(1));
}
