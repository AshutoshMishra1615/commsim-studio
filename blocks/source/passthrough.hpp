#pragma once
/**
 * blocks/source/passthrough.hpp  --  Pass-through / splitter block.
 *
 * Forwards in[0] to out[0] and optionally out[1] .. out[m-1], allowing
 * signal streams to be tapped or split across branches.
 */
#include <vector>
#include "../../engine/block.hpp"

namespace blocks {

class PassThrough : public engine::Block {
public:
    const char* name() const override { return "PassThrough"; }

    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || !in[0]) return;
        for (auto* o : out) {
            if (!o) continue;
            o->resize(n);
            o->buf = in[0]->buf;
        }
    }
};

}  // namespace blocks
