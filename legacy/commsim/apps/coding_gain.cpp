// Uncoded BPSK vs Hamming(7,4) vs conv K=7 (hard / soft Viterbi). Output: results/coding_gain.csv
#include <cstdio>
#include "commsim/channel.hpp"
#include "commsim/coding.hpp"
using namespace commsim;

enum Scheme { UNCODED, HAMMING, CONV_HARD, CONV_SOFT, NS };
static const char* NAMES[NS] = {"Uncoded BPSK", "Hamming(7;4) hard", "Conv K=7 R=1/2 hard", "Conv K=7 R=1/2 soft"};
static const double RATE[NS] = {1.0, 4.0 / 7.0, 0.5, 0.5};

int main() {
    const double lo = 0, hi = 7, step = 0.5;
    const int npts = int((hi - lo) / step) + 1;
    const size_t NINFO = 1000, MAXBITS = 600000, MINERR = 60;
    struct Row { size_t bits = 0, errs = 0; };
    std::vector<Row> rows(NS * npts);

    parallelFor((int)rows.size(), [&](int idx) {
        int sc = idx / npts;
        double db = lo + step * (idx % npts);
        double sigma = std::sqrt(noiseN0(db2lin(db), 1, RATE[sc]) / 2.0);
        Rng rng(777 + 104729ull * idx);
        std::normal_distribution<double> g(0, sigma);
        Row& r = rows[idx];
        while (r.bits < MAXBITS && r.errs < MINERR) {
            Bits info = randomBits(NINFO, rng);
            Bits coded = sc == UNCODED ? info : sc == HAMMING ? Hamming74::encode(info) : ConvK7::encode(info);
            std::vector<double> rx(coded.size());
            for (size_t i = 0; i < coded.size(); ++i) rx[i] = (coded[i] ? -1.0 : 1.0) + g(rng);
            Bits dec;
            if (sc == UNCODED) {
                dec.resize(NINFO);
                for (size_t i = 0; i < NINFO; ++i) dec[i] = rx[i] < 0;
            } else if (sc == HAMMING) {
                Bits h(rx.size());
                for (size_t i = 0; i < rx.size(); ++i) h[i] = rx[i] < 0;
                dec = Hamming74::decode(h);
            } else {
                if (sc == CONV_HARD) for (auto& v : rx) v = v < 0 ? -1.0 : 1.0;
                dec = ConvK7::decode(rx, NINFO);
            }
            r.errs += countErrors(info, dec);
            r.bits += NINFO;
        }
    });

    FILE* f = fopen("results/coding_gain.csv", "w");
    fprintf(f, "scheme,ebn0_db,bits,errors,ber\n");
    for (size_t i = 0; i < rows.size(); ++i)
        fprintf(f, "%s,%.1f,%zu,%zu,%.3e\n", NAMES[i / npts], lo + step * (i % npts), rows[i].bits,
                rows[i].errs, (double)rows[i].errs / rows[i].bits);
    fclose(f);
    puts("wrote results/coding_gain.csv");
}
