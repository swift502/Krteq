import argparse
import os
import shutil
import subprocess
from pathlib import Path

# Args
args_parse = argparse.ArgumentParser()
args_parse.add_argument("--clean", action="store_true")
args = args_parse.parse_args()

# Config
name = "krteq"
msys_exe = r"C:\QMK_MSYS\usr\bin\bash.exe"

# Local
root_local = Path(__file__).resolve().parents[2]
kb_local = root_local / "source" / "qmk"
bin_local = root_local / "production" / f"{name}_firmware.uf2"

# Remote
root_remote = Path.home() / "qmk_firmware"
kb_remote = root_remote / "keyboards" / name
bin_remote = root_remote / f"{name}_default.uf2"

# Remove existing
if kb_remote.exists():
    shutil.rmtree(kb_remote)
    print(f"Removed existing folder {kb_remote}")

# Setup qmk keyboard folder
shutil.copytree(kb_local, kb_remote)
print(f"Copied {kb_local} to {kb_remote}")

# Environment
# https://docs.qmk.fm/other_vscode#msys2-setup
env = os.environ.copy()
env["MSYSTEM"] = "MINGW64" # Will be deprecated in favor of UCRT64

# Args
command = f"qmk compile -kb {name} -km default"
if args.clean: command += " --clean"
args = [msys_exe, "--login", "-c", command]

# Run
print()
subprocess.run(args, env=env, check=True)
print()

# Retrieve bin file
shutil.move(bin_remote, bin_local)
print(f"Moved {bin_remote} to {bin_local}")

# Clean up
shutil.rmtree(kb_remote)
print(f"Cleaned up {kb_remote}")
