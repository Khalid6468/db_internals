#include "tinydb/byte_order.h"
#include <gtest/gtest.h>
#include <cstring>

using namespace tinydb;

TEST(ByteOrder, WriteU16BigEndianLaysOutMostSignificantByteFirst) {
    char buf[2] = {};
    byte_order::WriteU16BigEndian(0x1234, buf);
    EXPECT_EQ(static_cast<uint8_t>(buf[0]), 0x12);
    EXPECT_EQ(static_cast<uint8_t>(buf[1]), 0x34);
}

TEST(ByteOrder, WriteU32BigEndianLaysOutMostSignificantByteFirst) {
    char buf[4] = {};
    byte_order::WriteU32BigEndian(0x12345678, buf);
    EXPECT_EQ(static_cast<uint8_t>(buf[0]), 0x12);
    EXPECT_EQ(static_cast<uint8_t>(buf[1]), 0x34);
    EXPECT_EQ(static_cast<uint8_t>(buf[2]), 0x56);
    EXPECT_EQ(static_cast<uint8_t>(buf[3]), 0x78);
}

TEST(ByteOrder, WriteU64BigEndianLaysOutMostSignificantByteFirst) {
    char buf[8] = {};
    byte_order::WriteU64BigEndian(0x0123456789ABCDEFULL, buf);
    const uint8_t expected[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
    for (int i = 0; i < 8; i++) {
        EXPECT_EQ(static_cast<uint8_t>(buf[i]), expected[i]) << "byte " << i;
    }
}

TEST(ByteOrder, U16RoundTrip) {
    for (uint16_t value : {static_cast<uint16_t>(0), static_cast<uint16_t>(1),
                            static_cast<uint16_t>(0x00FF), static_cast<uint16_t>(0xFF00),
                            static_cast<uint16_t>(0xFFFF), static_cast<uint16_t>(0x1234)}) {
        char buf[2];
        byte_order::WriteU16BigEndian(value, buf);
        uint16_t out = 0;
        byte_order::ReadU16BigEndian(out, buf);
        EXPECT_EQ(out, value);
    }
}

TEST(ByteOrder, U32RoundTrip) {
    for (uint32_t value : {0u, 1u, 0x000000FFu, 0xFF000000u, 0xFFFFFFFFu, 0xDEADBEEFu}) {
        char buf[4];
        byte_order::WriteU32BigEndian(value, buf);
        uint32_t out = 0;
        byte_order::ReadU32BigEndian(out, buf);
        EXPECT_EQ(out, value);
    }
}

TEST(ByteOrder, U64RoundTrip) {
    for (uint64_t value : {0ULL, 1ULL, 0x00000000FFFFFFFFULL, 0xFFFFFFFF00000000ULL,
                            0xFFFFFFFFFFFFFFFFULL, 0x0123456789ABCDEFULL}) {
        char buf[8];
        byte_order::WriteU64BigEndian(value, buf);
        uint64_t out = 0;
        byte_order::ReadU64BigEndian(out, buf);
        EXPECT_EQ(out, value);
    }
}

TEST(ByteOrder, ReadDoesNotDependOnHostSignedness) {
    // buffer bytes with the high bit set must not sign-extend into the result
    char buf[4] = {static_cast<char>(0xFF), static_cast<char>(0xFF),
                    static_cast<char>(0xFF), static_cast<char>(0xFF)};
    uint32_t out = 0;
    byte_order::ReadU32BigEndian(out, buf);
    EXPECT_EQ(out, 0xFFFFFFFFu);
}

TEST(ByteOrder, WritesAreIndependentAdjacentCallsDontOverlap) {
    char buf[8] = {};
    byte_order::WriteU32BigEndian(0xAAAAAAAA, buf);
    byte_order::WriteU32BigEndian(0xBBBBBBBB, buf + 4);
    uint32_t first = 0, second = 0;
    byte_order::ReadU32BigEndian(first, buf);
    byte_order::ReadU32BigEndian(second, buf + 4);
    EXPECT_EQ(first, 0xAAAAAAAAu);
    EXPECT_EQ(second, 0xBBBBBBBBu);
}
