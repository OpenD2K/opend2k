"""The Emperor's construction yard (IMPERIAL_CONSTRUCTION_YARD, art 58: frames 3892 / 3893, animation 3): the
original is the Harkonnen yard's picture in the Emperor's colour, so this runs the Harkonnen build
(tools/models/harkonnen_construction_yard.py) under that key.

    blender -b --factory-startup --python tools/models/harkonnen_imperial_construction_yard.py --
    D2K_BUILDING=IMPERIAL_CONSTRUCTION_YARD .venv/bin/python -I tools/models/harkonnen_check/pack.py
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import harkonnen_kit as K  # noqa: E402
import harkonnen_construction_yard as H  # noqa: E402

KEY = "IMPERIAL_CONSTRUCTION_YARD"

if __name__ == "__main__":
    K.run(KEY, H.build, poses=H.A.POSES, rest=H.A.rest, spec=H.SPEC, P=H.P, short="imperial_construction_yard")
