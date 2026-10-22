#pragma once
/**
 * blocks/mapping/qpsk.hpp  --  M1: QPSK modulator + demodulator blocks.
 *
 * Gray mapping: bit-pair (b0,b1) → symbol on ±1/√2 axes.
 * 00→(+a,+a), 01→(+a,-a), 10→(-a,+a), 11→(-a,-a), a=1/√2, Es=1.
 * BER = Q(sqrt(2 Eb/N0))  [same as BPSK per axis]
 *
 * QpskMod:
 *   in[0]  : bit stream as cf (real ∈ {0,1}), consumed in pairs
 *   out[0] : QPSK symbols (cf, Es=1)
 *
 * QpskDemod:
 *   in[0]  : received symbols
 *   out[0] : decoded bits as cf (pairs, real ∈ {0,1})
 *
 * Note: chunk size n must be even; the graph sets chunk_size accordingly.
 */
#include <cmath>
#include "../../engine/block.hpp"

namespace blocks {

// --------------------------------------------------------------------------
class QpskMod : public engine::Block {
public:
    const char* name() const override { return "QpskMod"; }

    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || !in[0] || out.empty() || !out[0]) return;
        // n bits → n/2 symbols
        const size_t nsym = n / 2;
        out[0]->resize(nsym);
        const auto& b = in[0]->buf;
        auto& s = out[0]->buf;
        static constexpr float a = 0.707106781f; // 1/sqrt(2)
        for (size_t i = 0; i < nsym; ++i)
            s[i] = engine::cf(b[2*i].real()   > 0.5f ? -a : a,
                              b[2*i+1].real() > 0.5f ? -a : a);
    }
};

// --------------------------------------------------------------------------
class QpskDemod : public engine::Block {
public:
    const char* name() const override { return "QpskDemod"; }

    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || !in[0] || out.empty() || !out[0]) return;
        const size_t nsym = in[0]->size();
        out[0]->resize(nsym * 2);
        const auto& r = in[0]->buf;
        auto& b = out[0]->buf;
        (void)n;
        for (size_t i = 0; i < nsym; ++i) {
            b[2*i]   = engine::cf(r[i].real() < 0.f ? 1.f : 0.f, 0.f);
            b[2*i+1] = engine::cf(r[i].imag() < 0.f ? 1.f : 0.f, 0.f);
        }
    }
};

}  // namespace blocks
