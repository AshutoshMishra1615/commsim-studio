#pragma once
/**
 * blocks/mapping/qam64.hpp  --  M6: 64-QAM modulator + demodulator blocks.
 *
 * Theory:
 *   M = 64, k = 6 bits/sym (3 bits for I, 3 bits for Q).
 *   Square 8x8 constellation, normalised to average symbol energy Es = 1.
 *   Levels: {-7, -5, -3, -1, 1, 3, 5, 7} / sqrt(42).
 *   Gray-coded mapping per axis.
 *   Theoretical BER:
 *     Pb approx (7/12) * Q(sqrt((2/7) * Eb/N0))
 *   [Proakis & Salehi, Communication Systems Engineering, 2nd ed. eq 7.4-44]
 */
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include "../../engine/block.hpp"

namespace blocks {

class Qam64Mod : public engine::Block {
public:
    const char* name() const override { return "Qam64Mod"; }

    static double theoryBer(double ebn0_lin) {
        // Pb approx (7/12) * Q(sqrt((2/7) * Eb/N0))
        const double arg = std::sqrt((2.0 / 7.0) * ebn0_lin);
        return (7.0 / 12.0) * 0.5 * std::erfc(arg / 1.41421356237309504880);
    }

    static inline float map3Bits(int b2, int b1, int b0) {
        // 3 bits -> Gray coded level in {-7, -5, -3, -1, 1, 3, 5, 7} / sqrt(42)
        // 000->-7, 001->-5, 011->-3, 010->-1, 110->+1, 111->+3, 101->+5, 100->+7
        static const float levels[8] = {
            -7.0f, // 000 (0)
            -5.0f, // 001 (1)
            -1.0f, // 010 (2)
            -3.0f, // 011 (3)
            +7.0f, // 100 (4)
            +5.0f, // 101 (5)
            +1.0f, // 110 (6)
            +3.0f  // 111 (7)
        };
        const int val = (b2 << 2) | (b1 << 1) | b0;
        constexpr float inv_sqrt42 = 0.1543033499620919f;
        return levels[val] * inv_sqrt42;
    }

    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || !in[0] || out.empty() || !out[0]) return;
        const size_t in_n = in[0]->buf.empty() ? n : in[0]->buf.size();
        const size_t num_syms = in_n / 6;
        out[0]->resize(num_syms);

        const auto& b = in[0]->buf;
        auto& s = out[0]->buf;

        for (size_t i = 0; i < num_syms; ++i) {
            const int i2 = (b[6 * i + 0].real() > 0.5f) ? 1 : 0;
            const int i1 = (b[6 * i + 1].real() > 0.5f) ? 1 : 0;
            const int i0 = (b[6 * i + 2].real() > 0.5f) ? 1 : 0;

            const int q2 = (b[6 * i + 3].real() > 0.5f) ? 1 : 0;
            const int q1 = (b[6 * i + 4].real() > 0.5f) ? 1 : 0;
            const int q0 = (b[6 * i + 5].real() > 0.5f) ? 1 : 0;

            s[i] = engine::cf(map3Bits(i2, i1, i0), map3Bits(q2, q1, q0));
        }
    }
};

class Qam64Demod : public engine::Block {
public:
    const char* name() const override { return "Qam64Demod"; }

    static inline void slice(float x, uint8_t& b2, uint8_t& b1, uint8_t& b0) {
        constexpr float d = 0.1543033499620919f; // 1 / sqrt(42)
        // Thresholds: -6d, -4d, -2d, 0, 2d, 4d, 6d
        if (x < -6.0f * d)      { b2 = 0; b1 = 0; b0 = 0; } // -7
        else if (x < -4.0f * d) { b2 = 0; b1 = 0; b0 = 1; } // -5
        else if (x < -2.0f * d) { b2 = 0; b1 = 1; b0 = 1; } // -3
        else if (x < 0.0f)      { b2 = 0; b1 = 1; b0 = 0; } // -1
        else if (x < 2.0f * d)  { b2 = 1; b1 = 1; b0 = 0; } // +1
        else if (x < 4.0f * d)  { b2 = 1; b1 = 1; b0 = 1; } // +3
        else if (x < 6.0f * d)  { b2 = 1; b1 = 0; b0 = 1; } // +5
        else                    { b2 = 1; b1 = 0; b0 = 0; } // +7
    }

    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || !in[0] || out.empty() || !out[0]) return;
        const size_t in_n = in[0]->buf.empty() ? n : in[0]->buf.size();
        out[0]->resize(in_n * 6);

        const auto& r = in[0]->buf;
        auto& b = out[0]->buf;

        for (size_t i = 0; i < in_n; ++i) {
            uint8_t i2, i1, i0, q2, q1, q0;
            slice(r[i].real(), i2, i1, i0);
            slice(r[i].imag(), q2, q1, q0);

            b[6 * i + 0] = engine::cf(static_cast<float>(i2), 0.0f);
            b[6 * i + 1] = engine::cf(static_cast<float>(i1), 0.0f);
            b[6 * i + 2] = engine::cf(static_cast<float>(i0), 0.0f);

            b[6 * i + 3] = engine::cf(static_cast<float>(q2), 0.0f);
            b[6 * i + 4] = engine::cf(static_cast<float>(q1), 0.0f);
            b[6 * i + 5] = engine::cf(static_cast<float>(q0), 0.0f);
        }
    }
};

}  // namespace blocks
