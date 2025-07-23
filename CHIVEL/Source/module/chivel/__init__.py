import sys

# Ensure Python 3.13 is being used
if sys.version_info[:2] != (3, 13):
    raise RuntimeError(
        f"CHIVEL requires Python 3.13, but you are using Python {sys.version_info.major}.{sys.version_info.minor}. "
        "Please install Python 3.13 to use this module."
    )

from .chivel import *