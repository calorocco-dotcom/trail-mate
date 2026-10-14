"""Integer E7 viewport intersections, including date-line crossing boxes."""
from tools.tracks.protocol import integer


def validate_box(box):
    if (type(box) is not list or len(box) != 4
            or not integer(box[0], -1800000000, 1799999999)
            or not integer(box[2], -1800000000, 1799999999)
            or not integer(box[1], -900000000, 900000000)
            or not integer(box[3], box[1], 900000000)):
        raise ValueError("query bounds")
    return box


def intersects(a, b):
    validate_box(a)
    validate_box(b)
    if a[1] > b[3] or b[1] > a[3]:
        return False
    def arcs(box):
        return [(box[0], box[2])] if box[0] <= box[2] else [(box[0], 1799999999), (-1800000000, box[2])]
    return any(left <= other_right and other_left <= right for left, right in arcs(a) for other_left, other_right in arcs(b))
