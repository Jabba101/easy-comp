import sys, numpy as np, soundfile as sf
for f in sys.argv[1:]:
    x, sr = sf.read(f)
    corr = np.corrcoef(x[:,0], x[:,1])[0,1] if x.ndim > 1 else 1
    m = x.mean(axis=1) if x.ndim > 1 else x
    env = np.abs(m)
    # onsets: rises above 15% of max after quiet
    th = env.max() * 0.15
    hits = []; i = 0
    while i < len(env):
        if env[i] > th:
            hits.append(i); i += int(0.25 * sr)
        else: i += 1
    print(f.split('/')[-1], 'L/R corr %.3f' % corr, 'peak %.1f dBFS' % (20*np.log10(np.abs(x).max())), 'hits:', len(hits))
    for h in hits:
        seg = m[h:h+int(0.05*sr)]
        print('   %.3f s  peak %.1f dB' % (h/sr, 20*np.log10(np.abs(seg).max()+1e-12)))
