#pragma once
/**
 * blocks/mapping/qam16.hpp  --  M1: 16-QAM modulator + demodulator blocks.
 *
 * Gray coding per axis: 00→-3, 01→-1, 11→+1, 10→+3 (index=b1*2+b0).
 * Normalised to Es=1: levels divided by sqrt(10).
 * BER ≈ (3/8) erfc(sqrt(0.4 Eb/N0))  [Proakis & Salehi, Gray-coded 16-QAM]
 *
 * Qam16Mod:   in[0] bits (4 per symbol) → out[0] symbols (cf, Es=1)
 * Qam16Demod: in[0] received symbols    → out[0] decoded bits (cf, 4 per symbol)
 */
#include <cmath>
#include "../../engine/block.hpp"

namespace blocks {

static constexpr float kQam16Norm = 0.316227766f; // 1/sqrt(10)
static constexpr float kQam16Thr  = 0.632455532f; // 2/sqrt(10)

inline float qam16_lvl(int b1, int b0) noexcept {
    static const float L[4] = {-3.f, -1.f, 3.f, 1.f}; // index=b1*2+b0
    return L[b1*2+b0] * kQam16Norm;
}

inline void qam16_slice(float x, uint8_t& b1, uint8_t& b0) noexcept {
    if      (x < -kQam16Thr) { b1=0; b0=0; }
    else if (x <  0.f)       { b1=0; b0=1; }
    else if (x <  kQam16Thr) { b1=1; b0=1; }
    else                     { b1=1; b0=0; }
}

// --------------------------------------------------------------------------
class Qam16Mod : public engine::Block {
public:
    const char* name() const override { return "Qam16Mod"; }

    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || !in[0] || out.empty() || !out[0]) return;
        const size_t nsym = n / 4;
        out[0]->resize(nsym);
        const auto& b = in[0]->buf;
        auto& s = out[0]->buf;
        for (size_t i = 0; i < nsym; ++i) {
            auto bit = [&](size_t k) -> int { return b[4*i+k].real() > 0.5f ? 1 : 0; };
            s[i] = engine::cf(qam16_lvl(bit(0), bit(1)),
                              qam16_lvl(bit(2), bit(3)));
        }
    }
};

// --------------------------------------------------------------------------
class Qam16Demod : public engine::Block {
public:
    const char* name() const override { return "Qam16Demod"; }

    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || !in[0] || out.empty() || !out[0]) return;
        const size_t nsym = in[0]->size();
        out[0]->resize(nsym * 4);
        const auto& r = in[0]->buf;
        auto& b = out[0]->buf;
        (void)n;
        for (size_t i = 0; i < nsym; ++i) {
            uint8_t b0r, b1r, b0i, b1i;
            qam16_slice(r[i].real(), b1r, b0r);
            qam16_slice(r[i].imag(), b1i, b0i);
            b[4*i]   = engine::cf(static_cast<float>(b1r), 0.f);
            b[4*i+1] = engine::cf(static_cast<float>(b0r), 0.f);
            b[4*i+2] = engine::cf(static_cast<float>(b1i), 0.f);
            b[4*i+3] = engine::cf(static_cast<float>(b0i), 0.f);
        }
    }
};

}  // namespace blocks
