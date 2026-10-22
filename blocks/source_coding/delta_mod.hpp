#pragma once
/**
 * blocks/source_coding/delta_mod.hpp  --  M6: Delta Modulation (DM)
 *
 * Implements:
 *   - Delta modulation encoder and decoder blocks.
 *   - Configurable step size Delta.
 *
 * Theory:
 *   Linear Delta Modulation (LDM) encodes 1 bit per sample:
 *     b[n] = (x[n] >= est[n-1]) ? 1 : 0
 *     est[n] = est[n-1] + (b[n] ? +Delta : -Delta)
 *
 *   Slope overload condition:
 *     Max signal slope |dx/dt|_max = 2*pi*f_m*A.
 *     Tracking slope limit = Delta * f_s = Delta / T_s.
 *     Slope overload occurs if: 2*pi*f_m*A > Delta * f_s.
 *     [Haykin, Communication Systems, 4th ed., §3.8]
 */
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include "../../engine/block.hpp"

namespace blocks {

class DeltaModCore {
public:
    explicit DeltaModCore(double step = 0.05) : step_(step) {}

    void reset() { est_ = 0.0; }
    double step() const { return step_; }
    void setStep(double s) { step_ = std::max(1e-6, s); }
    double state() const { return est_; }

    uint8_t encodeSample(double x) {
        const uint8_t b = (x >= est_) ? 1u : 0u;
        est_ += (b ? step_ : -step_);
        return b;
    }

    double decodeSample(uint8_t b) {
        est_ += (b ? step_ : -step_);
        return est_;
    }

private:
    double step_ = 0.05;
    double est_ = 0.0;
};

// ---------------------------------------------------------------------------
// DeltaModEncoder: 1 analog sample -> 1 bit (cf.real() in {0.0f, 1.0f})
// ---------------------------------------------------------------------------
class DeltaModEncoder : public engine::Block {
public:
    explicit DeltaModEncoder(double step = 0.05) : core_(step) {}

    const char* name() const override { return "DeltaModEncoder"; }

    double step() const { return core_.step(); }
    void setStep(double s) { core_.setStep(s); }

    void setParam(const std::string& key, double v) override {
        if (key == "step" && v > 0.0) core_.setStep(v);
        if (params) params->set(key, v);
    }

    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || !in[0] || out.empty() || !out[0]) return;
        const size_t in_n = in[0]->buf.empty() ? n : in[0]->buf.size();
        out[0]->resize(in_n);

        const auto& x = in[0]->buf;
        auto& y = out[0]->buf;
        for (size_t i = 0; i < in_n; ++i) {
            const uint8_t b = core_.encodeSample(static_cast<double>(x[i].real()));
            y[i] = engine::cf(static_cast<float>(b), 0.0f);
        }
    }

private:
    DeltaModCore core_;
};

// ---------------------------------------------------------------------------
// DeltaModDecoder: 1 bit -> 1 reconstructed sample
// ---------------------------------------------------------------------------
class DeltaModDecoder : public engine::Block {
public:
    explicit DeltaModDecoder(double step = 0.05) : core_(step) {}

    const char* name() const override { return "DeltaModDecoder"; }

    double step() const { return core_.step(); }
    void setStep(double s) { core_.setStep(s); }

    void setParam(const std::string& key, double v) override {
        if (key == "step" && v > 0.0) core_.setStep(v);
        if (params) params->set(key, v);
    }

    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || !in[0] || out.empty() || !out[0]) return;
        const size_t in_n = in[0]->buf.empty() ? n : in[0]->buf.size();
        out[0]->resize(in_n);

        const auto& x = in[0]->buf;
        auto& y = out[0]->buf;
        for (size_t i = 0; i < in_n; ++i) {
            const uint8_t b = (x[i].real() > 0.5f) ? 1u : 0u;
            const double s = core_.decodeSample(b);
            y[i] = engine::cf(static_cast<float>(s), 0.0f);
        }
    }

private:
    DeltaModCore core_;
};

}  // namespace blocks
