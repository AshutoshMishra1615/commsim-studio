#pragma once
/**
 * blocks/coding/conv_k7.hpp  --  M5: Rate-1/2, K=7 convolutional encoder.
 *
 * Theory:
 *   NASA / CCSDS / IEEE 802.11 industry standard convolutional code.
 *   Constraint length: K = 7 (64 states).
 *   Rate: R = 1/2.
 *   Generator polynomials:
 *     G0 = 0171 (octal) = 135 = 1111001 (binary)
 *     G1 = 0133 (octal) = 91  = 1011011 (binary)
 *   Free distance: d_free = 10.
 *
 *   Terminated with 6 zero tail bits, producing 2*(N + 6) output bits
 *   for an N-bit input message.
 */
#include <vector>
#include <cstdint>
#include "../../engine/block.hpp"

namespace blocks {

struct ConvK7 {
    static constexpr unsigned G0 = 0171;
    static constexpr unsigned G1 = 0133;

    static int par(unsigned x) {
        return __builtin_parity(x);
    }

    /// Encode bit vector; appends 6 tail bits -> output length 2 * (in.size() + 6)
    static std::vector<uint8_t> encode(const std::vector<uint8_t>& in) {
        std::vector<uint8_t> out;
        out.reserve(2 * (in.size() + 6));
        unsigned state = 0;

        auto step = [&](unsigned b) {
            const unsigned reg = (b << 6) | state;
            out.push_back(static_cast<uint8_t>(par(reg & G0)));
            out.push_back(static_cast<uint8_t>(par(reg & G1)));
            state = reg >> 1;
        };

        for (uint8_t b : in) {
            step(b ? 1u : 0u);
        }
        for (int i = 0; i < 6; ++i) {
            step(0u);
        }
        return out;
    }
};

class ConvK7Encoder : public engine::Block {
public:
    const char* name() const override { return "ConvK7Encoder"; }

    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || !in[0] || out.empty() || !out[0]) return;
        const size_t in_n = in[0]->buf.size() > 0 ? in[0]->buf.size() : n;

        const size_t out_n = 2 * (in_n + 6);
        out[0]->resize(out_n);

        const auto& x = in[0]->buf;
        auto& y = out[0]->buf;

        unsigned state = 0;
        size_t out_idx = 0;

        auto step = [&](unsigned b) {
            const unsigned reg = (b << 6) | state;
            const float bit0 = static_cast<float>(ConvK7::par(reg & ConvK7::G0));
            const float bit1 = static_cast<float>(ConvK7::par(reg & ConvK7::G1));
            y[out_idx++] = engine::cf(bit0, 0.0f);
            y[out_idx++] = engine::cf(bit1, 0.0f);
            state = reg >> 1;
        };

        for (size_t i = 0; i < in_n; ++i) {
            step(x[i].real() > 0.5f ? 1u : 0u);
        }
        for (int i = 0; i < 6; ++i) {
            step(0u);
        }
    }
};

}  // namespace blocks
