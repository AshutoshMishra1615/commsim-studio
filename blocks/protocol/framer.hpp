#pragma once
/**
 * blocks/protocol/framer.hpp  --  M7: Protocol Framer & Deframer
 *
 * Implements:
 *   - Frame encapsulation: Preamble + Header (Type, Seq, Len) + Payload + CRC-16.
 *   - Deframer: validates sync word, verifies CRC-16, discards corrupted frames.
 */
#include <algorithm>
#include <cstdint>
#include <vector>
#include "../coding/crc16.hpp"

namespace blocks {

struct Frame {
    static constexpr uint16_t PREAMBLE = 0xAA7E; // Sync marker
    uint16_t sync = PREAMBLE;
    uint8_t  type = 0;       // 0: DATA, 1: ACK, 2: NACK
    uint16_t seq_num = 0;
    uint16_t length = 0;
    std::vector<uint8_t> payload;
    uint16_t crc = 0;

    /// Serialize frame to byte buffer
    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> bytes;
        bytes.reserve(7 + payload.size() + 2);

        bytes.push_back(static_cast<uint8_t>((sync >> 8) & 0xFF));
        bytes.push_back(static_cast<uint8_t>(sync & 0xFF));
        bytes.push_back(type);
        bytes.push_back(static_cast<uint8_t>((seq_num >> 8) & 0xFF));
        bytes.push_back(static_cast<uint8_t>(seq_num & 0xFF));

        const uint16_t len = static_cast<uint16_t>(payload.size());
        bytes.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        bytes.push_back(static_cast<uint8_t>(len & 0xFF));

        bytes.insert(bytes.end(), payload.begin(), payload.end());

        // Compute CRC-16 over Header + Payload
        const uint16_t calculated_crc = crc16(bytes.data() + 2, bytes.size() - 2);
        bytes.push_back(static_cast<uint8_t>((calculated_crc >> 8) & 0xFF));
        bytes.push_back(static_cast<uint8_t>(calculated_crc & 0xFF));

        return bytes;
    }

    /// Deserialize and validate frame from byte buffer
    static bool deserialize(const uint8_t* data, size_t size, Frame& out) {
        if (!data || size < 9) return false;

        const uint16_t sync = (static_cast<uint16_t>(data[0]) << 8) | data[1];
        if (sync != PREAMBLE) return false;

        out.sync = sync;
        out.type = data[2];
        out.seq_num = (static_cast<uint16_t>(data[3]) << 8) | data[4];
        out.length = (static_cast<uint16_t>(data[5]) << 8) | data[6];

        if (size < 7 + static_cast<size_t>(out.length) + 2) return false;

        out.payload.assign(data + 7, data + 7 + out.length);

        const uint16_t received_crc = (static_cast<uint16_t>(data[7 + out.length]) << 8) |
                                      data[7 + out.length + 1];
        const uint16_t calculated_crc = crc16(data + 2, 5 + out.length);

        out.crc = received_crc;
        return (received_crc == calculated_crc);
    }
};

}  // namespace blocks
