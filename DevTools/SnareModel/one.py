import sys, os; sys.path.insert(0, '/home/claude/snare/tools')
import numpy as np, snare
dmp, out = sys.argv[1], sys.argv[2]; vel = [float(v) for v in sys.argv[3].split(',')]
np.save(out, np.stack([snare.render(dmp, velocity=v, seed=i + 17, length=1.4) for i, v in enumerate(vel)]))
