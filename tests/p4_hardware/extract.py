"""Compile production functions unchanged against observable hardware boundaries."""
import pathlib
import re
import sys

root, output = map(pathlib.Path, sys.argv[1:])


def extract(source, name):
    match = re.search(r"^[\w:*<> ]+\b" + re.escape(name) + r"\([^;]*?\)\s*\{", source, re.M)
    if not match:
        raise RuntimeError(f"Cannot locate production function {name}")
    start = source.index("{", match.start())
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end] + "\n"


board = (root / "boards/t_display_p4/src/t_display_p4_board.cpp").read_text(encoding="utf-8")
display = (root / "platform/esp/idf_components/t_display_p4/trail_mate_t_display_p4_runtime.cpp").read_text(encoding="utf-8")
gps = (root / "platform/esp/idf_common/src/gps_runtime.cpp").read_text(encoding="utf-8")
power = "".join(extract(board, name) for name in (
    "locate_expander_pin", "TDisplayP4Board::initializeExpander",
    "TDisplayP4Board::expanderPinMode", "TDisplayP4Board::expanderWrite",
    "TDisplayP4Board::expanderWriteActive", "TDisplayP4Board::runColdBootPowerSequence"))
for name in ("kDsiPhyLdoChannel", "kDsiPhyLdoMv"):
    power += re.search(r"^constexpr int " + name + r" = .*;", display, re.M)[0] + "\n"
power += extract(display, "ensure_dsi_phy_power")
power += extract(gps, "should_try_p4_factory_baud")
power += extract(board, "configure_l76k_update_rate")
(output / "power_actual.inc").write_text(power, encoding="utf-8")
rtc = (root / "boards/t_display_p4/src/rtc_runtime.cpp").read_text(encoding="utf-8")
(output / "rtc_actual.inc").write_text("".join(extract(rtc, name) for name in (
    "days_from_civil", "is_leap_year", "days_in_month", "bcd_to_dec",
    "validate_datetime_utc", "datetime_to_epoch_utc", "read_rtc_epoch")), encoding="utf-8")
