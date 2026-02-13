import os
import shutil
import subprocess
from pathlib import Path

# Exec
msys_exe = r"C:\QMK_MSYS\usr\bin\bash.exe"

# Local
root_local = Path(__file__).parent.resolve()
kb_local = root_local / "source" / "qmk"
bin_local = root_local / "production" / "krteq.uf2"

# Remote
root_remote = Path.home() / "qmk_firmware"
kb_remote = root_remote / "keyboards" / "krteq"
bin_remote = root_remote / "krteq_default.uf2"

# Remove existing
if kb_remote.exists():
    shutil.rmtree(kb_remote)
    print(f"Removed existing folder '{kb_remote}'.")

# Setup qmk keyboard folder
shutil.copytree(kb_local, kb_remote)
print(f"Copied '{kb_local}' to '{kb_remote}'.")

# Environment
# https://docs.qmk.fm/other_vscode#msys2-setup
env = os.environ.copy()
env["MSYSTEM"] = "MINGW64"

# Args
command = "qmk compile -kb krteq -km default --clean"
args = [msys_exe, "--login", "-c", command]

# Run
print()
subprocess.run(args, env=env, check=True)
print()

# Move .uf2 file
shutil.move(bin_remote, bin_local)
print(f"Moved '{bin_remote}' to '{bin_local}'.")

# Clean up
shutil.rmtree(kb_remote)
print(f"Cleaned up '{kb_remote}'.")
