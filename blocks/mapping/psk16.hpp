#pragma once
/**
 * blocks/mapping/psk16.hpp  --  M6: 16-PSK modulator + demodulator blocks.
 *
 * Theory:
 *   M = 16, k = 4 bits/sym.
 *   Gray-coded constellation points on unit circle (Es = 1).
 *   BER approx (2/log2 M) * Q(sqrt(2 * log2(M) * Eb/N0) * sin(pi/M))
 *            = (1/2) * Q(sqrt(8 * Eb/N0) * sin(pi/16))
 *   [Proakis & Salehi, Communication Systems Engineering, 2nd ed. eq 7.4-23]
 */
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include "../../engine/block.hpp"

namespace blocks {

class Psk16Mod : public engine::Block {
public:
    const char* name() const override { return "Psk16Mod"; }

    static double theoryBer(double ebn0_lin) {
        const double sin_pi_16 = 0.19509032201612826785;
        const double arg = std::sqrt(8.0 * ebn0_lin) * sin_pi_16;
        return 0.5 * 0.5 * std::erfc(arg / 1.41421356237309504880);
    }

    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || !in[0] || out.empty() || !out[0]) return;
        const size_t in_n = in[0]->buf.empty() ? n : in[0]->buf.size();
        const size_t num_syms = in_n / 4;
        out[0]->resize(num_syms);

        const auto& b = in[0]->buf;
        auto& s = out[0]->buf;

        const double pi_8 = 0.39269908169872415481;

        for (size_t i = 0; i < num_syms; ++i) {
            const int g3 = (b[4 * i + 0].real() > 0.5f) ? 1 : 0;
            const int g2 = (b[4 * i + 1].real() > 0.5f) ? 1 : 0;
            const int g1 = (b[4 * i + 2].real() > 0.5f) ? 1 : 0;
            const int g0 = (b[4 * i + 3].real() > 0.5f) ? 1 : 0;

            // Gray to binary conversion
            const int b3 = g3;
            const int b2 = b3 ^ g2;
            const int b1 = b2 ^ g1;
            const int b0 = b1 ^ g0;
            const int sector = (b3 << 3) | (b2 << 2) | (b1 << 1) | b0;

            const double theta = sector * pi_8;
            s[i] = engine::cf(static_cast<float>(std::cos(theta)),
                              static_cast<float>(std::sin(theta)));
        }
    }
};

class Psk16Demod : public engine::Block {
public:
    const char* name() const override { return "Psk16Demod"; }

    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || !in[0] || out.empty() || !out[0]) return;
        const size_t in_n = in[0]->buf.empty() ? n : in[0]->buf.size();
        out[0]->resize(in_n * 4);

        const auto& r = in[0]->buf;
        auto& b = out[0]->buf;

        const double two_pi = 6.28318530717958647692;
        const double pi_16  = 0.19509032201612826785;
        const double pi_8   = 0.39269908169872415481;

        for (size_t i = 0; i < in_n; ++i) {
            double ang = std::atan2(r[i].imag(), r[i].real());
            if (ang < 0.0) ang += two_pi;

            const int sector = static_cast<int>(std::floor((ang + pi_16) / pi_8)) % 16;
            // Binary to Gray conversion
            const int gray = sector ^ (sector >> 1);

            b[4 * i + 0] = engine::cf(static_cast<float>((gray >> 3) & 1), 0.0f);
            b[4 * i + 1] = engine::cf(static_cast<float>((gray >> 2) & 1), 0.0f);
            b[4 * i + 2] = engine::cf(static_cast<float>((gray >> 1) & 1), 0.0f);
            b[4 * i + 3] = engine::cf(static_cast<float>((gray >> 0) & 1), 0.0f);
        }
    }
};

}  // namespace blocks
