# AGENTS.md

Project: commsim-studio (C++17 simulation and visualization engine for a data communications course).
Source of truth: `PLAN.md`. Read it fully before any task.

## Rules
- Work only on the milestone named in the task. Never start the next milestone.
- C++17, no raw owning pointers, no global mutable state, `-Wall -Wextra` clean.
- Every new block gets a Catch2 test in `tests/` that checks against theory (see PLAN.md section 7).
- Never mark a milestone done until: the build passes, all tests pass, and the acceptance
  numbers from PLAN.md section 6 are printed in the test log.
- Do not add dependencies beyond PLAN.md section 4. All dependencies via CMake FetchContent, versions pinned.
- Do not write a custom UI toolkit. The only custom rendering code is the GPU density renderer.
- The simulation must be deterministic: counter-based RNG keyed by (seed, block id, sample index);
  results must be bit-identical for any thread count.
- Keep `legacy/` read-only. Port code from it into `blocks/`, do not edit it in place.
- Commit nothing that does not build. Use small commits with clear messages.
- Do not claim a number you did not measure. Paste actual test output.

## End-of-milestone report (required)
1. Files changed
2. Test results (full pass/fail list)
3. Measured acceptance numbers vs targets from PLAN.md section 6
4. Open issues and deviations
Then stop and wait for approval.

## Build and test
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    ctest --test-dir build --output-on-failure
