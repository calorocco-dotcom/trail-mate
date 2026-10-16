"""Generate country + first-level administration for the existing directory.

Natural Earth sources are pinned and hash-checked. Shapely is build-time only;
the output uses the existing RegionCatalog format and device protocol.
"""
import argparse
import hashlib
import json
from pathlib import Path

from shapely import make_valid
from shapely.geometry import mapping, shape

from tools.tracks.build_country_regions import build_country_regions, country_geometry
from tools.tracks.region_catalog import RegionCatalog

ROOT = Path(__file__).resolve().parents[2]
REVISION = "ca96624a56bd078437bca8184e78163e5039ad19"
SOURCE_BASE = f"https://raw.githubusercontent.com/nvkelso/natural-earth-vector/{REVISION}/geojson/"
SOURCES = {
    "countries": ("ne_10m_admin_0_countries.geojson", "239eec57ac17f100a11e2536cffc56752c318b50ae765b0918ff7aab4ce8f255"),
    "administration": ("ne_10m_admin_1_states_provinces.geojson", "22d0e3ad85eb3e27f17cabf8ba2d50e554fbc27a87796ff891d958185da62fb5"),
}
NAME_FIELDS = {"en": "name_en", "zh-Hans": "name_zh", "zh-Hant-TW": "name_zht",
               "ja": "name_ja", "ko": "name_ko", "ar": "name_ar", "ru": "name_ru",
               "de": "name_de", "es": "name_es", "fr": "name_fr", "it": "name_it"}
SIMPLIFICATION_DEGREES = 0.025


def rounded_coordinates(value):
    return [rounded_coordinates(item) for item in value] if isinstance(value, (tuple, list)) else round(value, 5)


def normalized_geometry(value):
    geometry = shape(value)
    if not geometry.is_valid:
        geometry = make_valid(geometry)
    if geometry.geom_type == "GeometryCollection":
        from shapely.geometry import MultiPolygon
        geometry = MultiPolygon([part for part in geometry.geoms if part.geom_type == "Polygon"])
    # Generalized search geometry, approximately 2.8 km at the equator. Preserve
    # topology and small regions; do not replace polygons with bounding boxes.
    geometry = geometry.simplify(SIMPLIFICATION_DEGREES, preserve_topology=True)
    boundary = mapping(geometry)
    return country_geometry({"type": boundary["type"], "coordinates": rounded_coordinates(boundary["coordinates"])})


def admin_names(properties, fallback):
    names = {locale: properties.get(field) or (properties.get("name_zh") if locale == "zh-Hant-TW" else None) or fallback
             for locale, field in NAME_FIELDS.items()}
    # Clients already fall back to the node name. Do not repeat that same text
    # in eleven language entries for every small administrative region.
    return {locale: name for locale, name in names.items() if name != fallback}


def build_world_regions(country_catalog, countries, administration):
    result = json.loads(json.dumps(country_catalog))
    features = result["features"]
    country_ids = {feature["id"] for feature in features}
    required = {feature["properties"]["adm0_a3"] for feature in administration["features"]}
    missing = required - country_ids
    extra = [{"type": "Feature", "id": feature["properties"]["ADM0_A3"],
              "properties": {"name": feature["properties"]["NAME_EN"], "iso2": feature["properties"]["ISO_A2_EH"]},
              "geometry": normalized_geometry(feature["geometry"])}
             for feature in countries["features"] if feature["properties"]["ADM0_A3"] in missing]
    if {feature["id"] for feature in extra} != missing:
        raise ValueError("administrative parent missing from pinned country source")
    if extra:
        features.extend(build_country_regions({"type": "FeatureCollection", "source": SOURCE_BASE + SOURCES["countries"][0],
                                              "sourceSha256": SOURCES["countries"][1], "features": extra})["features"])
    for feature in administration["features"]:
        properties = feature["properties"]
        name = properties.get("name_en") or properties.get("name") or "Unnamed area"
        identifier = properties["adm1_code"].replace("+", "_2b").replace("?", "_3f")
        features.append({"type": "Feature", "id": identifier,
                         "properties": {"parent": properties["adm0_a3"], "name": name,
                                        "names": admin_names(properties, name)},
                         "geometry": normalized_geometry(feature["geometry"])})
    features.sort(key=lambda feature: feature["id"])
    result["administrativeSources"] = [{"url": SOURCE_BASE + filename, "sha256": digest} for filename, digest in SOURCES.values()]
    result["simplificationDegrees"] = SIMPLIFICATION_DEGREES
    catalog = RegionCatalog("world", result)
    if catalog.capabilities()[4] != 1:
        raise ValueError("catalog must contain only country and first-level administration")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--countries-source", required=True, type=Path)
    parser.add_argument("--admin-source", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    inputs = {}
    for kind, path in (("countries", args.countries_source), ("administration", args.admin_source)):
        raw = path.read_bytes()
        if hashlib.sha256(raw).hexdigest() != SOURCES[kind][1]:
            raise ValueError(f"{kind} source hash mismatch")
        inputs[kind] = json.loads(raw)
    country_catalog = json.loads((ROOT / "tools/tracks/data/country-regions.json").read_text(encoding="utf-8"))
    data = build_world_regions(country_catalog, inputs["countries"], inputs["administration"])
    raw = json.dumps(data, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    if len(raw) > 8388608:
        raise ValueError(f"generated catalog exceeds existing directory file capacity: {len(raw)} bytes")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(raw)
    RegionCatalog.from_file("world", args.output)
    print(json.dumps({"features": len(data["features"]), "administration": len(inputs["administration"]["features"]),
                      "levels": 2, "bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()}))


if __name__ == "__main__":
    main()
