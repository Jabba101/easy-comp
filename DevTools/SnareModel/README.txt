The first modelled snare (experiment, not shipped) - fitted to the user's own
Yamaha SD296 steel snare recordings (open, gel pad, damping ring; the
recordings are not in the repo).

  esprit.py    fits a hit's rings (pitch, decay, strength, phase) from the waveform
  residual.py  what the rings don't explain: the wires' colour and envelope, the stick's tick
  snare.py     the model: fitted rings (rising in over the 3 ms of stick contact),
               a pitch lift on hard hits, softer = darker, the stick's papery tick,
               and the wires (plateau, then a smooth fade, pulsing at the snare-side
               head's rate - 166 Hz open / with the ring, 540 Hz with the gel)
  the rest     the measurements (hits, pitches, decays, wires) used along the way
