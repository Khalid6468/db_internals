#include "tinydb/io_backend.h"
#include "tinydb/page.h"
#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

using namespace tinydb;

// Runs the same behavioral checks against both backends, since they're expected
// to behave identically from the caller's point of view (DirectIOBackend falls
// back to F_NOCACHE on macOS, so it can't exercise real O_DIRECT alignment
// behavior on this platform, but everything else is fully exercised here).
class IOBackendTest : public ::testing::TestWithParam<IOStrategy> {
protected:
    void SetUp() override {
        const char* strategy_name = (GetParam() == IOStrategy::K_BUFFERED) ? "buffered" : "direct";
        path_ = ::testing::TempDir() + "tinydb_io_backend_" + strategy_name + "_" +
                std::to_string(reinterpret_cast<uintptr_t>(this)) + ".db";
        std::remove(path_.c_str());
    }

    void TearDown() override {
        std::remove(path_.c_str());
    }

    std::unique_ptr<IOBackend> MakeBackend() {
        return IOBackendFactory::Create(GetParam(), path_);
    }

    std::string path_;
};

TEST_P(IOBackendTest, FreshFileStartsWithZeroPages) {
    auto backend = MakeBackend();
    EXPECT_EQ(backend->SizeInPages(), 0u);
}

TEST_P(IOBackendTest, GrowToIncreasesSizeInPages) {
    auto backend = MakeBackend();
    backend->GrowTo(3);
    EXPECT_EQ(backend->SizeInPages(), 3u);
    backend->GrowTo(5);
    EXPECT_EQ(backend->SizeInPages(), 5u);
}

TEST_P(IOBackendTest, GrowToRefusesToShrink) {
    auto backend = MakeBackend();
    backend->GrowTo(5);
    EXPECT_THROW(backend->GrowTo(2), std::runtime_error);
    EXPECT_EQ(backend->SizeInPages(), 5u);
}

TEST_P(IOBackendTest, GrowToTheSameSizeIsAllowed) {
    auto backend = MakeBackend();
    backend->GrowTo(4);
    EXPECT_NO_THROW(backend->GrowTo(4));
    EXPECT_EQ(backend->SizeInPages(), 4u);
}

TEST_P(IOBackendTest, WriteThenReadRoundTripsPageContents) {
    auto backend = MakeBackend();
    backend->GrowTo(1);

    Page write_page;
    write_page.SetPageId(7);
    write_page.SetChecksum(0xDEADBEEF);
    std::memset(write_page.payload(), 0x5A, Page::payload_size());
    backend->WritePage(0, write_page);

    Page read_page;
    backend->ReadPage(0, read_page);
    EXPECT_EQ(read_page.GetPageId(), 7u);
    EXPECT_EQ(read_page.GetChecksum(), 0xDEADBEEFu);
    for (size_t i = 0; i < Page::payload_size(); i++) {
        ASSERT_EQ(static_cast<uint8_t>(read_page.payload()[i]), 0x5A) << "byte " << i;
    }
}

TEST_P(IOBackendTest, DifferentPagesDoNotInterfereWithEachOther) {
    auto backend = MakeBackend();
    backend->GrowTo(3);

    for (page_id_t i = 0; i < 3; i++) {
        Page p;
        p.SetPageId(i);
        std::memset(p.payload(), static_cast<int>('A' + i), Page::payload_size());
        backend->WritePage(i, p);
    }
    for (page_id_t i = 0; i < 3; i++) {
        Page p;
        backend->ReadPage(i, p);
        EXPECT_EQ(p.GetPageId(), i);
        EXPECT_EQ(static_cast<uint8_t>(p.payload()[0]), static_cast<uint8_t>('A' + i));
    }
}

TEST_P(IOBackendTest, GrowingWithoutWritingReadsBackAsZero) {
    auto backend = MakeBackend();
    backend->GrowTo(1);
    Page p;
    std::memset(p.raw(), 0xFF, Page::size()); // seed with garbage so we can tell if it changed
    backend->ReadPage(0, p);
    for (size_t i = 0; i < Page::size(); i++) {
        ASSERT_EQ(static_cast<uint8_t>(p.raw()[i]), 0) << "byte " << i;
    }
}

TEST_P(IOBackendTest, DataSurvivesReopeningTheBackend) {
    {
        auto backend = MakeBackend();
        backend->GrowTo(2);
        Page p;
        p.SetPageId(55);
        backend->WritePage(1, p);
        backend->Sync();
    } // destructed here -- underlying fd closed

    auto backend2 = MakeBackend();
    EXPECT_EQ(backend2->SizeInPages(), 2u);
    Page read_page;
    backend2->ReadPage(1, read_page);
    EXPECT_EQ(read_page.GetPageId(), 55u);
}

TEST_P(IOBackendTest, SyncDoesNotThrowOnAHealthyFile) {
    auto backend = MakeBackend();
    backend->GrowTo(1);
    EXPECT_NO_THROW(backend->Sync());
}

INSTANTIATE_TEST_SUITE_P(
    BufferedAndDirect, IOBackendTest,
    ::testing::Values(IOStrategy::K_BUFFERED, IOStrategy::K_DIRECT),
    [](const ::testing::TestParamInfo<IOStrategy>& info) {
        return info.param == IOStrategy::K_BUFFERED ? "Buffered" : "Direct";
    });
