#pragma once
/**
 * blocks/analog/am.hpp  --  M6: Amplitude Modulation (AM)
 *
 * Implements:
 *   - DSB-FC (standard AM with carrier) and DSB-SC (suppressed carrier).
 *   - Envelope detector / coherent demodulator.
 *
 * Theory:
 *   DSB-FC: s(t) = Ac * [1 + mu * m(t)] * exp(j * 2*pi*fc*t)
 *   mu: modulation index (0 <= mu <= 1 for distortion-free envelope detection)
 *   DSB-SC: s(t) = Ac * m(t) * exp(j * 2*pi*fc*t)
 */
#include <algorithm>
#include <cmath>
#include <vector>
#include "../../engine/block.hpp"

namespace blocks {

class AmModulator : public engine::Block {
public:
    explicit AmModulator(double fc = 1000.0, double fs = 10000.0, double mu = 0.8, double Ac = 1.0, bool dsb_sc = false)
        : fc_(fc), fs_(fs), mu_(mu), Ac_(Ac), dsb_sc_(dsb_sc) {}

    const char* name() const override { return "AmModulator"; }

    double fc() const { return fc_; }
    double fs() const { return fs_; }
    double mu() const { return mu_; }
    double Ac() const { return Ac_; }
    bool dsbSc() const { return dsb_sc_; }

    void setParam(const std::string& key, double v) override {
        if (key == "fc" && v >= 0.0) fc_ = v;
        else if (key == "fs" && v > 0.0) fs_ = v;
        else if (key == "mu" && v >= 0.0) mu_ = v;
        else if (key == "Ac" && v >= 0.0) Ac_ = v;
        else if (key == "dsb_sc") dsb_sc_ = (v > 0.5);
        if (params) params->set(key, v);
    }

    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || !in[0] || out.empty() || !out[0]) return;
        const size_t in_n = in[0]->buf.empty() ? n : in[0]->buf.size();
        out[0]->resize(in_n);

        const auto& m = in[0]->buf;
        auto& s = out[0]->buf;

        const double dt = 1.0 / fs_;
        const double two_pi = 6.28318530717958647692;

        for (size_t i = 0; i < in_n; ++i) {
            const double msg = static_cast<double>(m[i].real());
            const double carrier_phase = two_pi * fc_ * (sample_count_ + i) * dt;

            double env = 0.0;
            if (dsb_sc_) {
                env = Ac_ * msg;
            } else {
                env = Ac_ * (1.0 + mu_ * msg);
            }

            const float re = static_cast<float>(env * std::cos(carrier_phase));
            const float im = static_cast<float>(env * std::sin(carrier_phase));
            s[i] = engine::cf(re, im);
        }
        sample_count_ += in_n;
    }

    void reset() { sample_count_ = 0; }

private:
    double fc_ = 1000.0;
    double fs_ = 10000.0;
    double mu_ = 0.8;
    double Ac_ = 1.0;
    bool dsb_sc_ = false;
    uint64_t sample_count_ = 0;
};

class AmDemodulator : public engine::Block {
public:
    explicit AmDemodulator(double Ac = 1.0, double mu = 0.8, bool dsb_sc = false)
        : Ac_(Ac), mu_(mu), dsb_sc_(dsb_sc) {}

    const char* name() const override { return "AmDemodulator"; }

    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || !in[0] || out.empty() || !out[0]) return;
        const size_t in_n = in[0]->buf.empty() ? n : in[0]->buf.size();
        out[0]->resize(in_n);

        const auto& s = in[0]->buf;
        auto& m = out[0]->buf;

        for (size_t i = 0; i < in_n; ++i) {
            const double mag = std::hypot(s[i].real(), s[i].imag());
            double recovered = 0.0;
            if (!dsb_sc_ && mu_ > 1e-6 && Ac_ > 1e-6) {
                // Envelope detector: (envelope - Ac) / (Ac * mu)
                recovered = (mag - Ac_) / (Ac_ * mu_);
            } else {
                recovered = mag;
            }
            m[i] = engine::cf(static_cast<float>(recovered), 0.0f);
        }
    }

private:
    double Ac_ = 1.0;
    double mu_ = 0.8;
    bool dsb_sc_ = false;
};

}  // namespace blocks
