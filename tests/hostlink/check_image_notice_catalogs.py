from pathlib import Path

root = Path(__file__).resolve().parents[2]
keys = {"Image cache needs checking. Download again.", "Image memory unavailable"}
files = list((root / "packs").rglob("strings.tsv"))
assert len(files) == 13
for file in files:
    rows = [line.split("\t", 1) for line in file.read_text(encoding="utf-8-sig").splitlines()]
    for key in keys:
        matches = [row for row in rows if row and row[0] == key]
        assert len(matches) == 1 and len(matches[0]) == 2 and matches[0][1].strip(), (file, key)
print("Image compatibility and memory notices: 13 device locale catalogs passed")
