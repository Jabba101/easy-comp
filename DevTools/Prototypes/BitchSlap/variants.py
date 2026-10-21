import numpy as np
import slap, measure
SR = slap.SR
imp = np.zeros(int(0.4 * SR)); imp[0] = 1
src = slap.lpf(slap.hpf(imp, 180), 9000)

measure.report('A1 room', slap.room(src, rt60=3.0))

# early path: two different short diffuser chains straight to L and R
def early(x, chains):
    out = []
    for chain in chains:
        y = x.copy()
        for d, g in chain:
            y = slap.allpass(y, d, g)
        out.append(y)
    return np.stack(out, axis=1)

EARLY = [[(53, 0.7), (97, 0.7), (151, 0.7), (211, 0.7)], [(61, 0.7), (89, 0.7), (163, 0.7), (199, 0.7)]]
for mix in (0.25, 0.5, 1.0):
    ir = slap.room(src, rt60=3.0) + mix * early(src, EARLY)
    measure.report(f'A2 e{mix}', ir)

measure.report('B burst', slap.burst_ir(300))
noise = np.random.default_rng(0).standard_normal((int(0.3*SR), 2))
measure.report('noise', noise)
