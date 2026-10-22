#pragma once
/**
 * engine/rng.hpp  --  M1: counter-based PRNG keyed by (seed, block_id, sample_index).
 *
 * PLAN.md §3:  "Determinism: counter-based RNG (Philox or PCG) keyed by
 *              (seed, block id, sample index); results must be bit-identical
 *              for any thread count."
 *
 * Implementation: PCG-RXS-M-XS 64-bit, counter mode.
 * State = seed ^ (block_id * kBlockStride) + sample_index.
 * Each call to next() increments the internal counter so sequential
 * samples within a block are independent.  Re-seeding to any (block_id,
 * sample_index) pair gives bit-identical results regardless of thread order.
 *
 * For Gaussian noise we use the Box-Muller transform so the sequence of
 * normal samples depends only on (seed, block_id, sample_index), not on
 * call order across threads.
 */
#include <cstdint>
#include <cmath>

namespace engine {

/// 64-bit PCG (O'Neill, minimal variant: pcg64_oneseq_once_insecure)
/// We use it in counter mode: advance the state deterministically
/// from a base derived from (seed, block_id, sample_index).
class Rng {
public:
    /// Seed the generator for a specific (global_seed, block_id, sample_index).
    /// Calling this at the start of every work() invocation ensures
    /// bit-identical output regardless of thread interleaving.
    void seed(uint64_t global_seed, uint32_t block_id, uint64_t sample_index) noexcept {
        // Mix inputs with distinct large primes to avoid correlation.
        constexpr uint64_t kBlockMul  = 0x9e3779b97f4a7c15ULL; // Fibonacci hashing
        constexpr uint64_t kSampleMul = 0x6c62272e07bb0142ULL; // from xxHash
        state_ = global_seed
               ^ (static_cast<uint64_t>(block_id) * kBlockMul)
               ^ (sample_index * kSampleMul);
        inc_   = (static_cast<uint64_t>(block_id) << 1u) | 1u; // must be odd
        // Advance once to mix
        (void)next_u64();
    }

    /// Uniform uint64_t
    uint64_t next_u64() noexcept {
        uint64_t s = state_;
        state_ = s * kMul + inc_;
        // RXS-M-XS output function
        uint64_t word = ((s >> ((s >> 59u) + 5u)) ^ s) * 0xaef17502108ef2d9ULL;
        return (word >> 43u) ^ word;
    }

    /// Uniform float in [0, 1)
    float next_f32() noexcept {
        return static_cast<float>(next_u64() >> 40u) * (1.f / (1u << 24));
    }

    /// Standard normal via Box-Muller (consumes 2 u64)
    /// Returns a pair {z0, z1} of independent N(0,1) floats.
    std::pair<float,float> next_normal2() noexcept {
        // Use two independent draws; avoid log(0) with a small epsilon.
        constexpr float kEps = 1e-38f;
        float u1 = std::max(next_f32(), kEps);
        float u2 = next_f32();
        float r  = std::sqrt(-2.f * std::log(u1));
        float th = 6.28318530718f * u2;
        return { r * std::cos(th), r * std::sin(th) };
    }

private:
    static constexpr uint64_t kMul = 6364136223846793005ULL; // LCG multiplier
    uint64_t state_ = 0;
    uint64_t inc_   = 1;
};

}  // namespace engine
