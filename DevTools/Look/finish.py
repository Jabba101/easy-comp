#!/usr/bin/env python3
"""Cleans up the renders and builds the turning-knob clip and a filmstrip sheet."""
import glob
import sys

import cv2
import numpy as np
from PIL import Image

OUT = '/home/claude/render/out'


def load(path):
    img = cv2.imread(path, cv2.IMREAD_UNCHANGED)
    if img.dtype == np.uint16:
        img = (img / 257.0).round().astype(np.uint8)
    return img[:, :, :3]


def clean(img, strength=3):
    """Light denoise (the renderer here has no built-in denoiser), plus a soft vignette."""
    img = cv2.fastNlMeansDenoisingColored(img, None, strength, strength, 5, 15)
    h, w = img.shape[:2]
    y, x = np.mgrid[0:h, 0:w]
    r = np.sqrt(((x - w / 2) / (w / 2)) ** 2 + ((y - h / 2) / (h / 2)) ** 2)
    vignette = 1.0 - 0.16 * np.clip(r - 0.35, 0, None) ** 1.5
    return np.clip(img * vignette[:, :, None], 0, 255).astype(np.uint8)


def to_pil(img):
    return Image.fromarray(cv2.cvtColor(img, cv2.COLOR_BGR2RGB))


if 'still' in sys.argv:
    still = clean(load('/home/claude/render/still.png'), 3)
    to_pil(still).save(f'{OUT}/field_case_knob.png', optimize=True)
    print('still done')

if 'clip' in sys.argv:
    frames = [to_pil(clean(load(p), 4)) for p in sorted(glob.glob('/home/claude/render/sweep/frame_*.png'))]
    sequence = frames + frames[-2:0:-1]                       # there and back again
    durations = [60] * len(sequence)
    durations[0] = durations[len(frames) - 1] = 700           # pause at each end
    palette_source = frames[len(frames) // 2].quantize(colors=255, method=Image.Quantize.MEDIANCUT)
    gif = [f.quantize(palette=palette_source, dither=Image.Dither.FLOYDSTEINBERG) for f in sequence]
    gif[0].save(f'{OUT}/field_case_knob_turning.gif', save_all=True, append_images=gif[1:], duration=durations,
                loop=0, optimize=False, disposal=1)

    # The filmstrip a plugin would load: the same knob at each position, one after another
    w, h = frames[0].size
    cx, cy, half = w // 2, int(h / 2 - 0.003 * (w / 0.1296)), int(0.024 * (w / 0.1296))
    picks = [frames[i] for i in np.linspace(0, len(frames) - 1, 8).round().astype(int)]
    tiles = [f.crop((cx - half, cy - half, cx + half, cy + half)) for f in picks]
    gap = 6
    sheet = Image.new('RGB', (len(tiles) * (2 * half) + (len(tiles) - 1) * gap, 2 * half), (14, 15, 10))
    for i, tile in enumerate(tiles):
        sheet.paste(tile, (i * (2 * half + gap), 0))
    sheet.save(f'{OUT}/field_case_knob_filmstrip.png', optimize=True)
    print('clip done:', len(frames), 'frames')
