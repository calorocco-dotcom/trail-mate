"""Bounded streaming geometry extraction from hash-bound KML 2.2 bytes.

This is a spatial extractor, not a replacement for full KML profile validation.
It never fetches NetworkLink, Model, icons, or external entities.
"""
import hashlib
import math
import re
from decimal import Decimal, DecimalException, localcontext, ROUND_FLOOR
from xml.parsers import expat
from tools.tracks.protocol import binary, integer

NS = "http://www.opengis.net/kml/2.2}"
GX = "http://www.google.com/kml/ext/2.2}"
NUMBER = re.compile(r"[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?\Z", re.ASCII)


def _coordinate(token, gx=False):
    parts = token.strip().split() if gx else token.split(",")
    if not 2 <= len(parts) <= 3 or any(not 1 <= len(part) <= 64 or not NUMBER.fullmatch(part) for part in parts):
        raise ValueError("KML coordinate syntax")
    try:
        values = tuple(Decimal(part) for part in parts)
    except DecimalException as error:
        raise ValueError("KML coordinate decimal") from error
    if any(not value.is_finite() or not math.isfinite(float(value)) for value in values) or abs(values[0]) > 180 or abs(values[1]) > 90:
        raise ValueError("KML coordinate range")
    with localcontext() as context:
        context.prec = 80
        # Shared manifest rule: half ties round toward positive infinity.
        x, y = (int((value * 10000000 + Decimal("0.5")).to_integral_value(rounding=ROUND_FLOOR)) for value in values[:2])
    return ((x + 1800000000) % 3600000000 - 1800000000, y), values[:2]


class KmlGeometryReader:
    def __init__(self):
        self.stack, self.geometries = [], []
        self.points = self.elements = self.features = 0
        self.parser = expat.ParserCreate("UTF-8", "}")
        self.parser.StartElementHandler = self._start
        self.parser.EndElementHandler = self._end
        self.parser.CharacterDataHandler = self._text
        self.parser.StartDoctypeDeclHandler = self._forbidden
        self.parser.EntityDeclHandler = self._forbidden
        self.parser.ExternalEntityRefHandler = self._forbidden
        self.parser.XmlDeclHandler = self._declaration

    def _forbidden(self, *args):
        raise ValueError("KML DTD/entities unsupported")

    def _declaration(self, version, encoding, standalone):
        if version != "1.0" or encoding is not None and encoding.lower() != "utf-8":
            raise ValueError("KML XML declaration")

    def _start(self, tag, attributes):
        self.elements += 1
        if self.elements > 200000 or len(self.stack) >= 64:
            raise ValueError("KML element/depth capacity")
        parent = self.stack[-1] if self.stack else None
        frame = {"tag": tag, "role": None}
        if parent is None:
            if tag != NS + "kml":
                raise ValueError("KML 2.2 root required")
            frame["role"] = "container"
        elif parent["role"] in ("coordinates", "gxcoord"):
            raise ValueError("nested KML coordinate element")
        elif parent["role"] == "container" and tag in (NS + "Document", NS + "Folder"):
            frame["role"] = "container"
        elif parent["role"] == "container" and tag == NS + "Placemark":
            self.features += 1
            if self.features > 5000:
                raise ValueError("KML feature capacity")
            frame["role"] = "placemark"
        elif parent["role"] in ("placemark", "multi") and tag in (NS + "Point", NS + "LineString", NS + "LinearRing", GX + "Track"):
            frame.update(role="leaf", points=[], first=None, last=None, seen=False)
        elif parent["role"] in ("placemark", "multi") and tag == NS + "Polygon":
            frame.update(role="polygon", outer=[], inner=[])
        elif parent["role"] in ("placemark", "multi") and tag in (NS + "MultiGeometry", GX + "MultiTrack"):
            frame["role"] = "multi"
        elif parent["role"] == "polygon" and tag in (NS + "outerBoundaryIs", NS + "innerBoundaryIs"):
            frame.update(role="boundary", rings=[])
        elif parent["role"] == "boundary" and tag == NS + "LinearRing":
            frame.update(role="leaf", points=[], first=None, last=None, seen=False)
        elif parent["role"] == "leaf" and tag == NS + "coordinates" and parent["tag"] != GX + "Track":
            if parent["seen"]:
                raise ValueError("duplicate KML coordinates")
            parent["seen"] = True
            frame.update(role="coordinates", token=[], owner=parent)
        elif parent["role"] == "leaf" and parent["tag"] == GX + "Track" and tag == GX + "coord":
            frame.update(role="gxcoord", token=[], owner=parent)
        elif tag in (NS + "NetworkLink", NS + "Model", NS + "GroundOverlay"):
            raise ValueError("KML external/unsupported spatial geometry")
        self.stack.append(frame)

    def _point(self, frame, gx=False):
        token = "".join(frame["token"])
        frame["token"].clear()
        if not token:
            if gx:
                raise ValueError("empty gx:coord")
            return
        point, original = _coordinate(token, gx)
        owner = frame["owner"]
        self.points += 1
        if self.points > 100000:
            raise ValueError("KML point capacity")
        owner["points"].append(point)
        if owner["first"] is None:
            owner["first"] = original
        owner["last"] = original

    def _text(self, data):
        if not self.stack:
            return
        frame = self.stack[-1]
        if frame["role"] not in ("coordinates", "gxcoord"):
            return
        for char in data:
            if frame["role"] == "coordinates" and char in " \r\n\t":
                self._point(frame)
            else:
                frame["token"].append(char)
                if len(frame["token"]) > 256:
                    raise ValueError("KML coordinate token capacity")

    def _end(self, tag):
        frame = self.stack.pop()
        parent = self.stack[-1] if self.stack else None
        role = frame["role"]
        if role in ("coordinates", "gxcoord"):
            self._point(frame, role == "gxcoord")
        elif role == "leaf":
            points = frame["points"]
            if tag == NS + "Point":
                if len(points) != 1:
                    raise ValueError("KML Point count")
                geometry = ("Point", points[0])
            else:
                if len(points) < 2:
                    raise ValueError("KML line count")
                if tag == NS + "LinearRing" and (len(points) < 4 or frame["first"] != frame["last"]):
                    raise ValueError("KML ring closure")
                geometry = ("LineString", tuple(points))
            if parent and parent["role"] == "boundary":
                parent["rings"].append(geometry[1])
            else:
                self.geometries.append(geometry)
        elif role == "boundary":
            if len(frame["rings"]) != 1:
                raise ValueError("KML polygon boundary")
            parent["outer" if tag == NS + "outerBoundaryIs" else "inner"].append(frame["rings"][0])
        elif role == "polygon":
            if len(frame["outer"]) != 1:
                raise ValueError("KML polygon outer ring")
            self.geometries.append(("Polygon", tuple(frame["outer"] + frame["inner"])))

    def feed(self, chunk, final=False):
        try:
            self.parser.Parse(chunk, final)
        except expat.ExpatError as error:
            raise ValueError("KML XML invalid") from error
        if final and not self.geometries:
            raise ValueError("KML has no supported spatial geometry")


def read_verified_kml_geometry(source, digest, size, *, chunk_bytes=65536):
    if not binary(digest, 32) or not integer(size, 1, 8388608) or not integer(chunk_bytes, 1, 65536):
        raise ValueError("KML file binding")
    reader, hash_state, total = KmlGeometryReader(), hashlib.sha256(), 0
    while True:
        chunk = source.read(min(chunk_bytes, size - total + 1))
        if not chunk:
            break
        total += len(chunk)
        if total > size:
            raise ValueError("KML file size mismatch")
        hash_state.update(chunk)
        reader.feed(chunk)
    reader.feed(b"", True)
    if total != size or hash_state.digest() != digest:
        raise ValueError("KML file hash/size mismatch")
    return tuple(reader.geometries)


def manifest_geometry_bounds(geometries):
    """Match the browser/native constant-memory anchor arc and point count."""
    anchor = west = east = south = north = None
    count = 0
    for kind, coordinates in geometries:
        paths = [[coordinates]] if kind == "Point" else coordinates if kind == "Polygon" else [coordinates]
        for path in paths:
            for x, y in path:
                count += 1
                if anchor is None:
                    anchor = west = east = x
                    south = north = y
                else:
                    delta = x - anchor
                    if delta >= 1800000000:
                        delta -= 3600000000
                    if delta < -1800000000:
                        delta += 3600000000
                    x = anchor + delta
                    west, east, south, north = min(west, x), max(east, x), min(south, y), max(north, y)
    if not count:
        raise ValueError("KML geometry empty")
    wrap = lambda value: (value + 1800000000) % 3600000000 - 1800000000
    return [wrap(west), south, wrap(east), north], count
