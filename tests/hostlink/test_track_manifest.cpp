#include "track_sharing/manifest.h"
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>
int main(int argc, char** argv)
{
    const std::string hex = argc == 2 ? argv[1] : "9e01c44001010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101c410020202020202020202020202020202020001c000cf000000012a05f200cf000000012a05f200a174a094000000009400c0c00194c420030303030303030303030303030303030303030303030303030303030303030301d9246170706c69636174696f6e2f766e642e676f6f676c652d65617274682e6b6d6c2b786d6c01";
    if (hex.size() % 2) return 1;
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i < hex.size(); i += 2)
    {
        unsigned value = 0;
        if (std::sscanf(hex.substr(i, 2).c_str(), "%2x", &value) != 1) return 1;
        bytes.push_back(uint8_t(value));
    }
    track_sharing::ManifestView view;
    if (!track_sharing::decodeManifest({bytes.data(), bytes.size()}, view)) return 1;
    if (argc != 2 && (view.created != 5000000000ULL || view.points != 1)) return 1;
    std::cout << view.created << ',' << view.points << '\n';
    return 0;
}
