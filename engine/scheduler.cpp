/**
 * engine/scheduler.cpp  --  M1: Graph implementation (topological sort + run).
 *
 * Topological sort: Kahn's algorithm on the block dependency DAG.
 * run() iterates chunks in sorted order; no multi-threading here
 * (M1 determinism test runs single-threaded; OpenMP sweeps are M1 CLI stretch).
 *
 * Determinism guarantee: each block's Rng is seeded with
 *   (global_seed, block_id, chunk_index * chunk_size)
 * before work() is called, making output bit-identical regardless of
 * scheduling order or thread count.
 */
#include "graph.hpp"
#include <algorithm>
#include <cassert>
#include <queue>
#include <stdexcept>

namespace engine {

int Graph::add(std::unique_ptr<Block> b) {
    const int id = static_cast<int>(blocks_.size());
    b->block_id    = static_cast<uint32_t>(id);
    b->global_seed = seed_;
    b->params      = &params_;
    blocks_.push_back(std::move(b));
    // grow adjacency lists
    in_edges_.emplace_back();   // port lists populated in connect()
    out_edges_.emplace_back();
    order_valid_ = false;
    return id;
}

void Graph::connect(int src, int srcPort, int dst, int dstPort) {
    assert(src >= 0 && src < static_cast<int>(blocks_.size()));
    assert(dst >= 0 && dst < static_cast<int>(blocks_.size()));

    const int eidx = static_cast<int>(edges_.size());
    Edge e;
    e.src_block = src;
    e.src_port  = srcPort;
    e.dst_block = dst;
    e.dst_port  = dstPort;
    e.stream.resize(chunk_size_);
    edges_.push_back(std::move(e));

    // Expand port vectors as needed
    if (static_cast<int>(out_edges_[src].size()) <= srcPort)
        out_edges_[src].resize(srcPort + 1, -1);
    out_edges_[src][srcPort] = eidx;

    if (static_cast<int>(in_edges_[dst].size()) <= dstPort)
        in_edges_[dst].resize(dstPort + 1, -1);
    in_edges_[dst][dstPort] = eidx;

    order_valid_ = false;
}

// ---------------------------------------------------------------------------
// Kahn's topological sort
// ---------------------------------------------------------------------------
void Graph::buildOrder() {
    const int N = static_cast<int>(blocks_.size());

    // Count in-degree per block (number of connected input ports)
    std::vector<int> indeg(N, 0);
    for (auto& e : edges_) ++indeg[e.dst_block];

    std::queue<int> q;
    for (int i = 0; i < N; ++i)
        if (indeg[i] == 0) q.push(i);

    order_.clear();
    order_.reserve(N);
    while (!q.empty()) {
        int u = q.front(); q.pop();
        order_.push_back(u);
        for (auto& e : edges_) {
            if (e.src_block == u) {
                if (--indeg[e.dst_block] == 0)
                    q.push(e.dst_block);
            }
        }
    }
    if (static_cast<int>(order_.size()) != N)
        throw std::runtime_error("engine::Graph: cycle detected");

    order_valid_ = true;
}

// ---------------------------------------------------------------------------
// run(): execute `chunks` iterations in topological order
// ---------------------------------------------------------------------------
void Graph::run(size_t chunks) {
    if (!order_valid_) buildOrder();

    // Propagate seed to blocks added after last add()
    for (auto& b : blocks_) b->global_seed = seed_;

    const uint64_t chunk_offset = static_cast<uint64_t>(params_.get("__chunk_offset", 0.0));

    for (size_t chunk = 0; chunk < chunks; ++chunk) {
        const uint64_t sample_base = (chunk_offset + chunk) * chunk_size_;

        for (int bid : order_) {
            Block* blk = blocks_[bid].get();

            // Collect input stream pointers (null if port unconnected)
            const int n_in = static_cast<int>(in_edges_[bid].size());
            std::vector<Stream<cf>*> ins(n_in, nullptr);
            for (int p = 0; p < n_in; ++p) {
                int eidx = in_edges_[bid][p];
                if (eidx >= 0) ins[p] = &edges_[eidx].stream;
            }

            // Collect output stream pointers
            const int n_out = static_cast<int>(out_edges_[bid].size());
            std::vector<Stream<cf>*> outs(n_out, nullptr);
            for (int p = 0; p < n_out; ++p) {
                int eidx = out_edges_[bid][p];
                if (eidx >= 0) outs[p] = &edges_[eidx].stream;
            }

            // Note: sample_base lets blocks seed their RNG deterministically.
            // We store it temporarily in a thread-local or pass via setParam;
            // the simplest approach for M1 (single-threaded run) is to set
            // a well-known param key before work().
            // For the counter-based RNG the block uses (seed, block_id, sample_base).
            // We pass sample_base through a reserved param key "__sample_base".
            // (Blocks that need it read it from params.)
            if (blk->params)
                blk->params->set("__sample_base", static_cast<double>(sample_base));

            blk->work(ins, outs, chunk_size_);
        }
    }
}

}  // namespace engine
