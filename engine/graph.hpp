#pragma once
/**
 * engine/graph.hpp  --  M1: signal-graph container and scheduler interface.
 *
 * PLAN.md §3 API:
 *   int  Graph::add(std::unique_ptr<Block>)
 *   void Graph::connect(int src, int srcPort, int dst, int dstPort)
 *   void Graph::run(size_t chunks)   // executes in topological order
 *
 * Each edge carries a Stream<cf>. The scheduler runs blocks in topological
 * order so every block's inputs are ready before work() is called.
 * Source blocks (0 inputs) are always first.
 */
#include <memory>
#include <string>
#include <vector>
#include "block.hpp"
#include "stream.hpp"
#include "params.hpp"

namespace engine {

struct Edge {
    int src_block;
    int src_port;
    int dst_block;
    int dst_port;
    Stream<cf> stream;   // owned here
};

class Graph {
public:
    explicit Graph(size_t chunk_size = 4096, uint64_t seed = 42)
        : chunk_size_(chunk_size), seed_(seed) {}

    /// Add a block; returns its integer id (0-based).
    int add(std::unique_ptr<Block> b);

    /// Connect output port srcPort of block src to input port dstPort of block dst.
    void connect(int src, int srcPort, int dst, int dstPort);

    /// Run the full graph for `chunks` iterations (topological order each time).
    void run(size_t chunks);

    /// Access block by id (non-owning).
    Block* block(int id) { return blocks_[id].get(); }

    /// Global seed used for all RNG initialisation.
    void setSeed(uint64_t s) { seed_ = s; }

    /// Access the shared ParamStore.
    ParamStore& params() { return params_; }

private:
    void buildOrder();   // topological sort, fills order_

    size_t   chunk_size_;
    uint64_t seed_;

    std::vector<std::unique_ptr<Block>> blocks_;
    std::vector<Edge>                   edges_;

    /// Execution order (block indices, topologically sorted).
    std::vector<int> order_;
    bool             order_valid_ = false;

    /// Per-block: list of incoming edges (edge index) per input port.
    /// in_edges_[block_id][port] = edge_index  (-1 if unconnected)
    std::vector<std::vector<int>> in_edges_;
    /// Per-block: list of outgoing edges per output port.
    std::vector<std::vector<int>> out_edges_;

    ParamStore params_;
};

}  // namespace engine
