import sys, os; sys.path.insert(0, '/home/claude/snare/tools')
import numpy as np, snare
SR = 48000
dmp = sys.argv[1]; hits = [tuple(map(float, h.split(':'))) for h in sys.argv[3].split(',')]
bpm = 100; beat = 60 / bpm; bars = 4
n = int((bars * 4 * beat + 2) * SR); y = np.zeros(n); rng = np.random.default_rng(11)
for bar in range(bars):
    for pos, v in hits:
        t = (bar * 4 + pos) * beat + rng.normal(0, 0.003)
        vv = float(np.clip(v * rng.normal(1, 0.05), 0.1, 1.0))
        h = snare.render(dmp, velocity=vv, seed=int(rng.integers(1, 1e6)), length=1.5)
        i = int(t * SR); y[i:i + len(h)] += h[:max(0, min(len(h), n - i))]
np.save(sys.argv[2], y)
