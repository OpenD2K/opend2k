#!/usr/bin/env python3
"""Write the models.ini sections of the redesigned refineries from the anchors their glb files carry
(as tools/models/heavy_factory_ini.py does for the heavy factories).

  .venv/bin/python -I tools/models/refinery_dock_ini.py artpacks/opend2k-redesign [--print]

For every *REFINERY.glb in <pack>/models/building: the fx_* nodes (fx_flue_N = the open flue tops, fx_inlet_0 = where
the docked harvester's pipe ends) become "anchor.NAME = x y z [size]" lines (glb axes, tiles) in the section [KEY]
of <pack>/models.ini. Only these sections are replaced; the rest of the file is kept byte for byte. --print only
shows the sections. The engine reads the same nodes from the glb; the lines make them visible and editable.
Check the dock itself with tools/models/refinery_dock_check.py.
"""
import glob
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from refinery_dock_check import read_glb  # noqa: E402

HEAD = """; 3D layout (tools/models/refinery_dock.py, docs/3d-models.md "Docking"): laid out on the cells - the pad on the dock
; cell's centre, nothing in the harvester's turning circle, the inlet socket where the centred harvester's pipe ends
; (fx_inlet_0: 26.35 px behind the dock centre, 10.6 px above the plate it stands on). Default fit: no move, scale 1.
; fx_flue_N: the open flue tops (fire / smoke). The 2D frames stay rendered from the 2D layout (--set inlet_2d=1)."""


def main():
    pack = sys.argv[1]
    ini = os.path.join(pack, "models.ini")
    text = open(ini).read() if os.path.exists(ini) else ""
    for path in sorted(glob.glob(os.path.join(pack, "models", "building", "*REFINERY.glb"))):
        key = os.path.basename(path)[:-4]
        _tris, anchors = read_glb(path)
        lines = [f"[{key}]", HEAD]
        for name in sorted(anchors):
            x, y, z, size = anchors[name]
            lines.append(f"anchor.{name} = {x / 32:.4f} {y / 32:.4f} {z / 32:.4f}" + ("" if name.startswith("fx_inlet") else f" {round(float(size), 3):g}"))
        block = "\n".join(lines) + "\n\n"
        if "--print" in sys.argv:
            print(block, end="")
            continue
        sec = re.compile(r"(?ms)^\[" + re.escape(key) + r"\]\n.*?(?=^\[|\Z)")
        text = sec.sub(lambda _m: block, text) if sec.search(text) else text.rstrip("\n") + "\n\n" + block
    if "--print" not in sys.argv:
        tmp = ini + f".tmp{os.getpid()}"
        open(tmp, "w").write(text.rstrip("\n") + "\n")
        os.replace(tmp, ini)
        print("wrote", ini)


if __name__ == "__main__":
    main()
