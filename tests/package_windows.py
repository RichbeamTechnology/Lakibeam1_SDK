"""Build a customer ZIP from a tested Windows x64 executable."""
from pathlib import Path
import hashlib
import importlib.util
import shutil
import sys
import zipfile

root = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1]).resolve()
destination = Path(sys.argv[2]).resolve()
destination.mkdir(parents=True, exist_ok=True)
stage = destination / "LakiBeam1_Capture_Windows_x64"
stage.mkdir(exist_ok=True)
shutil.copy2(exe, stage / "LakiBeamCapture.exe")
for path in (root / "docs").glob("Quick_Start_*.txt"):
    shutil.copy2(path, stage / path.name)
source = stage / "source"
source.mkdir(exist_ok=True)
for name in ("offline", "docs", "tests"):
    shutil.copytree(root / name, source / name, dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
shutil.copy2(root / "CMakeLists.txt", source / "CMakeLists.txt")
# Import fixture helpers without consuming this packaging command's arguments.
saved = sys.argv
sys.argv = ["fixtures", str(exe)]
spec = importlib.util.spec_from_file_location("fixtures", root / "tests" / "test_capture.py")
fixtures = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixtures)
sys.argv = saved
(stage / "demo_synthetic.pcap").write_bytes(fixtures.pcap([fixtures.frame(p) for p in fixtures.payloads()]))
checksums = []
for path in sorted(stage.rglob("*")):
    if path.is_file() and path.name != "SHA256SUMS.txt":
        checksums.append(hashlib.sha256(path.read_bytes()).hexdigest() + "  " + path.relative_to(stage).as_posix())
(stage / "SHA256SUMS.txt").write_text("\n".join(checksums) + "\n", encoding="utf-8")
archive = destination / "LakiBeam1_Capture_Windows_x64.zip"
with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as out:
    for path in sorted(stage.rglob("*")):
        if path.is_file():
            out.write(path, path.relative_to(destination))
print(archive)
