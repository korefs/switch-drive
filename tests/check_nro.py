"""Check that the distributable NRO actually contains its UI resources."""
import struct
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
path = Path(sys.argv[1]) if len(sys.argv) > 1 else root / "switch-drive.nro"
blob = path.read_bytes()
assert blob[16:20] == b"NRO0", "Missing NRO header"
asset_start = struct.unpack_from("<I", blob, 24)[0]
assert blob[asset_start:asset_start + 4] == b"ASET", "NRO has no embedded assets"
ranges = struct.unpack_from("<6Q", blob, asset_start + 8)
assets = []
end = 56
for offset, size in zip(ranges[::2], ranges[1::2]):
    assert offset >= end and size > 0, "Missing or overlapping asset"
    assert asset_start + offset + size <= len(blob), "Truncated asset"
    assets.append(blob[asset_start + offset:asset_start + offset + size])
    end = offset + size
icon, nacp, romfs = assets
assert icon == (root / "icon.jpg").read_bytes(), "Wrong launcher icon"
assert len(nacp) == 0x4000 and nacp.startswith(b"Switch Drive\0"), "Invalid app metadata"
assert (root / "romfs/icon.bmp").read_bytes() in romfs, "Missing UI logo in RomFS"
print(f"{path.name}: icon, NACP and RomFS verified")
