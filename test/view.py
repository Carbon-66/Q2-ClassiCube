#!/usr/bin/env python3
"""A panel dump (raw BGRA, 320 x 375 portrait) as the landscape view Q2 Pod shows, 375 x 320,
scaled up 2x: view.py in.raw out.png"""
import sys
from PIL import Image
raw = open(sys.argv[1], 'rb').read()
panel = Image.frombytes('RGBA', (320, 375), raw, 'raw', 'BGRA')
view = panel.transpose(Image.Transpose.ROTATE_90)  # panel row X, column 319 - Y is view (X, Y)
view.convert('RGB').resize((750, 640), Image.Resampling.NEAREST).save(sys.argv[2])
