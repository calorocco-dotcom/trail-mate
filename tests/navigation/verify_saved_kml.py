"""Check that the saved route preserves endpoints and computed road geometry."""
import sys
from xml.etree import ElementTree as ET

ns = {"k": "http://www.opengis.net/kml/2.2"}
document = ET.parse(sys.argv[1]).getroot().find("k:Document", ns)
assert document.findtext("k:name", namespaces=ns) == 'Origin & <gate> - Destination "ridge"'
route = document.find("k:Placemark/k:LineString/k:coordinates", ns)
points = [tuple(map(float, value.split(","))) for value in route.text.split()]
assert points[0] == (0, 0) and points[-1] == (0.002, 0)
assert (0.001, 0.001) in points, "The saved navigation lost its road detour"
endpoints = {}
for marker in document.findall("k:Placemark", ns):
    role = marker.findtext("k:ExtendedData/k:Data/k:value", namespaces=ns)
    if role:
        endpoints[role] = (
            marker.findtext("k:name", namespaces=ns),
            marker.findtext("k:Point/k:coordinates", namespaces=ns),
        )
assert endpoints == {
    "origin": ("Origin & <gate>", "0.0000000,0.0000000"),
    "destination": ('Destination "ridge"', "0.0020000,0.0000000"),
}
