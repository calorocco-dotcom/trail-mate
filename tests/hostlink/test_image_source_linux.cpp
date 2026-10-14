#include "platform/linux/runtime_paths.h"
#include "track_sharing/image_source_binding.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
namespace platform::ui::route_storage
{
std::filesystem::path root;
std::filesystem::path route_dir_path() { return root; }
#include "image_source_linux_actual.inc"
} // namespace platform::ui::route_storage
int main()
{
    namespace fs = std::filesystem;
    using namespace platform::ui::route_storage;
    root = fs::current_path() / "image-source-linux-fixture";
    fs::create_directories(root);
    const auto image = root / "test-owned.jpg", binding = root / "test-owned.jpg.source";
    const auto outside = fs::current_path() / "image-source-linux-owned-outside.source";
    uint8_t source[32]{1}, file_hash[32]{2}, record[track_sharing::kImageSourceBindingBytes];
    assert(track_sharing::encodeImageSourceBinding({{source, 32}, {file_hash, 32}, 5}, record, sizeof(record)));
    const auto write = [](const fs::path& path, const uint8_t* bytes, size_t size)
    {std::ofstream output(path,std::ios::binary|std::ios::trunc);assert(output.write(reinterpret_cast<const char*>(bytes),size)); };
    write(image, file_hash, 5);
    write(binding, record, sizeof(record));
    assert(route_image_source_matches(image.string(), source));
    assert(route_image_source_matches("test-owned.jpg", source));
    source[0] ^= 1;
    assert(!route_image_source_matches(image.string(), source));
    source[0] ^= 1;
    assert(!route_image_source_matches("../image-source-linux-owned-outside.source", source));
    write(outside, record, sizeof(record));
    assert(!route_image_source_matches(outside.string(), source));
    write(image, file_hash, 4);
    assert(!route_image_source_matches(image.string(), source));
    write(image, file_hash, 5);
    record[20] ^= 1;
    write(binding, record, sizeof(record));
    assert(!route_image_source_matches(image.string(), source));
    record[20] ^= 1;
    write(binding, record, sizeof(record) - 1);
    assert(!route_image_source_matches(image.string(), source));
    fs::remove(binding);
    assert(!route_image_source_matches(image.string(), source));
    // Symlink coverage is conditional on the host permitting link creation.
    std::error_code error;
    fs::create_symlink(outside, binding, error);
    if (!error)
    {
        assert(!route_image_source_matches(image.string(), source));
        fs::remove(binding);
    }
    fs::remove(image);
    fs::remove(outside);
    fs::remove(root);
}
