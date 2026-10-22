#pragma once
/**
 * blocks/analog/pm.hpp  --  M6: Phase Modulation (PM)
 *
 * Implements:
 *   - PM modulator and phase demodulator.
 *   - Phase deviation Delta phi = kp * max|m(t)|.
 *
 * Theory:
 *   phi(t) = 2*pi*fc*t + kp * m(t)
 *   s(t) = Ac * exp(j * phi(t))
 *   Demodulation: instantaneous phase unwrapped minus carrier phase.
 */
#include <algorithm>
#include <cmath>
#include <vector>
#include "../../engine/block.hpp"

namespace blocks {

class PmModulator : public engine::Block {
public:
    explicit PmModulator(double fc = 1000.0, double fs = 10000.0, double kp = 1.5, double Ac = 1.0)
        : fc_(fc), fs_(fs), kp_(kp), Ac_(Ac) {}

    const char* name() const override { return "PmModulator"; }

    double fc() const { return fc_; }
    double fs() const { return fs_; }
    double kp() const { return kp_; }
    double Ac() const { return Ac_; }

    void setParam(const std::string& key, double v) override {
        if (key == "fc" && v >= 0.0) fc_ = v;
        else if (key == "fs" && v > 0.0) fs_ = v;
        else if (key == "kp" && v >= 0.0) kp_ = v;
        else if (key == "Ac" && v >= 0.0) Ac_ = v;
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

        const double two_pi = 6.28318530717958647692;
        const double dt = 1.0 / fs_;

        for (size_t i = 0; i < in_n; ++i) {
            const double msg = static_cast<double>(m[i].real());
            const double carrier_phase = two_pi * fc_ * (sample_count_ + i) * dt;
            const double phi = carrier_phase + kp_ * msg;

            const float re = static_cast<float>(Ac_ * std::cos(phi));
            const float im = static_cast<float>(Ac_ * std::sin(phi));
            s[i] = engine::cf(re, im);
        }
        sample_count_ += in_n;
    }

    void reset() { sample_count_ = 0; }

private:
    double fc_ = 1000.0;
    double fs_ = 10000.0;
    double kp_ = 1.5;
    double Ac_ = 1.0;
    uint64_t sample_count_ = 0;
};

class PmDemodulator : public engine::Block {
public:
    explicit PmDemodulator(double fc = 1000.0, double fs = 10000.0, double kp = 1.5)
        : fc_(fc), fs_(fs), kp_(kp) {}

    const char* name() const override { return "PmDemodulator"; }

    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || !in[0] || out.empty() || !out[0]) return;
        const size_t in_n = in[0]->buf.empty() ? n : in[0]->buf.size();
        out[0]->resize(in_n);

        const auto& s = in[0]->buf;
        auto& m = out[0]->buf;

        const double two_pi = 6.28318530717958647692;
        const double dt = 1.0 / fs_;

        for (size_t i = 0; i < in_n; ++i) {
            const double carrier_phase = two_pi * fc_ * (sample_count_ + i) * dt;
            // Demodulate by mixing down by carrier and taking angle
            const double c_cos = std::cos(-carrier_phase);
            const double c_sin = std::sin(-carrier_phase);
            const double bb_re = s[i].real() * c_cos - s[i].imag() * c_sin;
            const double bb_im = s[i].real() * c_sin + s[i].imag() * c_cos;

            const double phi = std::atan2(bb_im, bb_re);
            const double msg = (kp_ > 1e-6) ? (phi / kp_) : 0.0;
            m[i] = engine::cf(static_cast<float>(msg), 0.0f);
        }
        sample_count_ += in_n;
    }

    void reset() { sample_count_ = 0; }

private:
    double fc_ = 1000.0;
    double fs_ = 10000.0;
    double kp_ = 1.5;
    uint64_t sample_count_ = 0;
};

}  // namespace blocks
