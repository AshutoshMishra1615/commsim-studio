#pragma once
/**
 * blocks/channel/isi_fir.hpp  --  M4: Dispersive multipath / ISI channel block.
 *
 * Simulates inter-symbol interference (ISI) via a tapped-delay-line FIR channel.
 * Default taps [1.0, 0.5, 0.3] represent a static multipath channel causing severe
 * ISI that visibly closes the eye diagram and introduces bit errors.
 *
 * Can operate at symbol rate (spacing = 1) or sample rate (spacing = sps).
 *
 * Parameters:
 *   "isi_enabled" -> 1.0 = active, 0.0 = bypass (pass-through)
 *   "isi_tap0"    -> tap 0 weight (default 1.0)
 *   "isi_tap1"    -> tap 1 weight (default 0.5)
 *   "isi_tap2"    -> tap 2 weight (default 0.3)
 */
#include <vector>
#include <algorithm>
#include "../../engine/block.hpp"

namespace blocks {

class IsiFir : public engine::Block {
public:
    explicit IsiFir(const std::vector<float>& taps = {1.0f, 0.5f, 0.3f}, size_t spacing = 1)
        : taps_(taps), spacing_(spacing) {
        reset();
    }

    const char* name() const override { return "IsiFir"; }

    void setParam(const std::string& key, double v) override {
        if (key == "isi_enabled") {
            enabled_ = (v > 0.5);
        } else if (key == "isi_tap0") {
            if (taps_.size() > 0) taps_[0] = static_cast<float>(v);
        } else if (key == "isi_tap1") {
            if (taps_.size() > 1) taps_[1] = static_cast<float>(v);
        } else if (key == "isi_tap2") {
            if (taps_.size() > 2) taps_[2] = static_cast<float>(v);
        } else if (key == "spacing" && v >= 1.0) {
            spacing_ = static_cast<size_t>(v);
            reset();
        }
        if (params) params->set(key, v);
    }

    void setTaps(const std::vector<float>& taps) {
        taps_ = taps;
        reset();
    }

    void setSpacing(size_t spacing) {
        spacing_ = std::max<size_t>(1, spacing);
        reset();
    }

    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }
    const std::vector<float>& taps() const { return taps_; }
    size_t spacing() const { return spacing_; }

    void reset() {
        const size_t max_delay = taps_.empty() ? 0 : (taps_.size() - 1) * spacing_;
        history_.assign(max_delay, {0.0f, 0.0f});
    }

    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || !in[0] || out.empty() || !out[0]) return;

        const size_t in_n = in[0]->buf.size();
        const size_t process_n = in_n > 0 ? in_n : n;
        out[0]->resize(process_n);

        const auto& x = in[0]->buf;
        auto& y = out[0]->buf;

        if (!enabled_ || taps_.empty()) {
            std::copy_n(x.begin(), process_n, y.begin());
            return;
        }

        const size_t num_taps = taps_.size();
        const size_t max_delay = (num_taps - 1) * spacing_;

        if (history_.size() != max_delay) {
            history_.assign(max_delay, {0.0f, 0.0f});
        }

        for (size_t i = 0; i < process_n; ++i) {
            engine::cf acc{0.0f, 0.0f};
            for (size_t j = 0; j < num_taps; ++j) {
                const size_t delay = j * spacing_;
                engine::cf val;
                if (i >= delay) {
                    val = x[i - delay];
                } else {
                    val = history_[max_delay - (delay - i)];
                }
                acc += val * taps_[j];
            }
            y[i] = acc;
        }

        // Update history
        if (max_delay > 0) {
            if (process_n >= max_delay) {
                for (size_t k = 0; k < max_delay; ++k) {
                    history_[k] = x[process_n - max_delay + k];
                }
            } else {
                const size_t shift = process_n;
                for (size_t k = 0; k < max_delay - shift; ++k) {
                    history_[k] = history_[k + shift];
                }
                for (size_t k = 0; k < shift; ++k) {
                    history_[max_delay - shift + k] = x[k];
                }
            }
        }
    }

private:
    std::vector<float> taps_{1.0f, 0.5f, 0.3f};
    size_t spacing_ = 1;
    bool enabled_ = true;
    std::vector<engine::cf> history_;
};

}  // namespace blocks
