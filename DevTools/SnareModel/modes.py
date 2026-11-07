import sys, numpy as np, soundfile as sf
from scipy.signal import find_peaks
f = sys.argv[1]; start = float(sys.argv[2]); length = float(sys.argv[3])
x, sr = sf.read(f)
print('channels: L vs R level %.1f dB, polarity corr %.2f' % (20*np.log10(np.std(x[:,0])/np.std(x[:,1])), np.corrcoef(x[:,0],x[:,1])[0,1]))
for ch, name in ((0,'L'),(1,'R')):
    i = int(start*sr)
    # skip the stick (first 25 ms): the rings only
    a = x[i+int(0.025*sr): i+int(length*sr), ch]
    w = np.hanning(len(a)); n = 1 << 18
    spec = np.abs(np.fft.rfft(a*w, n)); fr = np.fft.rfftfreq(n, 1/sr)
    db = 20*np.log10(spec/spec.max()+1e-12)
    band = (fr > 60) & (fr < 3000)
    pk, prop = find_peaks(db[band], prominence=6, distance=int(8/(fr[1])))
    sel = sorted(zip(db[band][pk], fr[band][pk]), reverse=True)[:14]
    print(name, 'strongest rings (Hz, dB):', ', '.join('%.0f (%.0f)' % (f_, d) for d, f_ in sorted(sel, key=lambda t: t[1])))
    # where the energy is
    for lo, hi in ((60,150),(150,300),(300,600),(600,1200),(1200,3000),(3000,6000),(6000,12000),(12000,20000)):
        m = (fr>=lo)&(fr<hi); print('   %5d-%5d Hz: %5.1f dB' % (lo, hi, 10*np.log10((spec[m]**2).sum()/ (spec**2).sum()+1e-15)))
