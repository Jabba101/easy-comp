import numpy as np
from lib import *
V = [('modern', modern),
     ('B R/sqR', lambda L,T,R,a,r: vintage2(L,T,R,a,r, lambda s: 1+s, lambda s: np.sqrt(1+s))),
     ('B3 R/min(sqR,3)', lambda L,T,R,a,r: vintage2(L,T,R,a,r, lambda s: 1+s, lambda s: min(np.sqrt(1+s), 3.0))),
     ('B.4 R/R^.4', lambda L,T,R,a,r: vintage2(L,T,R,a,r, lambda s: 1+s, lambda s: (1+s)**0.4))]
for args in [('drumbus_rock', -20, 10, 3, 80), ('drumbus_rock', -18, 20, 1, 100), ('vocal_rock', -20, 8, 2, 100), ('bass_picked', -22, 10, 5, 80), ('mix_song', -12, 20, 1, 60)]:
    report(*args, V)
