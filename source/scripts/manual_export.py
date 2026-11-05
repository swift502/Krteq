import os
import shutil
import subprocess
import tempfile
from pathlib import Path

name = "krteq_manual"
root = Path(__file__).resolve().parents[2]
html = root / "source" / "manual" / f"{name}.html"
pdf = root / "production" / f"{name}.pdf"


def find_browser():
    candidates = [
        Path(os.environ.get("PROGRAMFILES(X86)", "")) / "Microsoft/Edge/Application/msedge.exe",
        Path(os.environ.get("PROGRAMFILES", "")) / "Microsoft/Edge/Application/msedge.exe",
        Path(os.environ.get("PROGRAMFILES", "")) / "Google/Chrome/Application/chrome.exe",
        Path(os.environ.get("PROGRAMFILES(X86)", "")) / "Google/Chrome/Application/chrome.exe",
        Path(os.environ.get("LOCALAPPDATA", "")) / "Google/Chrome/Application/chrome.exe",
    ]
    for path in candidates:
        if path.is_file():
            return str(path)
    for exe in ("msedge", "chrome", "google-chrome", "chromium"):
        found = shutil.which(exe)
        if found:
            return found
    print("No Edge/Chrome browser found.")
    raise SystemExit(1)


# Page size comes from the @page rule in the HTML
pdf.parent.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory() as profile:
    subprocess.run([
        find_browser(),
        "--headless",
        "--disable-gpu",
        "--no-pdf-header-footer",
        "--virtual-time-budget=10000",  # let web fonts load before printing
        f"--user-data-dir={profile}",
        f"--print-to-pdf={pdf}",
        html.as_uri(),
    ], check=True, capture_output=True)
print(f"Exported {pdf}")