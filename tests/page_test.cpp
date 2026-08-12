#include "tinydb/page.h"
#include <gtest/gtest.h>
#include <cstring>

using namespace tinydb;

TEST(Page, HeaderSizeIsExactly20Bytes) {
    EXPECT_EQ(PageHeader::SIZE, 20u);
}

TEST(Page, PayloadStartsRightAfterHeaderAndFillsRestOfPage) {
    Page page;
    EXPECT_EQ(page.payload(), page.raw() + PageHeader::SIZE);
    EXPECT_EQ(Page::payload_size(), PAGE_SIZE - PageHeader::SIZE);
    EXPECT_EQ(Page::payload_size() + PageHeader::SIZE, Page::size());
}

TEST(Page, SetPageIdGetPageIdRoundTrip) {
    Page page;
    page.SetPageId(0x12345678);
    EXPECT_EQ(page.GetPageId(), 0x12345678u);
}

TEST(Page, SetPageIdIsStoredBigEndianAtOffsetZero) {
    Page page;
    page.SetPageId(0x12345678);
    EXPECT_EQ(static_cast<uint8_t>(page.raw()[0]), 0x12);
    EXPECT_EQ(static_cast<uint8_t>(page.raw()[1]), 0x34);
    EXPECT_EQ(static_cast<uint8_t>(page.raw()[2]), 0x56);
    EXPECT_EQ(static_cast<uint8_t>(page.raw()[3]), 0x78);
}

TEST(Page, SetChecksumGetChecksumRoundTrip) {
    Page page;
    page.SetChecksum(0xDEADBEEF);
    EXPECT_EQ(page.GetChecksum(), 0xDEADBEEFu);
}

TEST(Page, SetLsnGetLsnRoundTrip) {
    Page page;
    page.SetLsn(0x0123456789ABCDEFULL);
    EXPECT_EQ(page.GetLsn(), 0x0123456789ABCDEFULL);
}

TEST(Page, SetLsnZeroAndMaxRoundTrip) {
    Page page;
    page.SetLsn(0);
    EXPECT_EQ(page.GetLsn(), 0u);
    page.SetLsn(UINT64_MAX);
    EXPECT_EQ(page.GetLsn(), UINT64_MAX);
}

TEST(Page, SetPageTypeGetPageTypeRoundTrip) {
    Page page;
    page.SetPageType(7);
    EXPECT_EQ(page.GetPageType(), 7);
}

TEST(Page, HeaderFieldsAreIndependentOfEachOther) {
    Page page;
    page.SetPageId(1);
    page.SetChecksum(2);
    page.SetLsn(3);
    page.SetPageType(4);
    EXPECT_EQ(page.GetPageId(), 1u);
    EXPECT_EQ(page.GetChecksum(), 2u);
    EXPECT_EQ(page.GetLsn(), 3u);
    EXPECT_EQ(page.GetPageType(), 4);
}

TEST(Page, WritePageHeaderThenReadPageHeaderRoundTrips) {
    Page page;
    PageHeader header{};
    header.page_id = 42;
    header.checksum = 0xCAFEBABE;
    header.lsn_hi = 0x11111111;
    header.lsn_lo = 0x22222222;
    header.page_type = 5;
    header.reserved = 0;
    page.WritePageHeader(header);

    PageHeader read_back{};
    page.ReadPageHeader(read_back);
    EXPECT_EQ(read_back.page_id, header.page_id);
    EXPECT_EQ(read_back.checksum, header.checksum);
    EXPECT_EQ(read_back.lsn_hi, header.lsn_hi);
    EXPECT_EQ(read_back.lsn_lo, header.lsn_lo);
    EXPECT_EQ(read_back.page_type, header.page_type);
    EXPECT_EQ(read_back.reserved, header.reserved);
}

TEST(Page, ReadPageHeaderWorksThroughConstReference) {
    Page page;
    page.SetPageId(99);
    const Page& const_page = page;
    PageHeader header{};
    const_page.ReadPageHeader(header);
    EXPECT_EQ(header.page_id, 99u);
}

TEST(Page, DeserializeFromMatchesWritePageHeader) {
    Page page;
    PageHeader header{};
    header.page_id = 7;
    header.checksum = 8;
    header.lsn_hi = 9;
    header.lsn_lo = 10;
    header.page_type = 11;
    header.reserved = 12;
    page.WritePageHeader(header);

    PageHeader deserialized = Page::DeserializeFrom(page.raw());
    EXPECT_EQ(deserialized.page_id, header.page_id);
    EXPECT_EQ(deserialized.checksum, header.checksum);
    EXPECT_EQ(deserialized.lsn_hi, header.lsn_hi);
    EXPECT_EQ(deserialized.lsn_lo, header.lsn_lo);
    EXPECT_EQ(deserialized.page_type, header.page_type);
    EXPECT_EQ(deserialized.reserved, header.reserved);
}

TEST(Page, WritingHeaderDoesNotTouchPayload) {
    Page page;
    std::memset(page.raw(), 0, Page::size());
    std::memset(page.payload(), 0x7A, Page::payload_size());

    page.SetPageId(123);
    page.SetChecksum(456);
    page.SetLsn(789);
    page.SetPageType(1);

    for (size_t i = 0; i < Page::payload_size(); i++) {
        ASSERT_EQ(static_cast<uint8_t>(page.payload()[i]), 0x7A) << "payload byte " << i << " was clobbered";
    }
}

TEST(Page, PayloadIsWritableAndReadableAcrossItsFullSize) {
    Page page;
    for (size_t i = 0; i < Page::payload_size(); i++) {
        page.payload()[i] = static_cast<char>(i % 256);
    }
    for (size_t i = 0; i < Page::payload_size(); i++) {
        ASSERT_EQ(static_cast<uint8_t>(page.payload()[i]), static_cast<uint8_t>(i % 256));
    }
}
