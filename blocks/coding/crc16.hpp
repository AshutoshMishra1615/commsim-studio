#pragma once
/**
 * blocks/coding/crc16.hpp  --  M5: CRC-16 (CCITT-FALSE) error detection.
 *
 * Parameters:
 *   Polynomial: 0x1021 (x^16 + x^12 + x^5 + 1)
 *   Initial value: 0xFFFF
 *   Final XOR: 0x0000
 *   Reflect in/out: False
 *
 * Theory:
 *   A CRC with degree-16 generator polynomial detects 100% of error bursts
 *   of length <= 16 bits for any message length.
 */
#include <cstdint>
#include <vector>
#include "../../engine/block.hpp"

namespace blocks {

/// Standalone CRC-16 calculation over byte buffer
inline uint16_t crc16(const uint8_t* d, size_t n) {
    uint16_t c = 0xFFFF;
    for (size_t i = 0; i < n; ++i) {
        c ^= static_cast<uint16_t>(d[i]) << 8;
        for (int k = 0; k < 8; ++k) {
            c = (c & 0x8000) ? static_cast<uint16_t>((c << 1) ^ 0x1021) : static_cast<uint16_t>(c << 1);
        }
    }
    return c;
}

/// Standalone CRC-16 calculation over bit buffer (each element 0 or 1)
inline uint16_t crc16_bits(const std::vector<uint8_t>& bits) {
    std::vector<uint8_t> bytes((bits.size() + 7) / 8, 0);
    for (size_t i = 0; i < bits.size(); ++i) {
        if (bits[i]) {
            bytes[i / 8] |= static_cast<uint8_t>(1u << (7 - (i % 8)));
        }
    }
    return crc16(bytes.data(), bytes.size());
}

/**
 * Crc16Encoder:
 * Appends 16-bit CRC to input bit stream.
 * In: n bits. Out: n + 16 bits.
 */
class Crc16Encoder : public engine::Block {
public:
    const char* name() const override { return "Crc16Encoder"; }

    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || !in[0] || out.empty() || !out[0]) return;
        const size_t in_n = in[0]->buf.size() > 0 ? in[0]->buf.size() : n;

        std::vector<uint8_t> bits(in_n);
        for (size_t i = 0; i < in_n; ++i) {
            bits[i] = (in[0]->buf[i].real() > 0.5f) ? 1 : 0;
        }

        const uint16_t crc = crc16_bits(bits);

        out[0]->resize(in_n + 16);
        for (size_t i = 0; i < in_n; ++i) {
            out[0]->buf[i] = in[0]->buf[i];
        }
        for (size_t k = 0; k < 16; ++k) {
            const float bit = ((crc >> (15 - k)) & 1u) ? 1.0f : 0.0f;
            out[0]->buf[in_n + k] = engine::cf(bit, 0.0f);
        }
    }
};

/**
 * Crc16Decoder:
 * Checks 16-bit CRC on incoming frame.
 * If pass, outputs data bits (or sets error flag).
 */
class Crc16Decoder : public engine::Block {
public:
    const char* name() const override { return "Crc16Decoder"; }

    bool lastValid() const { return last_valid_; }
    uint64_t totalFrames() const { return total_frames_; }
    uint64_t passedFrames() const { return passed_frames_; }

    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || !in[0] || out.empty() || !out[0]) return;
        const size_t in_n = in[0]->buf.size() > 0 ? in[0]->buf.size() : n;
        if (in_n <= 16) return;

        const size_t payload_len = in_n - 16;
        std::vector<uint8_t> payload_bits(payload_len);
        for (size_t i = 0; i < payload_len; ++i) {
            payload_bits[i] = (in[0]->buf[i].real() > 0.5f) ? 1 : 0;
        }

        const uint16_t expected_crc = crc16_bits(payload_bits);
        uint16_t received_crc = 0;
        for (size_t k = 0; k < 16; ++k) {
            if (in[0]->buf[payload_len + k].real() > 0.5f) {
                received_crc |= static_cast<uint16_t>(1u << (15 - k));
            }
        }

        last_valid_ = (expected_crc == received_crc);
        total_frames_++;
        if (last_valid_) passed_frames_++;

        out[0]->resize(payload_len);
        for (size_t i = 0; i < payload_len; ++i) {
            out[0]->buf[i] = in[0]->buf[i];
        }
    }

private:
    bool last_valid_ = false;
    uint64_t total_frames_ = 0;
    uint64_t passed_frames_ = 0;
};

}  // namespace blocks
