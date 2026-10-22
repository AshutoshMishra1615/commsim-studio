#pragma once
/**
 * blocks/coding/hamming74.hpp  --  M5: Hamming (7,4) forward error correction.
 *
 * Theory:
 *   Rate R = 4/7 block code.
 *   Codeword bit layout: [p1, p2, d1, p3, d2, d3, d4]
 *   Minimum Hamming distance d_min = 3.
 *   Corrects 100% of single-bit errors in each 7-bit codeword:
 *     t = floor((d_min - 1) / 2) = 1.
 */
#include <vector>
#include <cstdint>
#include <algorithm>
#include "../../engine/block.hpp"

namespace blocks {

struct Hamming74 {
    /// Encode a vector of bits (size multiple of 4) into 7-bit codewords
    static std::vector<uint8_t> encode(const std::vector<uint8_t>& in) {
        std::vector<uint8_t> out;
        out.reserve(in.size() / 4 * 7);
        for (size_t i = 0; i + 3 < in.size(); i += 4) {
            const uint8_t d1 = in[i];
            const uint8_t d2 = in[i + 1];
            const uint8_t d3 = in[i + 2];
            const uint8_t d4 = in[i + 3];
            const uint8_t p1 = d1 ^ d2 ^ d4;
            const uint8_t p2 = d1 ^ d3 ^ d4;
            const uint8_t p3 = d2 ^ d3 ^ d4;
            out.push_back(p1);
            out.push_back(p2);
            out.push_back(d1);
            out.push_back(p3);
            out.push_back(d2);
            out.push_back(d3);
            out.push_back(d4);
        }
        return out;
    }

    /// Decode a vector of bits (size multiple of 7) with single-bit error correction
    static std::vector<uint8_t> decode(const std::vector<uint8_t>& rx) {
        std::vector<uint8_t> out;
        out.reserve(rx.size() / 7 * 4);
        for (size_t i = 0; i + 6 < rx.size(); i += 7) {
            uint8_t c[7];
            for (size_t j = 0; j < 7; ++j) c[j] = rx[i + j];

            const int s1 = c[0] ^ c[2] ^ c[4] ^ c[6];
            const int s2 = c[1] ^ c[2] ^ c[5] ^ c[6];
            const int s3 = c[3] ^ c[4] ^ c[5] ^ c[6];
            const int syn = s1 + 2 * s2 + 4 * s3;

            if (syn > 0 && syn <= 7) {
                c[syn - 1] ^= 1; // Correct single-bit error
            }
            out.push_back(c[2]); // d1
            out.push_back(c[4]); // d2
            out.push_back(c[5]); // d3
            out.push_back(c[6]); // d4
        }
        return out;
    }
};

class Hamming74Encoder : public engine::Block {
public:
    const char* name() const override { return "Hamming74Encoder"; }

    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || !in[0] || out.empty() || !out[0]) return;
        const size_t in_n = in[0]->buf.size() > 0 ? in[0]->buf.size() : n;
        const size_t blocks = in_n / 4;
        const size_t out_n = blocks * 7;
        out[0]->resize(out_n);

        const auto& x = in[0]->buf;
        auto& y = out[0]->buf;

        for (size_t b = 0; b < blocks; ++b) {
            const uint8_t d1 = x[b * 4 + 0].real() > 0.5f ? 1 : 0;
            const uint8_t d2 = x[b * 4 + 1].real() > 0.5f ? 1 : 0;
            const uint8_t d3 = x[b * 4 + 2].real() > 0.5f ? 1 : 0;
            const uint8_t d4 = x[b * 4 + 3].real() > 0.5f ? 1 : 0;
            const uint8_t p1 = d1 ^ d2 ^ d4;
            const uint8_t p2 = d1 ^ d3 ^ d4;
            const uint8_t p3 = d2 ^ d3 ^ d4;

            y[b * 7 + 0] = engine::cf(static_cast<float>(p1), 0.0f);
            y[b * 7 + 1] = engine::cf(static_cast<float>(p2), 0.0f);
            y[b * 7 + 2] = engine::cf(static_cast<float>(d1), 0.0f);
            y[b * 7 + 3] = engine::cf(static_cast<float>(p3), 0.0f);
            y[b * 7 + 4] = engine::cf(static_cast<float>(d2), 0.0f);
            y[b * 7 + 5] = engine::cf(static_cast<float>(d3), 0.0f);
            y[b * 7 + 6] = engine::cf(static_cast<float>(d4), 0.0f);
        }
    }
};

class Hamming74Decoder : public engine::Block {
public:
    const char* name() const override { return "Hamming74Decoder"; }

    uint64_t correctedErrors() const { return corrected_errors_; }

    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || !in[0] || out.empty() || !out[0]) return;
        const size_t in_n = in[0]->buf.size() > 0 ? in[0]->buf.size() : n;
        const size_t blocks = in_n / 7;
        const size_t out_n = blocks * 4;
        out[0]->resize(out_n);

        const auto& x = in[0]->buf;
        auto& y = out[0]->buf;

        for (size_t b = 0; b < blocks; ++b) {
            uint8_t c[7];
            for (size_t j = 0; j < 7; ++j) {
                c[j] = x[b * 7 + j].real() > 0.5f ? 1 : 0;
            }

            const int s1 = c[0] ^ c[2] ^ c[4] ^ c[6];
            const int s2 = c[1] ^ c[2] ^ c[5] ^ c[6];
            const int s3 = c[3] ^ c[4] ^ c[5] ^ c[6];
            const int syn = s1 + 2 * s2 + 4 * s3;

            if (syn > 0 && syn <= 7) {
                c[syn - 1] ^= 1;
                corrected_errors_++;
            }

            y[b * 4 + 0] = engine::cf(static_cast<float>(c[2]), 0.0f);
            y[b * 4 + 1] = engine::cf(static_cast<float>(c[4]), 0.0f);
            y[b * 4 + 2] = engine::cf(static_cast<float>(c[5]), 0.0f);
            y[b * 4 + 3] = engine::cf(static_cast<float>(c[6]), 0.0f);
        }
    }

private:
    uint64_t corrected_errors_ = 0;
};

}  // namespace blocks
