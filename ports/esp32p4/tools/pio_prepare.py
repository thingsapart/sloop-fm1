"""PlatformIO pre-build dependency checks."""
Import("env")
import subprocess
import sys
from pathlib import Path
root=Path(env.subst("$PROJECT_DIR"))
subprocess.run([sys.executable,str(root/"tools/prepare.py")],check=True)
