#pragma once

#include <cstdint>

namespace tinydb {

    struct byte_order {

        static void WriteU32BigEndian(uint32_t value, char* buffer) {
            buffer[0] = static_cast<char>((value >> 24) & 0xFF);
            buffer[1] = static_cast<char>((value >> 16) & 0xFF);
            buffer[2] = static_cast<char>((value >> 8) & 0xFF);
            buffer[3] = static_cast<char>(value & 0xFF);
        }
        
        static void WriteU64BigEndian(uint64_t value, char* buffer) {
            WriteU32BigEndian(static_cast<uint32_t>((value >> 32) & 0xFFFFFFFF), buffer);
            WriteU32BigEndian(static_cast<uint32_t>(value & 0xFFFFFFFF), buffer + 4);
        }

        static void WriteU16BigEndian(uint16_t value, char* buffer) {
            buffer[0] = static_cast<char>((value >> 8) & 0xFF);
            buffer[1] = static_cast<char>(value & 0xFF);
        }

        static void ReadU32BigEndian(uint32_t& value, const char* buffer) {
            value = static_cast<uint32_t>(static_cast<uint8_t>(buffer[0])) << 24 
            | static_cast<uint32_t>(static_cast<uint8_t>(buffer[1])) << 16 
            | static_cast<uint32_t>(static_cast<uint8_t>(buffer[2])) << 8 
            | static_cast<uint32_t>(static_cast<uint8_t>(buffer[3]));
        }

        static void ReadU64BigEndian(uint64_t& value, const char* buffer) {
            uint32_t hi, lo;
            ReadU32BigEndian(hi, buffer);
            ReadU32BigEndian(lo, buffer + 4);
            value = static_cast<uint64_t>(hi) << 32 | lo;
        }

        static void ReadU16BigEndian(uint16_t& value, const char* buffer) {
            value = static_cast<uint16_t>(static_cast<uint8_t>(buffer[0])) << 8 
            | static_cast<uint16_t>(static_cast<uint8_t>(buffer[1]));
        }
    };
}