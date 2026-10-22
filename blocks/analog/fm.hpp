#pragma once
/**
 * blocks/analog/fm.hpp  --  M6: Frequency Modulation (FM)
 *
 * Implements:
 *   - FM modulator and discriminator demodulator.
 *   - Peak deviation Delta f = kf * max|m(t)|.
 *   - Carson's bandwidth calculation helper: B_Carson = 2 * (Delta f + fm).
 *
 * Theory:
 *   phi(t) = 2*pi*fc*t + 2*pi*kf * integral_0^t m(tau) dtau
 *   s(t) = Ac * exp(j * phi(t))
 *   Demodulation via phase differencing:
 *     Delta phi[n] = arg(s[n] * conj(s[n-1])) = 2*pi*(fc + kf*m[n]) / fs
 *     m[n] = (Delta phi[n] * fs / (2*pi) - fc) / kf
 *   [Carlson, Communication Systems, 5th ed., §5.2]
 */
#include <algorithm>
#include <cmath>
#include <vector>
#include "../../engine/block.hpp"

namespace blocks {

class FmModulator : public engine::Block {
public:
    explicit FmModulator(double fc = 2000.0, double fs = 20000.0, double kf = 1000.0, double Ac = 1.0)
        : fc_(fc), fs_(fs), kf_(kf), Ac_(Ac) {}

    const char* name() const override { return "FmModulator"; }

    double fc() const { return fc_; }
    double fs() const { return fs_; }
    double kf() const { return kf_; }
    double Ac() const { return Ac_; }

    double carsonBandwidth(double fm, double max_m = 1.0) const {
        const double delta_f = kf_ * max_m;
        return 2.0 * (delta_f + fm);
    }

    void setParam(const std::string& key, double v) override {
        if (key == "fc" && v >= 0.0) fc_ = v;
        else if (key == "fs" && v > 0.0) fs_ = v;
        else if (key == "kf" && v >= 0.0) kf_ = v;
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
            const double inst_freq = fc_ + kf_ * msg;
            phase_ += two_pi * inst_freq * dt;
            // Keep phase wrapped to [-pi, pi] to avoid numerical loss of precision
            while (phase_ > 3.14159265358979323846) phase_ -= two_pi;
            while (phase_ < -3.14159265358979323846) phase_ += two_pi;

            const float re = static_cast<float>(Ac_ * std::cos(phase_));
            const float im = static_cast<float>(Ac_ * std::sin(phase_));
            s[i] = engine::cf(re, im);
        }
    }

    void reset() { phase_ = 0.0; }

private:
    double fc_ = 2000.0;
    double fs_ = 20000.0;
    double kf_ = 1000.0;
    double Ac_ = 1.0;
    double phase_ = 0.0;
};

class FmDemodulator : public engine::Block {
public:
    explicit FmDemodulator(double fc = 2000.0, double fs = 20000.0, double kf = 1000.0)
        : fc_(fc), fs_(fs), kf_(kf) {}

    const char* name() const override { return "FmDemodulator"; }

    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || !in[0] || out.empty() || !out[0]) return;
        const size_t in_n = in[0]->buf.empty() ? n : in[0]->buf.size();
        out[0]->resize(in_n);

        const auto& s = in[0]->buf;
        auto& m = out[0]->buf;

        const double two_pi = 6.28318530717958647692;

        for (size_t i = 0; i < in_n; ++i) {
            const engine::cf curr = s[i];
            // Delta phi = arg(curr * conj(prev_))
            const double prod_re = curr.real() * prev_.real() + curr.imag() * prev_.imag();
            const double prod_im = curr.imag() * prev_.real() - curr.real() * prev_.imag();
            const double dphi = std::atan2(prod_im, prod_re);

            // inst_freq = dphi * fs / (2*pi)
            const double inst_freq = dphi * fs_ / two_pi;
            double msg = (inst_freq - fc_) / kf_;

            m[i] = engine::cf(static_cast<float>(msg), 0.0f);
            prev_ = curr;
        }
    }

    void reset() { prev_ = engine::cf(1.0f, 0.0f); }

private:
    double fc_ = 2000.0;
    double fs_ = 20000.0;
    double kf_ = 1000.0;
    engine::cf prev_ = engine::cf(1.0f, 0.0f);
};

}  // namespace blocks
