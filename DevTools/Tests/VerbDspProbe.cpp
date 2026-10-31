// Quick probe of NaturalReverb: measured decay time, level, stability, cost. (Not shipped.)
#include "NaturalReverb.h"
#include <iostream>
#include <chrono>
int main()
{
    const double sr = 48000.0;
    const char* names[] { "ROOM", "CLUB", "HALL", "PLATE" };
    for (int t = 0; t < 4; ++t)
        for (float decay : { 0.3f, 1.2f, 3.0f, 6.0f })
        {
            NaturalReverb r; r.prepare (sr);
            NaturalReverb::Settings s; s.type = (NaturalReverb::Type) t; s.decay = decay; s.preDelayMs = 0; s.tone = 0;
            r.setSettings (s, true);
            const int n = (int) (sr * (decay * 2 + 1));
            std::vector<float> in (n, 0.0f), L (n, 0.0f), R (n, 0.0f);
            // 100 ms of noise burst
            juce::Random rnd (1);
            for (int i = 0; i < 4800; ++i) in[i] = rnd.nextFloat() * 2 - 1;
            for (int i = 0; i < n; i += 512) r.process (in.data() + i, L.data() + i, R.data() + i, std::min (512, n - i), 1.0f);
            // energy in 50 ms windows
            auto win = [&] (double t0) { double e = 0; int a = (int) (t0 * sr); for (int i = a; i < a + 2400 && i < n; ++i) e += L[i]*L[i] + R[i]*R[i]; return 10 * std::log10 (e / 2400 + 1e-20); };
            const double peak = std::max ({ win (0.05), win (0.1), win (0.15) });
            // time to fall 30 dB below the level at 0.15 s, x2 = RT60
            double ref = win (0.15), t30 = -1;
            for (double tt = 0.15; tt < n / sr - 0.05; tt += 0.01) if (win (tt) < ref - 30) { t30 = tt - 0.15; break; }
            double corr = 0, el = 0, er = 0; for (int i = 0; i < n; ++i) { corr += L[i]*R[i]; el += L[i]*L[i]; er += R[i]*R[i]; }
            bool finite = true; for (auto v : L) if (! std::isfinite (v)) finite = false;
            std::printf ("%-5s decay %.1f: peak %6.1f dB  RT60~ %.2f s  L/R corr %.2f  finite %d\n", names[t], decay, peak, t30 * 2, corr / std::sqrt (el * er + 1e-30), finite);
        }
    NaturalReverb r; r.prepare (sr); NaturalReverb::Settings s; s.type = NaturalReverb::Type::hall; s.decay = 2; r.setSettings (s, true);
    std::vector<float> in (512, 0.1f), L (512), R (512);
    auto t0 = std::chrono::steady_clock::now();
    for (int b = 0; b < 940; ++b) { in[0] = (float) b; r.process (in.data(), L.data(), R.data(), 512, 1.0f); }
    std::printf ("10 s of audio in %.1f ms\n", std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count());
}
