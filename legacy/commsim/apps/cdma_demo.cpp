// Synchronous DS-CDMA, BPSK, AWGN. Walsh (orthogonal) vs random PN codes, SF=16.
// Output: results/cdma.csv
#include <cstdio>
#include "commsim/cdma.hpp"
#include "commsim/channel.hpp"
using namespace commsim;

int main() {
    const int SF = 16;
    const int userList[] = {1, 2, 4, 8, 12};
    const char* codeNames[] = {"Walsh", "Random PN"};
    const double lo = 0, hi = 10, step = 1;
    const int npts = int((hi - lo) / step) + 1, nu = 5;
    const size_t NSYM = 60000;

    Rng crng(2024);
    auto W = walsh(SF);
    auto P = randomCodes(12, SF, crng);
    std::vector<double> ber(2 * nu * npts);

    parallelFor((int)ber.size(), [&](int idx) {
        int ct = idx / (nu * npts), ui = (idx / npts) % nu, pi = idx % npts;
        int U = userList[ui];
        const auto& C = ct == 0 ? W : P;
        double ebn0 = db2lin(lo + step * pi);
        double sigma = std::sqrt(1.0 / ebn0 / 2.0);  // Eb = 1 per user
        Rng rng(99 + 31337ull * idx);
        std::normal_distribution<double> g(0, sigma);
        const double a = 1.0 / std::sqrt((double)SF);
        size_t err = 0;
        std::vector<double> b(U);
        for (size_t n = 0; n < NSYM; ++n) {
            for (int u = 0; u < U; ++u) b[u] = (rng() & 1) ? 1.0 : -1.0;
            double corr = 0;
            for (int j = 0; j < SF; ++j) {
                double chip = 0;
                for (int u = 0; u < U; ++u) chip += b[u] * C[u][j] * a;
                chip += g(rng);
                corr += chip * C[0][j] * a;
            }
            err += (corr > 0) != (b[0] > 0);
        }
        ber[idx] = (double)err / NSYM;
    });

    FILE* f = fopen("results/cdma.csv", "w");
    fprintf(f, "codes,users,ebn0_db,ber\n");
    for (size_t i = 0; i < ber.size(); ++i) {
        int ct = i / (nu * npts), ui = (i / npts) % nu, pi = i % npts;
        fprintf(f, "%s,%d,%.1f,%.3e\n", codeNames[ct], userList[ui], lo + step * pi, ber[i]);
    }
    fclose(f);
    puts("wrote results/cdma.csv");
}
