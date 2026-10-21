import numpy as np, sys
import matplotlib; matplotlib.use('Agg')
import matplotlib.pyplot as plt
exec(open('proto.py').read().split("if __name__")[0])

def run(name, T, R, att, rel, seconds=8.0, alphas=(1.0,), cap=6.0):
    x = load(name, seconds)
    L = peak_level_db(x)
    gm = modern(L, T, R, att, rel)
    res = {'modern': gm}
    for a in alphas:
        res[f'vintage a={a}'] = vintage(L, T, R, att, rel, alpha=a, cap=cap)
    return x, L, res

def report(name, T, R, att, rel, alphas=(0.5, 1.0)):
    x, L, res = run(name, T, R, att, rel, alphas=alphas)
    print(f'{name}: T {T} R {R} att {att} rel {rel}')
    for k, g in res.items():
        # crest of output (peak/rms) as a transient measure, average GR, GR ripple (std of GR over 50 ms windows minus mean)
        mono = x.mean(axis=1) if x.ndim > 1 else x
        y = mono * 10 ** (-g / 20)
        crest_in = 20*np.log10(np.max(np.abs(mono))/np.sqrt(np.mean(mono**2)))
        crest_out = 20*np.log10(np.max(np.abs(y))/np.sqrt(np.mean(y**2)))
        # transient: energy of the first 5 ms after each onset relative to the next 50 ms, out vs in
        print(f'   {k:14s} avg GR {np.mean(g):5.2f}  max {np.max(g):5.2f}  p90 {np.percentile(g,90):5.2f}  crest in {crest_in:5.1f} out {crest_out:5.1f}  GR wobble {np.std(np.diff(g[::240])):.3f}')
    return x, L, res

for args in [('drumbus_rock', -24, 4, 10, 100), ('drumbus_rock', -24, 4, 30, 200), ('bass_picked', -26, 4, 10, 100), ('vocal_rock', -24, 3, 5, 120), ('mix_song', -16, 2, 30, 200)]:
    x, L, res = report(*args)
    fig, ax = plt.subplots(2, 1, figsize=(14, 6))
    t = np.arange(len(L)) / SR
    n = int(3 * SR)
    ax[0].plot(t[:n], L[:n], color='0.6', lw=0.5)
    ax[0].axhline(args[1], color='k', ls='--', lw=0.8)
    for k, g in res.items():
        ax[1].plot(t[:n], -g[:n], lw=1.0, label=k)
    ax[1].legend(); ax[0].set_title(str(args))
    plt.tight_layout(); plt.savefig(f'dyn_{args[0]}_{args[3]}_{args[4]}.png', dpi=60); plt.close()
