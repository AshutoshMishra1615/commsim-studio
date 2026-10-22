#pragma once
/**
 * blocks/protocol/arq.hpp  --  M7: Automatic Repeat reQuest (ARQ) Protocols
 *
 * Implements:
 *   - Stop-and-Wait ARQ simulator and theoretical throughput efficiency.
 *   - Go-Back-N ARQ simulator and theoretical throughput efficiency.
 *
 * Theory:
 *   Stop-and-Wait efficiency:
 *     eta_SW = (1 - Pf) / (1 + 2*a)
 *     where Pf = frame error probability, a = t_prop / t_frame.
 *     [Stallings, Data and Computer Communications, 10th ed., §7.4]
 *
 *   Go-Back-N efficiency (W >= 1 + 2*a):
 *     eta_GBN = (1 - Pf) / (1 + (W - 1)*Pf)
 */
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include "../../engine/rng.hpp"

namespace blocks {

class StopAndWaitArq {
public:
    static double theoryEfficiency(double Pf, double a) {
        if (1.0 + 2.0 * a <= 0.0) return 0.0;
        return (1.0 - Pf) / (1.0 + 2.0 * a);
    }

    /// Simulate Stop-and-Wait ARQ for a given number of delivered packets
    /// Returns measured throughput efficiency (useful frame transmissions / total elapsed time slots)
    static double simulate(double Pf, double a, size_t target_delivered = 10000, uint64_t seed = 42) {
        engine::Rng rng;
        rng.seed(seed, 0, 0);
        size_t delivered = 0;
        double total_time_slots = 0.0;
        const double cycle_time = 1.0 + 2.0 * a;

        while (delivered < target_delivered) {
            // Attempt to transmit a frame
            total_time_slots += cycle_time;
            const double r = static_cast<double>(rng.next_f32());
            if (r >= Pf) {
                // Successful transmission & ACK
                delivered++;
            }
            // Else: frame lost or NACKed, timeout occurs and retransmits
        }

        // Efficiency = useful transmission time / total elapsed time
        // Each delivered frame has 1 unit of useful transmission time
        return static_cast<double>(delivered) / total_time_slots;
    }
};

class GoBackNArq {
public:
    /// Theoretical throughput efficiency for Go-Back-N ARQ (Stallings eq. 7.6)
    static double theoryEfficiency(double Pf, double a, size_t W) {
        const double k = 2.0 * a;
        const double w_dbl = static_cast<double>(W);
        if (w_dbl >= 1.0 + k) {
            return (1.0 - Pf) / (1.0 + k * Pf);
        } else {
            return (w_dbl * (1.0 - Pf)) / ((1.0 + k) * (1.0 - Pf + w_dbl * Pf));
        }
    }

    /// Simulate Go-Back-N ARQ for a sliding window of size W
    static double simulate(double Pf, double a, size_t W, size_t target_delivered = 10000, uint64_t seed = 42) {
        engine::Rng rng;
        rng.seed(seed, 0, 0);
        size_t delivered = 0;
        size_t total_transmissions = 0;
        const double k = 2.0 * a;
        const size_t retrans = (static_cast<double>(W) >= 1.0 + k)
            ? static_cast<size_t>(std::round(k))
            : (W > 0 ? W - 1 : 0);

        while (delivered < target_delivered) {
            // Transmit frame
            total_transmissions++;
            const double r = static_cast<double>(rng.next_f32());
            if (r >= Pf) {
                // Frame delivered
                delivered++;
            } else {
                // Frame lost: sender must retransmit the lost frame plus subsequent frames in flight
                total_transmissions += retrans;
            }
        }
        return static_cast<double>(delivered) / static_cast<double>(total_transmissions);
    }
};

}  // namespace blocks
