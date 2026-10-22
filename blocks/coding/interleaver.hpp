#pragma once
/**
 * blocks/coding/interleaver.hpp  --  M5: Matrix block interleaver & deinterleaver.
 *
 * Theory:
 *   An R x C block interleaver writes bits row-by-row and reads them out
 *   column-by-column. A burst error of length B <= R in the transmitted stream
 *   is transformed upon deinterleaving into isolated errors separated by at
 *   least C symbols, enabling random-error-correcting codes to correct burst errors.
 */
#include <vector>
#include <cstdint>
#include <algorithm>
#include "../../engine/block.hpp"

namespace blocks {

struct BlockInterleaverCore {
    /// Interleave input vector (size must be multiple of rows * cols)
    template <typename T>
    static std::vector<T> interleave(const std::vector<T>& in, size_t rows, size_t cols) {
        const size_t block_size = rows * cols;
        const size_t num_blocks = in.size() / block_size;
        std::vector<T> out(num_blocks * block_size);

        for (size_t b = 0; b < num_blocks; ++b) {
            const size_t base = b * block_size;
            for (size_t r = 0; r < rows; ++r) {
                for (size_t c = 0; c < cols; ++c) {
                    // Write row-wise (r * cols + c), read col-wise (c * rows + r)
                    out[base + c * rows + r] = in[base + r * cols + c];
                }
            }
        }
        return out;
    }

    /// Deinterleave input vector (inverse of interleave)
    template <typename T>
    static std::vector<T> deinterleave(const std::vector<T>& in, size_t rows, size_t cols) {
        const size_t block_size = rows * cols;
        const size_t num_blocks = in.size() / block_size;
        std::vector<T> out(num_blocks * block_size);

        for (size_t b = 0; b < num_blocks; ++b) {
            const size_t base = b * block_size;
            for (size_t r = 0; r < rows; ++r) {
                for (size_t c = 0; c < cols; ++c) {
                    // Inverse mapping: out[r * cols + c] = in[c * rows + r]
                    out[base + r * cols + c] = in[base + c * rows + r];
                }
            }
        }
        return out;
    }
};

class BlockInterleaver : public engine::Block {
public:
    explicit BlockInterleaver(size_t rows = 16, size_t cols = 16)
        : rows_(rows), cols_(cols) {}

    const char* name() const override { return "BlockInterleaver"; }

    size_t rows() const { return rows_; }
    size_t cols() const { return cols_; }

    void setParam(const std::string& key, double v) override {
        if (key == "rows" && v >= 1.0) rows_ = static_cast<size_t>(v);
        else if (key == "cols" && v >= 1.0) cols_ = static_cast<size_t>(v);
        if (params) params->set(key, v);
    }

    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || !in[0] || out.empty() || !out[0]) return;
        const size_t in_n = in[0]->buf.size() > 0 ? in[0]->buf.size() : n;

        const size_t bsize = rows_ * cols_;
        const size_t nblocks = in_n / bsize;
        const size_t valid_n = nblocks * bsize;

        out[0]->resize(valid_n);
        const auto interleaved = BlockInterleaverCore::interleave(in[0]->buf, rows_, cols_);
        std::copy_n(interleaved.begin(), valid_n, out[0]->buf.begin());
    }

private:
    size_t rows_ = 16;
    size_t cols_ = 16;
};

class BlockDeinterleaver : public engine::Block {
public:
    explicit BlockDeinterleaver(size_t rows = 16, size_t cols = 16)
        : rows_(rows), cols_(cols) {}

    const char* name() const override { return "BlockDeinterleaver"; }

    size_t rows() const { return rows_; }
    size_t cols() const { return cols_; }

    void setParam(const std::string& key, double v) override {
        if (key == "rows" && v >= 1.0) rows_ = static_cast<size_t>(v);
        else if (key == "cols" && v >= 1.0) cols_ = static_cast<size_t>(v);
        if (params) params->set(key, v);
    }

    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || !in[0] || out.empty() || !out[0]) return;
        const size_t in_n = in[0]->buf.size() > 0 ? in[0]->buf.size() : n;

        const size_t bsize = rows_ * cols_;
        const size_t nblocks = in_n / bsize;
        const size_t valid_n = nblocks * bsize;

        out[0]->resize(valid_n);
        const auto deinterleaved = BlockInterleaverCore::deinterleave(in[0]->buf, rows_, cols_);
        std::copy_n(deinterleaved.begin(), valid_n, out[0]->buf.begin());
    }

private:
    size_t rows_ = 16;
    size_t cols_ = 16;
};

}  // namespace blocks
