"""Prevent release pins/build ownership from drifting between application builds."""
import json
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


class DependencyContract(unittest.TestCase):
    def test_platformio_and_cmake_lock_the_same_release(self):
        cmake = (ROOT / "cmake/TrailMateTmap.cmake").read_text(encoding="utf-8")
        version = re.search(r'set\(TRAILMATE_TMAP_VERSION "([^"]+)"\)', cmake)[1]
        commit = re.search(r'set\(TRAILMATE_TMAP_COMMIT "([a-f0-9]{40})"\)', cmake)[1]
        self.assertRegex(cmake, r'TRAILMATE_TMAP_ARCHIVE_SHA256 "[a-f0-9]{64}"')
        uri = "https://github.com/vicliu624/tmap.git#" + commit
        self.assertIn(uri, (ROOT / "platformio.ini").read_text(encoding="utf-8"))
        manifest = json.loads((ROOT / "modules/ui_map_runtime/library.json").read_text(encoding="utf-8"))
        self.assertEqual(uri, manifest["dependencies"]["tmap"])
        self.assertRegex(version, r"^\d+\.\d+\.\d+$")
        self.assertIn("URL_HASH", cmake)

    def test_old_implementation_is_not_compiled(self):
        self.assertFalse((ROOT / "modules/core_tmap/src/reader.cpp").exists())
        self.assertFalse((ROOT / "modules/core_tmap/src/search.cpp").exists())
        for path in ("builds/esp_idf/ESP_IDF_COMPONENT_SOURCES.cmake", "tests/tmap/CMakeLists.txt",
                     "modules/ui_map_runtime/library.json"):
            self.assertNotIn("modules/core_tmap", (ROOT / path).read_text(encoding="utf-8"))
        idf = (ROOT / "builds/esp_idf/main/CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("target_link_libraries(${COMPONENT_LIB} PUBLIC tmap::tmap)", idf)
        linux = (ROOT / "cmake/TrailMateLinuxSources.cmake").read_text(encoding="utf-8")
        self.assertIn("trailmate_add_tmap()", linux)
        self.assertIn("tmap::tmap", linux)


if __name__ == "__main__":
    unittest.main()
