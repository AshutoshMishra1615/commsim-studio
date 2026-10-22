#pragma once
/**
 * engine/probe.hpp  --  M1: BerProbe
 *
 * BerProbe is a sink block that receives two streams:
 *   port 0: tx bits encoded as complex<float> (+1 = bit 0, -1 = bit 1)
 *   port 1: rx symbols after the channel (demodulation is done inside the probe
 *           or the modem block feeds decoded bits directly)
 *
 * Simpler and more general design used here:
 *   The modem block has TWO output ports:
 *     port 0: modulated symbols (complex<float>)
 *     port 1: tx reference bits (float, 1.0 or 0.0 packed as cf.real())
 *   A decision block (inside the modem) or a separate DecisionBlock
 *   demodulates and feeds decoded bits.
 *
 * For M1 we keep it minimal:
 *   BerProbe takes two float streams (tx_bits, rx_bits) encoded as cf.real()
 *   and accumulates error count + total count.
 *
 * Thread safety: probe is only written from the sim thread; read from UI thread
 * via atomic snapshot.
 */
#include <atomic>
#include <cstdint>
#include "block.hpp"

namespace engine {

class BerProbe : public Block {
public:
    const char* name() const override { return "BerProbe"; }

    // in[0]: tx bits  (cf.real() = 0.0f or 1.0f)
    // in[1]: rx bits  (cf.real() = 0.0f or 1.0f)
    void work(const std::vector<Stream<cf>*>& in,
              std::vector<Stream<cf>*>& /*out*/,
              size_t n) override {
        if (in.size() < 2 || !in[0] || !in[1]) return;
        const auto& tx = in[0]->buf;
        const auto& rx = in[1]->buf;
        uint64_t err = 0;
        for (size_t i = 0; i < n && i < tx.size() && i < rx.size(); ++i) {
            // compare rounded integer bits
            int tb = (tx[i].real() > 0.5f) ? 1 : 0;
            int rb = (rx[i].real() > 0.5f) ? 1 : 0;
            err += (tb != rb) ? 1u : 0u;
        }
        errors_.fetch_add(err, std::memory_order_relaxed);
        total_.fetch_add(static_cast<uint64_t>(n), std::memory_order_relaxed);
    }

    /// Snapshot: returns {errors, total}. Reset optional.
    struct Snapshot { uint64_t errors; uint64_t total; };
    Snapshot snapshot(bool reset = false) {
        Snapshot s;
        s.errors = errors_.load(std::memory_order_relaxed);
        s.total  = total_.load(std::memory_order_relaxed);
        if (reset) {
            errors_.store(0, std::memory_order_relaxed);
            total_.store(0, std::memory_order_relaxed);
        }
        return s;
    }

    double ber() const {
        uint64_t t = total_.load(std::memory_order_relaxed);
        if (t == 0) return 0.0;
        return static_cast<double>(errors_.load(std::memory_order_relaxed))
             / static_cast<double>(t);
    }

private:
    std::atomic<uint64_t> errors_{0};
    std::atomic<uint64_t> total_{0};
};

}  // namespace engine
