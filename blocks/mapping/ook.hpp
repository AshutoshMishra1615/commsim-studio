#pragma once
/**
 * blocks/mapping/ook.hpp  --  M1: Binary OOK (On-Off Keying) / ASK mod/demod.
 *
 * Amplitudes: bit 0 → 0+0j, bit 1 → √2+0j. Es = (0² + 2)/2 = 1.
 * Decision threshold at √2/2.
 * BER = Q(sqrt(Eb/N0))  [same as coherent BFSK; Proakis & Salehi §5.4]
 *
 * OokMod:   in[0] bits → out[0] symbols
 * OokDemod: in[0] received → out[0] decisions (real ∈ {0,1})
 */
#include <cmath>
#include "../../engine/block.hpp"

namespace blocks {

class OokMod : public engine::Block {
public:
    const char* name() const override { return "OokMod"; }

    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || !in[0] || out.empty() || !out[0]) return;
        out[0]->resize(n);
        const auto& b = in[0]->buf;
        auto& s = out[0]->buf;
        static constexpr float kAmp = 1.41421356f; // sqrt(2)
        for (size_t i = 0; i < n; ++i)
            s[i] = engine::cf(b[i].real() > 0.5f ? kAmp : 0.f, 0.f);
    }
};

class OokDemod : public engine::Block {
public:
    const char* name() const override { return "OokDemod"; }

    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || !in[0] || out.empty() || !out[0]) return;
        out[0]->resize(n);
        const auto& r = in[0]->buf;
        auto& b = out[0]->buf;
        static constexpr float kThr = 0.70710678f; // sqrt(2)/2
        for (size_t i = 0; i < n; ++i)
            b[i] = engine::cf(r[i].real() > kThr ? 1.f : 0.f, 0.f);
    }
};

}  // namespace blocks
