import numpy as np
exec(open('proto.py').read().split("if __name__")[0])

def follower_db(x, rel_ms):
    p = np.max(np.abs(x), axis=1) if x.ndim > 1 else np.abs(x)
    c = np.exp(-1/(rel_ms*1e-3*SR))
    y = np.empty_like(p); s = 0.0
    for i, v in enumerate(p):
        s = v if v > s else v + c*(s - v)
        y[i] = s
    return 20*np.log10(np.maximum(y, 1e-6))

t = np.arange(int(2*SR))/SR
print('steady tones, 12 dB over, R 4, att 10 rel 100: mean GR (ripple)')
for f in (50.0, 100.0, 1000.0):
    x = 10**((-20+12)/20)*np.sin(2*np.pi*f*t)
    L0 = peak_level_db(x)
    row = [f'{f:5.0f} Hz', f'modern {np.mean(modern(L0,-20,4,10,100)[-4800:]):5.2f} ({np.ptp(modern(L0,-20,4,10,100)[-4800:]):4.2f})']
    for rel in (0, 10, 20):
        L = L0 if rel == 0 else follower_db(x, rel)
        g = vintage(L, -20, 4, 10, 100)
        row.append(f'V{rel:2d} {np.mean(g[-4800:]):5.2f} ({np.ptp(g[-4800:]):4.2f})')
    print('   '.join(row))

for name, T, R, a, r in [('drumbus_rock', -24, 4, 10, 100), ('bass_picked', -26, 4, 10, 100), ('vocal_rock', -24, 3, 5, 120)]:
    x = load(name, 8.0)
    L0 = peak_level_db(x)
    gm = modern(L0, T, R, a, r)
    print(f'{name}: modern avg {np.mean(gm):4.2f} wobble {np.std(np.diff(gm[::240])):.3f}', end='')
    for rel in (0, 10, 20):
        L = L0 if rel == 0 else follower_db(x, rel)
        g = vintage(L, T, R, a, r)
        print(f' | V{rel} avg {np.mean(g):4.2f} wobble {np.std(np.diff(g[::240])):.3f}', end='')
    print()
