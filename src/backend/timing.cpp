#include "backend_impl.h"
#include <algorithm>
#include <cmath>

namespace citron {
namespace {
uint64_t Rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
}  // namespace

void Xrng::seed(uint64_t salt) {
    // splitmix64, so nearby salts give unrelated streams
    for (uint64_t& w : s) {
        salt += 0x9E3779B97F4A7C15ull;
        uint64_t z = salt;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        w = z ^ (z >> 31);
    }
}

uint64_t Xrng::next() {
    const uint64_t result = Rotl(s[1] * 5, 7) * 9;
    const uint64_t t = s[1] << 17;
    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = Rotl(s[3], 45);
    return result;
}

double Xrng::unit() { return (next() >> 11) * (1.0 / 9007199254740992.0); }

int Xrng::range(int lo, int hi) { return hi <= lo ? lo : lo + (int)(next() % (uint64_t)(hi - lo)); }

double Xrng::gaussian() {
    double u1 = unit(), u2 = unit();
    while (u1 <= 1e-15) u1 = unit();
    return std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
}

// The rate is drawn so the period, not the CPS, is spread evenly over the range; a slow drift wanders
// over seconds and a fast jitter keeps neighbouring clicks apart; now and then a click hesitates or
// comes early; the press length varies so the duty cycle does too. A period that leaves its band is
// reflected back inside rather than stacked on the edge.
HumanDelay::Pair HumanDelay::get(double minCps, double maxCps, Xrng& rng) {
    const double lo = std::clamp(std::min(minCps, maxCps), 1.0, 20.0);
    const double hi = std::clamp(std::max(minCps, maxCps), 1.0, 20.0);
    const double u = rng.unit();
    const double cps = hi <= lo ? lo : std::sqrt(lo * lo + u * (hi * hi - lo * lo));
    const double target = 1000.0 / std::max(cps, 1.0);
    drift = drift * 0.97 + rng.gaussian() * 0.012;
    const double driftF = 1.0 + std::clamp(drift, -0.06, 0.06);
    const double jit = std::clamp(1.0 + rng.gaussian() * 0.05, 0.93, 1.07);
    double period = target * driftF * jit;
    const double minP = 1000.0 / hi, maxP = 1000.0 / lo;
    const double center = (minP + maxP) * 0.5;
    const double halfw = std::max((maxP - minP) * 0.5, center * 0.08);
    const double loB = center - halfw, hiB = center + halfw;
    if (period > hiB || period < loB) {
        const double band = (hiB - loB) * 0.04;
        period = period > hiB ? hiB - rng.unit() * band : loB + rng.unit() * band;
    }
    const double r = rng.unit();
    if (r < 0.006) period += rng.range(8, 30);
    else if (r < 0.018) period += rng.range(2, 8);
    else if (r < 0.030) period -= rng.range(2, 8);
    period = std::max(period, 5.0);
    const int p = (int)std::lround(period);
    const int downCap = std::min(26, std::max(3, p - 2));
    const int downMin = std::min(3, downCap);
    int down = downMin >= downCap ? downCap : rng.range(downMin, downCap + 1);
    int up = p - down;
    if (up < 2) {
        down = std::clamp(p - 2, 2, downCap);
        up = p - down;
    }
    return {(double)std::max(up, 1), (double)std::max(down, 1)};
}

// A Lissajous walk, so consecutive offsets are neighbours and the shake has a direction.
bool SmoothJit::next(int intensity, Xrng& rng, int& ox, int& oy) {
    ox = oy = 0;
    if (intensity <= 0) return false;
    t += 0.35;
    if (t > 6.283185307179586) t -= 6.283185307179586;
    const double i = intensity;
    ox = (int)std::lround(std::sin(t) * i * (rng.unit() * 0.6 + 0.4));
    oy = (int)std::lround(std::cos(t * 0.8) * (i * 0.4) * (rng.unit() * 0.6 + 0.4));
    return ox != 0 || oy != 0;
}

}  // namespace citron
