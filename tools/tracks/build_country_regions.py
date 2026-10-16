"""Adapt the existing Geocaching country data to the track directory catalog.

No network download or additional administrative levels. Shapely 2.1.2 is a
build-time dependency for splitting the polar boundary; directories consume
the generated JSON and do not need Shapely or Node.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from tools.tracks.region_catalog import RegionCatalog, _geometry

ROOT = Path(__file__).resolve().parents[2]


def country_geometry(value):
    try:
        _geometry(value)
        return value
    except ValueError as error:
        if str(error) != "region ring needs date-line split":
            raise
    from shapely.geometry import box, mapping, shape
    source = shape(value)
    if not source.is_valid:
        raise ValueError("country boundary is invalid")
    polygons = []
    for west in range(-180, 180, 90):
        piece = source.intersection(box(west, -90, west + 90, 90))
        for polygon in getattr(piece, "geoms", (piece,)):
            if polygon.geom_type == "Polygon" and not polygon.is_empty:
                polygons.append(mapping(polygon)["coordinates"])
    result = json.loads(json.dumps({"type": "MultiPolygon", "coordinates": polygons}))
    _geometry(result)
    if abs(shape(result).area - source.area) > 1e-8:
        raise ValueError("country split changed boundary area")
    return result


def build_country_regions(source):
    name_code = """
import {locales} from './site/i18n/registry.js';
let input=''; for await (const chunk of process.stdin) input+=chunk;
const features=JSON.parse(input).features;
const displays=locales.map(({id})=>[id,new Intl.DisplayNames([id],{type:'region'})]);
console.log(JSON.stringify(features.map(({properties:p})=>Object.fromEntries(
 displays.map(([id,display])=>[id,/^[A-Z]{2}$/.test(p.iso2)?display.of(p.iso2):p.name])))));
"""
    translated = subprocess.run(["node", "--input-type=module", "-e", name_code],
                                input=json.dumps(source), text=True, encoding="utf-8",
                                capture_output=True, check=True, cwd=ROOT)
    names = json.loads(translated.stdout)
    features = [{"type": "Feature", "id": feature["id"],
                 "properties": {"name": feature["properties"]["name"], "names": translated_names},
                 "geometry": country_geometry(feature["geometry"])}
                for feature, translated_names in zip(source["features"], names, strict=True)]
    result = {"type": "FeatureCollection", "source": source["source"],
              "sourceSha256": source["sourceSha256"], "features": features}
    RegionCatalog("world", result)
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    source = json.loads((ROOT / "site/geocaching/data/countries.json").read_text(encoding="utf-8"))
    data = build_country_regions(source)
    raw = json.dumps(data, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(raw)
    print(json.dumps({"countries": len(data["features"]), "levels": 1,
                      "bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()}))
