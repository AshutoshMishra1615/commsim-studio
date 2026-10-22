#pragma once
/**
 * engine/block.hpp  --  M1: abstract Block base class.
 *
 * Matches PLAN.md §3 API exactly:
 *   virtual void work(const std::vector<Stream<cf>*>& in,
 *                     std::vector<Stream<cf>*>& out, size_t n) = 0;
 *
 * Additional housekeeping used by Graph / Scheduler:
 *   - block_id (assigned by Graph::add)
 *   - global_seed (propagated by Graph before run)
 *   - params pointer (optional, set by Graph)
 */
#include <string>
#include <vector>
#include "stream.hpp"
#include "params.hpp"

namespace engine {

struct Block {
    virtual ~Block() = default;

    /// Short display name, e.g. "BPSK", "AWGN".
    virtual const char* name() const = 0;

    /**
     * Process one chunk of n samples.
     * @param in   read-only input streams (may be empty for source blocks)
     * @param out  writable output streams (may be empty for sink/probe blocks)
     * @param n    chunk size in samples
     */
    virtual void work(const std::vector<Stream<cf>*>& in,
                      std::vector<Stream<cf>*>& out,
                      size_t n) = 0;

    /// Called by Graph/UI to change a parameter at runtime.
    virtual void setParam(const std::string& /*key*/, double /*v*/) {}

    // ---- set by Graph, read by Block implementations ----
    uint32_t    block_id    = 0;
    uint64_t    global_seed = 0;
    ParamStore* params      = nullptr;  // non-owning; Graph owns the store
};

}  // namespace engine
