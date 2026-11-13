"""Contact sheet of the turntable previews: montage.py DIR NAME [NAME...] -> DIR/NAME_sheet.png
(4 x 2 grid per model). Run with the project venv: .venv/bin/python -I tools/models/montage.py ..."""
import os
import sys

from PIL import Image

d = sys.argv[1]
for name in sys.argv[2:]:
    ims = [Image.open(os.path.join(d, f"{name}_{i}.png")).convert("RGB") for i in range(8)]
    w, h = ims[0].size
    s = Image.new("RGB", (w * 4, h * 2))
    for i, im in enumerate(ims):
        s.paste(im, ((i % 4) * w, (i // 4) * h))
    s.save(os.path.join(d, f"{name}_sheet.png"))
