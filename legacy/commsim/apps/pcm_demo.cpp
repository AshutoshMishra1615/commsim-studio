// PCM SNR vs bits, and delta modulation tracking. Outputs: results/pcm_snr.csv, results/dm.csv
#include <cstdio>
#include "commsim/pcm.hpp"
using namespace commsim;

int main() {
    const double PI = 3.14159265358979323846;
    FILE* f = fopen("results/pcm_snr.csv", "w");
    fprintf(f, "bits,snr_db,theory_db\n");
    for (int N = 2; N <= 12; ++N) {
        double ps = 0, pn = 0;
        for (int n = 0; n < 200000; ++n) {
            double x = std::sin(2 * PI * 0.0123456 * n);  // full-scale sine
            double y = pcmDecode(pcmEncode(x, N), N);
            ps += x * x; pn += (x - y) * (x - y);
        }
        fprintf(f, "%d,%.2f,%.2f\n", N, 10 * std::log10(ps / pn), 6.02 * N + 1.76);
    }
    fclose(f);

    // delta modulation: 1 kHz-ish sine sampled at 64 samples/period, three step sizes
    FILE* g = fopen("results/dm.csv", "w");
    fprintf(g, "n,input,step_0.02,step_0.1,step_0.4\n");
    DeltaMod d1(0.02), d2(0.1), d3(0.4);
    for (int n = 0; n < 192; ++n) {
        double x = 0.9 * std::sin(2 * PI * n / 64.0);
        d1.encode(x); d2.encode(x); d3.encode(x);
        fprintf(g, "%d,%.4f,%.4f,%.4f,%.4f\n", n, x, d1.est, d2.est, d3.est);
    }
    fclose(g);
    puts("wrote results/pcm_snr.csv, results/dm.csv");
}
