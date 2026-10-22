# commsim: Digital Communication System Simulator (C++17)

Monte-Carlo simulator for the data-communications course: modulation, AWGN channel,
pulse shaping/ISI, error control coding, PCM/DM and CDMA. The simulation core is C++17,
header-only, multithreaded; Python is used only to plot the CSV output.

## Build and run
    make            # builds all apps with g++ (or use CMake: mkdir build && cd build && cmake .. && make)
    make run        # selftest, all simulations, then plots -> results/figs/
Apps: `ber_sweep`, `coding_gain`, `cdma_demo`, `pcm_demo`, `eye_diagram`, `selftest`.

## Layout
    include/commsim/  common.hpp channel.hpp modem.hpp coding.hpp pcm.hpp cdma.hpp
    apps/             one executable per experiment (writes CSV to results/)
    scripts/plot.py   CSV -> figures

All modems implement one `Modem` interface and are normalised to Es = 1, with N0 = 1/(k R Eb/N0),
so any new scheme drops straight into the BER sweep.

## What is simulated
| Syllabus topic | Module / app | Result |
|---|---|---|
| Binary & multilevel modulation (ASK, FSK, PSK, QAM) | `modem.hpp`, `ber_sweep` | BER matches theory (BPSK/QPSK, 16-QAM Gray, coherent BFSK, OOK) |
| Bit error rate performance | `ber_sweep` | `figs/ber_curves.png` |
| ISI, Nyquist (raised-cosine) response, eye diagram | `eye_diagram` | `figs/eye.png`: beta 0.35 vs 1.0, and ISI channel |
| Error detection | `coding.hpp` CRC-16 | detects all single-bit and 9-bit bursts (selftest) |
| Block coding | Hamming(7,4) | corrects every single-bit error (selftest) |
| Convolutional coding | K=7, rate 1/2 (171,133), Viterbi | hard and soft decision, `figs/coding_gain.png` |
| PCM, delta modulation | `pcm.hpp`, `pcm_demo` | SNR tracks 6.02N + 1.76 dB; DM slope overload vs granular noise |
| Multiple access (CDMA) | `cdma.hpp`, `cdma_demo` | Walsh codes: no multi-user penalty (synchronous); random PN codes: BER rises with users |

## Key results
- BPSK/QPSK simulated BER within sampling error of Q(sqrt(2Eb/N0)); 16-QAM needs about 4 dB more Eb/N0 at BER 1e-3.
- Coding gain at BER about 1e-5 vs uncoded BPSK (9.6 dB): soft-decision Viterbi reaches 1.7e-5 at 4 dB (about 5.5 dB gain); hard-decision about 3 dB.
- Hamming(7,4) only helps above about 5 dB (it loses to uncoded BPSK at low Eb/N0 because of its rate penalty).

## Notes
- Sample sizes are small so the full suite runs in a couple of minutes on one core; raise `MAXBITS` / `MINERR` in the apps for smoother curves below 1e-5.
- Ideas to extend: TDMA/FDMA frames, OFDM, soft-decision QAM demapper, ARQ over a lossy channel.
