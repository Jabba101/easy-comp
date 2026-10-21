import numpy as np
exec(open('proto.py').read().split("if __name__")[0])
t = np.arange(int(2*SR))/SR
for f in (60.0, 200.0, 1000.0):
    for over in (6, 12):
        x = 10**((-20+over)/20)*np.sin(2*np.pi*f*t)
        L = 20*np.log10(np.maximum(np.abs(x),1e-6))
        gm = modern(L, -20, 4, 10, 100); gv = vintage(L, -20, 4, 10, 100)
        gv2 = vintage(L, -20, 4, 10, 100, alpha=0.0, cap=1.0)
        print(f'{f:6.0f} Hz over {over:2d}: modern {np.mean(gm[-4800:]):5.2f} (ripple {np.ptp(gm[-4800:]):4.2f})  vintage a1 {np.mean(gv[-4800:]):5.2f} (ripple {np.ptp(gv[-4800:]):4.2f})  vintage a0 {np.mean(gv2[-4800:]):5.2f} (ripple {np.ptp(gv2[-4800:]):4.2f})   static target {0.75*over:4.2f}')
