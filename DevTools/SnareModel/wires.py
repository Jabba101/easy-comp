import sys, numpy as np, soundfile as sf
from scipy.signal import butter, sosfiltfilt
for f in sys.argv[1:]:
    x, sr = sf.read(f); m = x.mean(axis=1)
    name = f.split('/')[-1].replace('SD296_','').replace('.wav','')
    out = []
    for lo, hi in ((150, 300), (2500, 6000), (6000, 14000)):
        sos = butter(4, [lo, hi], 'band', fs=sr, output='sos'); y = sosfiltfilt(sos, m[:int(1.9*sr)])
        e = np.sqrt(np.convolve(y*y, np.ones(96)/96, 'same')); e_db = 20*np.log10(e/e.max()+1e-9)
        pk = np.argmax(e)
        t = lambda d: (np.where(e_db[pk:] < d)[0][0] + 0) / sr * 1000
        out.append('%d-%dk: peak at %.1f ms, -20 dB after %.0f ms, -40 dB after %.0f ms' % (lo, hi//1000, pk/sr*1000, t(-20), t(-40)))
    print(name); [print('   ', o) for o in out]
    # attack: spectrum of the first 6 ms vs the next 100 ms
    i = np.argmax(np.abs(m[:2000]) > 0.05)
    a = m[i:i+int(0.006*sr)]; b = m[i+int(0.03*sr):i+int(0.13*sr)]
    def bands(s):
        sp = np.abs(np.fft.rfft(s*np.hanning(len(s)), 1<<14))**2; fr = np.fft.rfftfreq(1<<14, 1/sr)
        return [10*np.log10(sp[(fr>=lo)&(fr<hi)].sum()/sp.sum()+1e-15) for lo, hi in ((60,300),(300,1000),(1000,3000),(3000,8000),(8000,20000))]
    print('    first 6 ms (stick + crack):', ' '.join('%.0f' % v for v in bands(a)), ' | 30-130 ms (body + wires):', ' '.join('%.0f' % v for v in bands(b)), '(dB in 60-300/300-1k/1-3k/3-8k/8-20k)')
