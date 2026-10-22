#pragma once
/**
 * engine/stream.hpp  --  M1: one chunk of samples flowing between blocks.
 *
 * PLAN.md §3:  complex<float> streams, double for accumulators.
 * Using cf = std::complex<float> everywhere inside the engine.
 * Bit streams (sources / sinks) use uint8_t vectors passed separately.
 */
#include <complex>
#include <cstdint>
#include <vector>

namespace engine {

using cf = std::complex<float>;

/**
 * Stream<T> holds one "chunk" of T samples.
 * The graph pre-allocates streams; blocks just read/write buf directly.
 */
template <class T>
struct Stream {
    std::vector<T> buf;

    explicit Stream(size_t n = 0) : buf(n) {}

    void resize(size_t n) { buf.resize(n); }
    size_t size() const   { return buf.size(); }
};

}  // namespace engine
