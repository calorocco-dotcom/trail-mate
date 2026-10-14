"""Versioned operator-supplied administrative hierarchy; directory-side only.

Polygon geometry is retained as integer E7 coordinates. Bounding boxes are a
prefilter, never evidence that a track intersects the administrative region.
"""
import hashlib
import json
import math
import re
import threading
from pathlib import Path
from tools.tracks.protocol import binary, integer, text
from tools.tracks.region_budget import RegionQueryBudget

TURN = 3600000000


def _point(value):
    if (type(value) is not list or len(value) != 2 or any(type(v) not in (int, float) or not math.isfinite(v) for v in value)
            or not -180 <= value[0] <= 180 or not -90 <= value[1] <= 90):
        raise ValueError("region coordinate")
    x = round(value[0] * 10000000)
    return ((x + TURN // 2) % TURN - TURN // 2, round(value[1] * 10000000))


def _ring(values):
    if type(values) is not list or not 4 <= len(values) <= 100000:
        raise ValueError("region ring")
    points = [_point(value) for value in values]
    if points[0] != points[-1]:
        raise ValueError("region ring not closed")
    result = [points[0]]
    for x, y in points[1:]:
        previous = result[-1][0]
        x += TURN * round((previous - x) / TURN)
        if abs(x - previous) >= TURN // 2:
            raise ValueError("ambiguous region longitude edge")
        result.append((x, y))
    if result[-1] != result[0] or max(x for x, _ in result) - min(x for x, _ in result) >= TURN // 2:
        raise ValueError("region ring needs date-line split")
    if not sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(result, result[1:])):
        raise ValueError("region ring has no area")
    return tuple(result)


def _geometry(value):
    if type(value) is not dict or value.get("type") not in ("Polygon", "MultiPolygon"):
        raise ValueError("region polygon required")
    polygons = [value.get("coordinates")] if value["type"] == "Polygon" else value.get("coordinates")
    if type(polygons) is not list or not 1 <= len(polygons) <= 1024:
        raise ValueError("region polygons")
    result, count = [], 0
    for polygon in polygons:
        if type(polygon) is not list or not 1 <= len(polygon) <= 1024:
            raise ValueError("region rings")
        rings = tuple(_ring(ring) for ring in polygon)
        count += sum(len(ring) for ring in rings)
        if count > 200000:
            raise ValueError("region geometry capacity")
        result.append(rings)
    return tuple(result), count


def _bounds(polygons):
    points = [point for polygon in polygons for point in polygon[0]]
    xs = sorted({(x + TURN // 2) % TURN - TURN // 2 for x, _ in points})
    gaps = [(xs[(index + 1) % len(xs)] + (TURN if index + 1 == len(xs) else 0) - x, index) for index, x in enumerate(xs)]
    _, index = max(gaps)
    return [xs[(index + 1) % len(xs)], min(y for _, y in points), xs[index], max(y for _, y in points)]


def _cross(a, b, c):
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])


def _on_segment(a, b, point):
    return not _cross(a, b, point) and min(a[0], b[0]) <= point[0] <= max(a[0], b[0]) and min(a[1], b[1]) <= point[1] <= max(a[1], b[1])


def _ring_location(ring, point, budget, center=None):
    if center is None:
        budget.consume(2 * len(ring))
        center = (min(x for x, _ in ring) + max(x for x, _ in ring)) / 2
    x, y = point
    x += TURN * round((center - x) / TURN)
    inside = False
    for a, b in zip(ring, ring[1:]):
        budget.consume(1)
        if _on_segment(a, b, (x, y)):
            return 0
        if (a[1] > y) != (b[1] > y):
            crossing = _cross(a, b, (x, y))
            if (crossing > 0) == (b[1] > a[1]):
                inside = not inside
    return 1 if inside else -1


def _polygon_contains(polygon, point, budget, centers=None):
    outer = _ring_location(polygon[0], point, budget, centers[0] if centers else None)
    if outer == -1:
        return False
    if outer == 0:
        return True
    for index, hole in enumerate(polygon[1:], 1):
        location = _ring_location(hole, point, budget, centers[index] if centers else None)
        if location == 0:
            return True
        if location == 1:
            return False
    return True


def _segments_intersect(a, b, c, d):
    crosses = (_cross(a, b, c), _cross(a, b, d), _cross(c, d, a), _cross(c, d, b))
    if crosses[0] * crosses[1] < 0 and crosses[2] * crosses[3] < 0:
        return True
    return (_on_segment(a, b, c) or _on_segment(a, b, d) or _on_segment(c, d, a) or _on_segment(c, d, b))


def _track_path(values, closed=False, *, budget):
    if not isinstance(values, (list, tuple)) or not 1 <= len(values) <= 100000:
        raise ValueError("track region path")
    budget.consume(len(values))
    result = []
    for point in values:
        if (not isinstance(point, (list, tuple)) or len(point) != 2 or not integer(point[0], -1800000000, 1799999999)
                or not integer(point[1], -900000000, 900000000)):
            raise ValueError("track region point")
        x, y = point
        if result:
            x += TURN * round((result[-1][0] - x) / TURN)
            if abs(x - result[-1][0]) >= TURN // 2:
                raise ValueError("ambiguous track longitude edge")
        result.append((x, y))
    if closed and (len(result) < 4 or result[0] != result[-1]):
        raise ValueError("track region polygon closure")
    return tuple(result)


def _geometry_intersects(polygons, kind, coordinates, budget):
    if kind == "Point":
        paths = [_track_path([coordinates], budget=budget)]
    elif kind == "LineString":
        paths = [_track_path(coordinates, budget=budget)]
        if len(paths[0]) < 2:
            raise ValueError("track region line")
    elif kind == "Polygon":
        if not isinstance(coordinates, (list, tuple)) or not 1 <= len(coordinates) <= 1024:
            raise ValueError("track region polygon")
        paths = [_track_path(ring, True, budget=budget) for ring in coordinates]
        if sum(map(len, paths)) > 100000:
            raise ValueError("track region geometry capacity")
    else:
        raise ValueError("track region geometry type")
    for polygon in polygons:
        # Scan each ring for its longitude frame once, not once per track
        # segment or vertex. Keep this preparation local to the directory.
        centers = []
        for ring in polygon:
            budget.consume(2 * len(ring))
            centers.append((min(x for x, _ in ring) + max(x for x, _ in ring)) / 2)
        if any(_polygon_contains(polygon, point, budget, centers) for path in paths for point in path):
            return True
        for path in paths:
            for a, b in zip(path, path[1:]):
                for ring, center in zip(polygon, centers):
                    shift = TURN * round((center - (a[0] + b[0]) / 2) / TURN)
                    left, right = (a[0] + shift, a[1]), (b[0] + shift, b[1])
                    for c, d in zip(ring, ring[1:]):
                        budget.consume(1)
                        if _segments_intersect(left, right, c, d):
                            return True
        # An enclosing KML polygon can intersect a region without crossing its
        # boundary or placing a KML vertex inside. Its holes remain excluded.
        if kind == "Polygon" and _polygon_contains(paths, polygon[0][0], budget):
            return True
    return False


class RegionCatalog:
    def __init__(self, namespace, collection):
        if not isinstance(namespace, str) or not re.fullmatch(r"[a-z][a-z0-9.-]{0,23}", namespace):
            raise ValueError("region namespace")
        if (type(collection) is not dict or collection.get("type") != "FeatureCollection"
                or type(collection.get("features")) is not list or not 1 <= len(collection["features"]) <= 10000):
            raise ValueError("region feature collection")
        nodes, points = {}, 0
        for feature in collection["features"]:
            if type(feature) is not dict or feature.get("type") != "Feature":
                raise ValueError("region feature")
            raw_id, properties = feature.get("id"), feature.get("properties")
            if not isinstance(raw_id, str) or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,63}", raw_id) or type(properties) is not dict:
                raise ValueError("region ID/properties")
            identifier = namespace + ":" + raw_id
            if identifier in nodes or not text(properties.get("name"), 128):
                raise ValueError("region duplicate/name")
            parent = properties.get("parent")
            if parent is not None and (not isinstance(parent, str) or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,63}", parent)):
                raise ValueError("region parent")
            names = properties.get("names", {})
            if (type(names) is not dict or len(names) > 32 or any(not isinstance(locale, str) or not re.fullmatch(r"[A-Za-z]{2,3}(?:-[A-Za-z0-9]{2,8}){0,2}", locale) or not text(name, 128) for locale, name in names.items())):
                raise ValueError("region translated names")
            geometry, count = _geometry(feature.get("geometry"))
            points += count
            if points > 1000000:
                raise ValueError("region catalog geometry capacity")
            nodes[identifier] = {"id": identifier, "parent": namespace + ":" + parent if parent else None,
                                 "name": properties["name"], "names": tuple(sorted(names.items())), "geometry": geometry,
                                 "bounds": tuple(_bounds(geometry))}
        children = {None: []}
        for identifier, node in nodes.items():
            seen, ancestor = {identifier}, node["parent"]
            while ancestor is not None:
                if ancestor not in nodes or ancestor in seen:
                    raise ValueError("region parent missing/cycle")
                seen.add(ancestor)
                ancestor = nodes[ancestor]["parent"]
                if len(seen) > 9:
                    raise ValueError("region hierarchy depth")
            node["level"] = len(seen) - 1
            children.setdefault(node["parent"], []).append(identifier)
            children.setdefault(identifier, [])
        self._nodes = nodes
        self._children = {parent: tuple(sorted(ids)) for parent, ids in children.items()}
        self.namespace = namespace
        self._boundary_cache = None
        self._boundary_lock = threading.RLock()
        canonical = [[node["id"], node["parent"], node["name"], node["names"], node["geometry"]] for _, node in sorted(nodes.items())]
        self.version = hashlib.sha256(json.dumps(canonical, ensure_ascii=False, separators=(",", ":")).encode("utf-8")).digest()

    @classmethod
    def from_file(cls, namespace, filename):
        with Path(filename).open("rb") as source:
            raw = source.read(8388609)
        if len(raw) > 8388608:
            raise ValueError("region file capacity")
        return cls(namespace, json.loads(raw))

    def get(self, identifier, version):
        if not binary(version, 32) or version != self.version:
            raise ValueError("region boundary version mismatch")
        if type(identifier) is not str or identifier not in self._nodes:
            raise ValueError("region missing")
        node = self._nodes[identifier]
        return [node["id"], node["parent"], node["level"], node["name"], [list(pair) for pair in node["names"]], list(node["bounds"]), len(self._children[identifier])]

    def children(self, parent=None, *, version=None, after=None, limit=16):
        if not binary(version, 32) or version != self.version:
            raise ValueError("region boundary version mismatch")
        if (parent is not None and (type(parent) is not str or parent not in self._nodes)
                or not integer(limit, 1, 32) or after is not None and (type(after) is not str or after not in self._children.get(parent, ()))):
            raise ValueError("region page")
        ids = [identifier for identifier in self._children[parent] if after is None or identifier > after]
        page = ids[:limit]
        return [self.version, page[-1] if len(ids) > limit else None, [self.get(identifier, self.version) for identifier in page]]

    def intersects_geometry(self, identifier, version, kind, coordinates, *, budget=None):
        """Exact E7 boundary intersection; caller has verified KML/file integrity.

        Geometry coordinates are E7 longitude/latitude, excluding altitude.
        Polygon includes its fill and holes; contact with a boundary counts.
        """
        self.get(identifier, version)
        return _geometry_intersects(self._nodes[identifier]["geometry"], kind, coordinates, budget if budget is not None else RegionQueryBudget())

    def capabilities(self):
        return [1, "trailmate.track.regions.v1", self.namespace, self.version,
                max(node["level"] for node in self._nodes.values()), 32, 1024, [9,10,11,12,13]]

    def boundary(self, identifier, version):
        node = self.get(identifier, version)
        # Separately transferred integer JSON; it is not an oversized CMP1
        # control array. Hash the exact UTF-8 bytes supplied to the client.
        with self._boundary_lock:
            if self._boundary_cache is None or self._boundary_cache[0] != identifier:
                self._boundary_cache = None
                raw = json.dumps([1,self.version.hex(),identifier,self._nodes[identifier]["geometry"]],
                                 separators=(",", ":")).encode("utf-8")
                if not 1 <= len(raw) <= 8388608:
                    raise ValueError("region boundary capacity")
                self._boundary_cache = (identifier,hashlib.sha256(raw).digest(),raw)
            _, digest, raw = self._boundary_cache
            return node, digest, raw

    def boundary_chunk(self, identifier, version, offset, length):
        if not integer(offset,0,8388607) or not integer(length,1,1024):
            raise ValueError("region boundary range")
        _, digest, raw = self.boundary(identifier,version)
        if offset >= len(raw) or length > len(raw)-offset:
            raise ValueError("region boundary range")
        return [digest,len(raw),offset,raw[offset:offset+length]]
