"""Entry point of the debug exe (built by `build.ps1 -Debug`): turns debug mode on, then runs normally."""
import os
import sys

os.environ["WANDLIKE_DEBUG"] = "1"

from wandlike.__main__ import main

if __name__ == "__main__":
    sys.exit(main())
