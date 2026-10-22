#pragma once
/**
 * blocks/mapping/psk8.hpp  --  M6: 8-PSK modulator + demodulator blocks.
 *
 * Theory:
 *   M = 8, k = 3 bits/sym.
 *   Gray-coded constellation points on unit circle (Es = 1).
 *   BER approx (2/log2 M) * Q(sqrt(2 * log2(M) * Eb/N0) * sin(pi/M))
 *            = (2/3) * Q(sqrt(6 * Eb/N0) * sin(pi/8))
 *   [Proakis & Salehi, Communication Systems Engineering, 2nd ed. eq 7.4-23]
 */
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include "../../engine/block.hpp"

namespace blocks {

class Psk8Mod : public engine::Block {
public:
    const char* name() const override { return "Psk8Mod"; }

    static double theoryBer(double ebn0_lin) {
        // Pb approx (2/3) * Q(sqrt(6 * Eb/N0) * sin(pi/8))
        const double sin_pi_8 = 0.38268343236508977173;
        const double arg = std::sqrt(6.0 * ebn0_lin) * sin_pi_8;
        return (2.0 / 3.0) * 0.5 * std::erfc(arg / 1.41421356237309504880);
    }

    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || !in[0] || out.empty() || !out[0]) return;
        const size_t in_n = in[0]->buf.empty() ? n : in[0]->buf.size();
        const size_t num_syms = in_n / 3;
        out[0]->resize(num_syms);

        const auto& b = in[0]->buf;
        auto& s = out[0]->buf;

        // Gray mapping: 3-bit pattern -> sector angle (multiple of pi/4)
        // 000->0, 001->1, 011->2, 010->3, 110->4, 111->5, 101->6, 100->7
        static const int pattern_to_sector[8] = {
            0, // 000 -> 0
            1, // 001 -> 1
            3, // 010 -> 3
            2, // 011 -> 2
            7, // 100 -> 7
            6, // 101 -> 6
            4, // 110 -> 4
            5  // 111 -> 5
        };

        const double pi_4 = 0.78539816339744830962;

        for (size_t i = 0; i < num_syms; ++i) {
            const int b2 = (b[3 * i + 0].real() > 0.5f) ? 1 : 0;
            const int b1 = (b[3 * i + 1].real() > 0.5f) ? 1 : 0;
            const int b0 = (b[3 * i + 2].real() > 0.5f) ? 1 : 0;
            const int val = (b2 << 2) | (b1 << 1) | b0;

            const int sector = pattern_to_sector[val];
            const double theta = sector * pi_4;
            s[i] = engine::cf(static_cast<float>(std::cos(theta)),
                              static_cast<float>(std::sin(theta)));
        }
    }
};

class Psk8Demod : public engine::Block {
public:
    const char* name() const override { return "Psk8Demod"; }

    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || !in[0] || out.empty() || !out[0]) return;
        const size_t in_n = in[0]->buf.empty() ? n : in[0]->buf.size();
        out[0]->resize(in_n * 3);

        const auto& r = in[0]->buf;
        auto& b = out[0]->buf;

        // Sector to 3 bits Gray mapping:
        // Sector 0 -> 000 (0)
        // Sector 1 -> 001 (1)
        // Sector 2 -> 011 (3)
        // Sector 3 -> 010 (2)
        // Sector 4 -> 110 (6)
        // Sector 5 -> 111 (7)
        // Sector 6 -> 101 (5)
        // Sector 7 -> 100 (4)
        static const int sector_to_bits[8] = {
            0b000, 0b001, 0b011, 0b010,
            0b110, 0b111, 0b101, 0b100
        };

        const double two_pi = 6.28318530717958647692;
        const double pi_8   = 0.39269908169872415481;
        const double pi_4   = 0.78539816339744830962;

        for (size_t i = 0; i < in_n; ++i) {
            double ang = std::atan2(r[i].imag(), r[i].real());
            if (ang < 0.0) ang += two_pi;

            int sector = static_cast<int>(std::floor((ang + pi_8) / pi_4)) % 8;
            int bits = sector_to_bits[sector];

            b[3 * i + 0] = engine::cf(static_cast<float>((bits >> 2) & 1), 0.0f);
            b[3 * i + 1] = engine::cf(static_cast<float>((bits >> 1) & 1), 0.0f);
            b[3 * i + 2] = engine::cf(static_cast<float>((bits >> 0) & 1), 0.0f);
        }
    }
};

}  // namespace blocks
