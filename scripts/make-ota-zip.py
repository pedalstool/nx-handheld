#!/usr/bin/env python3
"""Build a slim OTA zip from a full device release zip.

Compares MinUI.zip inside the new full zip against MinUI.zip from the previous
release. Writes an outer zip that contains only a MinUI.zip of changed/added
entries (CRC/size). Boot's unzip -o merges that into .system, so cores and
other unchanged files stay put.

Usage:
  scripts/make-ota-zip.py <prev.zip|prev-MinUI.zip> <new-full.zip> <out-ota.zip>
"""

from __future__ import annotations

import io
import sys
import zipfile
from pathlib import Path


def load_minui_members(path: Path) -> dict[str, zipfile.ZipInfo]:
	"""Return ZipInfo map for MinUI.zip members (from a full release or a raw MinUI.zip)."""
	with zipfile.ZipFile(path, "r") as z:
		names = set(z.namelist())
		if "MinUI.zip" in names:
			data = z.read("MinUI.zip")
			tmp = path.with_suffix(path.suffix + ".minui.tmp")
			tmp.write_bytes(data)
			try:
				with zipfile.ZipFile(tmp, "r") as m:
					return {i.filename: i for i in m.infolist() if not i.is_dir()}
			finally:
				tmp.unlink(missing_ok=True)
		return {i.filename: i for i in z.infolist() if not i.is_dir()}


def extract_minui_bytes(path: Path) -> bytes:
	with zipfile.ZipFile(path, "r") as z:
		if "MinUI.zip" in z.namelist():
			return z.read("MinUI.zip")
		# path is already a MinUI.zip
		return path.read_bytes()


def changed_names(prev: dict[str, zipfile.ZipInfo], new: dict[str, zipfile.ZipInfo]) -> list[str]:
	out: list[str] = []
	for name, info in new.items():
		old = prev.get(name)
		if old is None or old.file_size != info.file_size or old.CRC != info.CRC:
			out.append(name)
	# Always ship version markers when present so About / updater see the new tag
	for marker in (".system/version.txt", ".system/commits.txt"):
		if marker in new and marker not in out:
			out.append(marker)
	return sorted(out)


def main() -> int:
	if len(sys.argv) != 4:
		print(__doc__.strip(), file=sys.stderr)
		return 2

	prev_path = Path(sys.argv[1])
	new_path = Path(sys.argv[2])
	out_path = Path(sys.argv[3])

	if not prev_path.is_file() or not new_path.is_file():
		print("make-ota-zip: missing input zip", file=sys.stderr)
		return 1

	prev_map = load_minui_members(prev_path)
	new_map = load_minui_members(new_path)
	names = changed_names(prev_map, new_map)
	if not names:
		print("make-ota-zip: no changed MinUI members; refusing empty OTA", file=sys.stderr)
		return 1

	new_minui = extract_minui_bytes(new_path)
	tmp_minui = out_path.with_suffix(".minui-work.zip")
	out_path.parent.mkdir(parents=True, exist_ok=True)

	with zipfile.ZipFile(io.BytesIO(new_minui), "r") as src:
		with zipfile.ZipFile(tmp_minui, "w", compression=zipfile.ZIP_DEFLATED) as dst:
			for name in names:
				dst.writestr(name, src.read(name))

	with zipfile.ZipFile(out_path, "w", compression=zipfile.ZIP_DEFLATED) as outer:
		outer.write(tmp_minui, arcname="MinUI.zip")
	tmp_minui.unlink(missing_ok=True)

	ota_size = out_path.stat().st_size
	print(f"make-ota-zip: {len(names)} files -> {out_path} ({ota_size} bytes)")
	for name in names[:40]:
		print(f"  {name}")
	if len(names) > 40:
		print(f"  ... +{len(names) - 40} more")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
