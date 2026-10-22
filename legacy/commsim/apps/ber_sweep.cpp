// BER vs Eb/N0 for all modems over AWGN, simulated vs theory. Output: results/ber_sweep.csv
#include <cstdio>
#include "commsim/channel.hpp"
#include "commsim/modem.hpp"
using namespace commsim;

int main() {
    std::vector<std::unique_ptr<Modem>> modems;
    modems.emplace_back(new BPSK);
    modems.emplace_back(new QPSK);
    modems.emplace_back(new QAM16);
    modems.emplace_back(new BFSK);
    modems.emplace_back(new OOK);

    const double step = 1.0, lo = 0, hi = 12;
    const int npts = int((hi - lo) / step) + 1;
    const size_t BLOCK = 20000, MAXBITS = 2000000, MINERR = 100;

    struct Row { size_t bits = 0, errs = 0; };
    std::vector<Row> rows(modems.size() * npts);

    parallelFor((int)rows.size(), [&](int idx) {
        const Modem& m = *modems[idx / npts];
        double db = lo + step * (idx % npts);
        double N0 = noiseN0(db2lin(db), m.k());
        Rng rng(12345 + 7919ull * idx);
        Row& r = rows[idx];
        while (r.bits < MAXBITS && r.errs < MINERR) {
            Bits tx = randomBits(BLOCK, rng);
            auto s = m.modulate(tx);
            awgn(s, N0, rng);
            r.errs += countErrors(tx, m.demodulate(s));
            r.bits += BLOCK;
        }
    });

    FILE* f = fopen("results/ber_sweep.csv", "w");
    fprintf(f, "modem,ebn0_db,bits,errors,ber,theory\n");
    for (size_t i = 0; i < rows.size(); ++i) {
        const Modem& m = *modems[i / npts];
        double db = lo + step * (i % npts);
        fprintf(f, "%s,%.1f,%zu,%zu,%.3e,%.3e\n", m.name(), db, rows[i].bits, rows[i].errs,
                (double)rows[i].errs / rows[i].bits, m.theoryBer(db2lin(db)));
    }
    fclose(f);
    puts("wrote results/ber_sweep.csv");
}
