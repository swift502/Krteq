import os
import shutil
import subprocess
import tempfile
from pathlib import Path

try:
    from pypdf import PageObject, PdfReader, PdfWriter, Transformation
except ImportError:
    print("pypdf not found. Install with: pip install pypdf")
    raise SystemExit(1)

# Config
name = "krteq_manual"
root = Path(__file__).resolve().parents[2]
html = root / "source" / "manual" / f"{name}.html"
pdf_a5 = root / "production" / f"{name}_A5.pdf"
pdf_a4 = root / "production" / f"{name}_A4.pdf"

# A4 landscape in points
a4_w, a4_h = 841.89, 595.28


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


# A5 export, page size comes from the @page rule in the HTML
pdf_a5.parent.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory() as profile:
    subprocess.run([
        find_browser(),
        "--headless",
        "--disable-gpu",
        "--no-pdf-header-footer",
        "--virtual-time-budget=10000",  # let web fonts load before printing
        f"--user-data-dir={profile}",
        f"--print-to-pdf={pdf_a5}",
        html.as_uri(),
    ], check=True, capture_output=True)
print(f"Exported {pdf_a5}")

# A4 landscape export, two A5 pages side by side
reader = PdfReader(pdf_a5)
writer = PdfWriter()
pages = list(reader.pages)
for i in range(0, len(pages), 2):
    sheet = PageObject.create_blank_page(width=a4_w, height=a4_h)
    for slot, page in enumerate(pages[i:i + 2]):
        w = float(page.mediabox.width)
        h = float(page.mediabox.height)
        x = (a4_w - 2 * w) / 2 + slot * w
        y = (a4_h - h) / 2
        sheet.merge_transformed_page(page, Transformation().translate(x, y))
    writer.add_page(sheet)
with open(pdf_a4, "wb") as f:
    writer.write(f)
print(f"Exported {pdf_a4}")
