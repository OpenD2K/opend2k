"""The old name of building_common.py (the shared code of the Reimagined pack's redesigned buildings): the three wind
trap generators import it under this name. It IS that module - `import windtrap_common as WC` hands back
building_common itself, so the settings, the palette and the helpers are one set whichever name a file uses.
docs/3d-models.md "Wind trap redesigns" -> "The common module"."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import building_common  # noqa: E402

sys.modules[__name__] = building_common
