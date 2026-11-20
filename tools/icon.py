#!/usr/bin/env python3
"""card/icon.png (52 px) and card/icon-40.png (40 px): an original drawing, an isometric block
with a grassy top on a sky-coloured disc. Drawn at 8x and shrunk. Usage: tools/icon.py"""
import pathlib
from PIL import Image, ImageDraw

def icon(size):
    s = 8
    n = size * s
    im = Image.new('RGBA', (n, n), (0, 0, 0, 0))
    disc = Image.new('RGBA', (n, n), (0, 0, 0, 0))
    g = ImageDraw.Draw(disc)
    for y in range(n):  # sky: deep blue at the top to pale at the bottom
        t = y / n
        g.line([(0, y), (n, y)], fill=(int(60 + 110 * t), int(130 + 80 * t), int(220 + 30 * t), 255))
    mask = Image.new('L', (n, n), 0)
    ImageDraw.Draw(mask).ellipse([s, s, n - s, n - s], fill=255)
    im.paste(disc, (0, 0), mask)

    d = ImageDraw.Draw(im)
    c, r = n / 2, n * 0.29           # centre and half-width of the block
    top, mid, bot = n * 0.20, n * 0.36, n * 0.80
    h = mid - top                     # the top face's half-height
    T = (c, top); L = (c - r, mid - 0); R = (c + r, mid); M = (c, mid + h)
    Lb = (c - r, bot - h); Rb = (c + r, bot - h); B = (c, bot)
    d.polygon([L, M, B, Lb], fill=(128, 86, 52, 255))        # left side: earth
    d.polygon([M, R, Rb, B], fill=(98, 64, 38, 255))         # right side: earth, in shade
    d.polygon([T, R, M, L], fill=(104, 178, 64, 255))        # top: grass
    # the grass hangs over the sides' top edge, ragged
    k = 7
    for i in range(k):
        x0 = L[0] + (M[0] - L[0]) * i / k; y0 = L[1] + (M[1] - L[1]) * i / k
        x1 = L[0] + (M[0] - L[0]) * (i + 1) / k; y1 = L[1] + (M[1] - L[1]) * (i + 1) / k
        drop = n * (0.05 if i % 2 else 0.08)
        d.polygon([(x0, y0), (x1, y1), (x1, y1 + drop), (x0, y0 + drop)], fill=(86, 152, 52, 255))
        x0 = M[0] + (R[0] - M[0]) * i / k; y0 = M[1] + (R[1] - M[1]) * i / k
        x1 = M[0] + (R[0] - M[0]) * (i + 1) / k; y1 = M[1] + (R[1] - M[1]) * (i + 1) / k
        drop = n * (0.07 if i % 2 else 0.04)
        d.polygon([(x0, y0), (x1, y1), (x1, y1 + drop), (x0, y0 + drop)], fill=(70, 128, 42, 255))
    edge = (40, 30, 20, 255)
    w = max(1, int(n * 0.012))
    for a, b in ((T, R), (R, Rb), (Rb, B), (B, Lb), (Lb, L), (L, T), (L, M), (M, R), (M, B)):
        d.line([a, b], fill=edge, width=w)
    return im.resize((size, size), Image.Resampling.LANCZOS)

here = pathlib.Path(__file__).resolve().parent.parent / 'card'
icon(52).save(here / 'icon.png')
icon(40).save(here / 'icon-40.png')
