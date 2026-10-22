// Raised-cosine pulse shaping, eye diagram traces, with and without ISI. Output: results/eye.csv
#include <cstdio>
#include "commsim/common.hpp"
using namespace commsim;
static const double PI = 3.14159265358979323846;

static double rc(double t, double beta) {  // t in symbol periods
    double s = std::fabs(t) < 1e-9 ? 1.0 : std::sin(PI * t) / (PI * t);
    double d = 1.0 - (2 * beta * t) * (2 * beta * t);
    if (std::fabs(d) < 1e-9) return PI / 4 * (std::fabs(t) < 1e-9 ? 1.0 : std::sin(PI * t) / (PI * t));
    return s * std::cos(PI * beta * t) / d;
}

int main() {
    const int SPS = 16, SPAN = 8, NSYM = 400, NTR = 250;
    struct Case { const char* name; double beta; std::vector<double> isi; };
    std::vector<Case> cases = {{"RC beta=0.35", 0.35, {1}},
                               {"RC beta=1.0", 1.0, {1}},
                               {"RC beta=0.35 + ISI channel", 0.35, {1, 0.5, 0.3}}};
    FILE* f = fopen("results/eye.csv", "w");
    fprintf(f, "case,trace,sample,value\n");
    for (auto& c : cases) {
        Rng rng(5);
        std::normal_distribution<double> g(0, 0.03);
        std::vector<double> a(NSYM), ae(NSYM, 0.0);
        for (auto& x : a) x = (rng() & 1) ? 1.0 : -1.0;
        for (int k = 0; k < NSYM; ++k)  // symbol-rate ISI channel
            for (size_t j = 0; j < c.isi.size(); ++j)
                if (k >= (int)j) ae[k] += c.isi[j] * a[k - j];
        std::vector<double> y((NSYM + SPAN) * SPS + 1, 0.0);
        for (int k = 0; k < NSYM; ++k)
            for (int n = 0; n <= SPAN * SPS; ++n)
                y[k * SPS + n] += ae[k] * rc((n - SPAN * SPS / 2.0) / SPS, c.beta);
        for (auto& v : y) v += g(rng);
        for (int tr = 0; tr < NTR; ++tr) {
            int peak = (SPAN + tr) * SPS + SPAN * SPS / 2;
            for (int s = -SPS; s < SPS; ++s)
                fprintf(f, "%s,%d,%.4f,%.4f\n", c.name, tr, (double)s / SPS, y[peak + s]);
        }
    }
    fclose(f);
    puts("wrote results/eye.csv");
}
