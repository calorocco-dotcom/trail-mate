#include "platform/ui/reticulum_group_config_runtime.h"

#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/esp/idf_common/bsp_runtime.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace platform::ui::reticulum_groups
{
namespace
{

constexpr const char* kConfigDir = "/trailmate/reticulum";
constexpr const char* kConfigPath = "/trailmate/reticulum/groups.tsv";
constexpr const char* kConfigTempPath = "/trailmate/reticulum/groups.tmp";
constexpr std::size_t kMaxConfigBytes = 2048;
chat::ReticulumGroupDestinationConfig
    s_pending_groups[chat::kReticulumGroupDestinationMaxCount] = {};
bool s_pending = false;

void copy_text(char* out, std::size_t out_len, const char* text)
{
    if (!out || out_len == 0)
    {
        return;
    }
    std::snprintf(out, out_len, "%s", text ? text : "");
}

void set_status(Status& out, const char* message, const char* detail = nullptr)
{
    copy_text(out.message, sizeof(out.message), message);
    copy_text(out.detail, sizeof(out.detail), detail);
}

std::string logical_path_for(const char* logical_path)
{
    if (!logical_path || logical_path[0] == '\0')
    {
        return "/";
    }
    std::string path = logical_path;
    if (path.size() >= 2 && (path[0] == 'A' || path[0] == 'a') && path[1] == ':')
    {
        path.erase(0, 2);
    }
    if (path.empty())
    {
        return "/";
    }
    if (path.front() != '/')
    {
        path.insert(path.begin(), '/');
    }
    return path;
}

bool path_exists(const char* logical_path)
{
    const std::string path = logical_path_for(logical_path);
    return ::platform::esp::arduino_common::storage::sd_exists(path.c_str());
}

bool is_directory(const char* logical_path)
{
    const std::string path = logical_path_for(logical_path);
    return ::platform::esp::arduino_common::storage::sd_is_directory(path.c_str());
}

bool ensure_config_dir()
{
    return ::platform::esp::arduino_common::storage::sd_mkdir("/trailmate") &&
           (::platform::esp::arduino_common::storage::sd_mkdir(kConfigDir) ||
            is_directory(kConfigDir));
}

bool enabled_text(const std::string& value)
{
    return value == "1" || value == "true" || value == "yes" || value == "enabled";
}

std::string trim_line(std::string line)
{
    while (!line.empty() &&
           (line.back() == '\r' || line.back() == '\n' ||
            line.back() == ' ' || line.back() == '\t'))
    {
        line.pop_back();
    }
    std::size_t start = 0;
    while (start < line.size() && (line[start] == ' ' || line[start] == '\t'))
    {
        ++start;
    }
    return start == 0 ? line : line.substr(start);
}

bool parse_group_line(const std::string& line,
                      chat::ReticulumGroupDestinationConfig& out,
                      char* error,
                      std::size_t error_len)
{
    const std::size_t first_tab = line.find('\t');
    const std::size_t second_tab =
        first_tab == std::string::npos ? std::string::npos : line.find('\t', first_tab + 1);
    if (first_tab == std::string::npos || second_tab == std::string::npos)
    {
        copy_text(error, error_len, "Invalid group config line");
        return false;
    }

    const std::string destination = line.substr(second_tab + 1);

    out = chat::ReticulumGroupDestinationConfig{};
    if (!chat::parseReticulumDestinationHashText(destination.c_str(),
                                                 &out.identity,
                                                 error,
                                                 error_len))
    {
        return false;
    }
    out.enabled = enabled_text(line.substr(0, first_tab));
    std::snprintf(out.name,
                  sizeof(out.name),
                  "%s",
                  line.substr(first_tab + 1, second_tab - first_tab - 1).c_str());
    return true;
}

bool read_config_text(std::string& out)
{
    out.clear();
    const std::string path = logical_path_for(kConfigPath);
    ::platform::esp::arduino_common::storage::SdRuntimeFile file;
    if (!file.open(path.c_str(), "rb"))
    {
        return false;
    }
    const uint64_t size = file.size();
    if (size == 0 || size > kMaxConfigBytes || !file.seek(0))
    {
        file.close();
        return false;
    }
    std::vector<char> buffer(static_cast<std::size_t>(size));
    const int read = file.read(buffer.data(), buffer.size());
    file.close();
    if (read < 0 || static_cast<std::size_t>(read) != buffer.size())
    {
        out.clear();
        return false;
    }
    out.assign(buffer.begin(), buffer.end());
    return true;
}

bool write_text_atomic(const std::string& text)
{
    const std::string temp_path = logical_path_for(kConfigTempPath);
    const std::string final_path = logical_path_for(kConfigPath);
    (void)::platform::esp::arduino_common::storage::sd_remove(temp_path.c_str());

    ::platform::esp::arduino_common::storage::SdRuntimeFile file;
    if (!file.open(temp_path.c_str(), "wb"))
    {
        return false;
    }
    const bool wrote = file.write(text.data(), text.size()) == text.size();
    const bool flushed = file.flush();
    file.close();
    if (!wrote || !flushed)
    {
        (void)::platform::esp::arduino_common::storage::sd_remove(temp_path.c_str());
        return false;
    }
    (void)::platform::esp::arduino_common::storage::sd_remove(final_path.c_str());
    if (!::platform::esp::arduino_common::storage::sd_rename(temp_path.c_str(),
                                                             final_path.c_str()))
    {
        (void)::platform::esp::arduino_common::storage::sd_remove(temp_path.c_str());
        return false;
    }
    return true;
}

} // namespace

const char* config_path()
{
    return kConfigPath;
}

void clear(chat::ReticulumGroupDestinationConfig* groups, std::size_t group_count)
{
    if (!groups)
    {
        return;
    }
    for (std::size_t index = 0; index < group_count; ++index)
    {
        groups[index] = chat::ReticulumGroupDestinationConfig{};
    }
}

Status load(chat::ReticulumGroupDestinationConfig* groups, std::size_t group_count)
{
    Status out{};
    out.supported = ::platform::esp::idf_common::bsp_runtime::sdcard_capable();
    out.sd_present = ::platform::esp::idf_common::bsp_runtime::ensure_sdcard_ready();
    if (!groups || group_count == 0)
    {
        set_status(out, "Group storage unavailable", kConfigPath);
        return out;
    }

    clear(groups, group_count);
    if (!out.supported || !out.sd_present)
    {
        set_status(out, "SD card required", kConfigPath);
        return out;
    }

    out.file_present = path_exists(kConfigPath);
    if (!out.file_present)
    {
        out.loaded = true;
        set_status(out, "No Reticulum groups", kConfigPath);
        return out;
    }

    std::string text;
    if (!read_config_text(text))
    {
        set_status(out, "Cannot read Reticulum groups", kConfigPath);
        return out;
    }

    std::size_t slot = 0;
    std::size_t line_start = 0;
    while (line_start <= text.size() && slot < group_count)
    {
        const std::size_t line_end = text.find('\n', line_start);
        std::string line = trim_line(text.substr(
            line_start,
            line_end == std::string::npos ? std::string::npos : line_end - line_start));
        if (!line.empty() && line[0] != '#' && line.rfind("version\t", 0) != 0)
        {
            char error[96] = {};
            chat::ReticulumGroupDestinationConfig parsed{};
            if (parse_group_line(line, parsed, error, sizeof(error)))
            {
                groups[slot++] = parsed;
            }
            else
            {
                std::printf("[RTGroupConfig] skip invalid line: %s\n", error);
            }
        }
        if (line_end == std::string::npos)
        {
            break;
        }
        line_start = line_end + 1;
    }

    out.loaded = true;
    set_status(out, slot == 0 ? "No Reticulum groups" : "Reticulum groups loaded", kConfigPath);
    return out;
}

Status submit(const chat::ReticulumGroupDestinationConfig* groups,
              std::size_t group_count)
{
    Status out{};
    out.supported = true;
    out.sd_present =
        ::platform::esp::idf_common::bsp_runtime::ensure_sdcard_ready();
    if (!groups || group_count == 0 ||
        group_count > chat::kReticulumGroupDestinationMaxCount)
    {
        set_status(out, "Group storage unavailable", kConfigPath);
        return out;
    }
    if (!out.sd_present)
    {
        set_status(out, "SD card required", kConfigPath);
        return out;
    }

    std::memcpy(s_pending_groups,
                groups,
                group_count * sizeof(chat::ReticulumGroupDestinationConfig));
    for (std::size_t index = group_count;
         index < chat::kReticulumGroupDestinationMaxCount;
         ++index)
    {
        s_pending_groups[index] = chat::ReticulumGroupDestinationConfig{};
    }
    s_pending = true;
    out.queued = true;
    set_status(out, "Reticulum groups save queued", kConfigPath);
    return out;
}

Status flushPending()
{
    if (!s_pending)
    {
        Status out{};
        out.supported = true;
        out.sd_present =
            ::platform::esp::idf_common::bsp_runtime::ensure_sdcard_ready();
        out.saved = true;
        set_status(out, "Reticulum groups idle", kConfigPath);
        return out;
    }

    const Status out = save(s_pending_groups,
                            chat::kReticulumGroupDestinationMaxCount);
    if (out.saved)
    {
        s_pending = false;
    }
    return out;
}

bool discardLegacySource()
{
    using namespace platform::esp::arduino_common::storage;
    if (!sd_card_ready()) return false;
    s_pending = false;
    return (!sd_exists(kConfigTempPath) || sd_remove(kConfigTempPath)) &&
           (!sd_exists(kConfigPath) || sd_remove(kConfigPath));
}

bool hasPending()
{
    return s_pending;
}

Status save(const chat::ReticulumGroupDestinationConfig* groups, std::size_t group_count)
{
    Status out{};
    out.supported = ::platform::esp::idf_common::bsp_runtime::sdcard_capable();
    out.sd_present = ::platform::esp::idf_common::bsp_runtime::ensure_sdcard_ready();
    if (!groups || group_count == 0)
    {
        set_status(out, "Group storage unavailable", kConfigPath);
        return out;
    }
    if (!out.supported || !out.sd_present)
    {
        set_status(out, "SD card required", kConfigPath);
        return out;
    }
    if (!ensure_config_dir())
    {
        set_status(out, "Cannot create group directory", kConfigDir);
        return out;
    }

    std::string text;
    text.reserve(384);
    text += "# Trail Mate Reticulum groups\n";
    text += "version\t1\n";
    for (std::size_t index = 0; index < group_count; ++index)
    {
        const auto& group = groups[index];
        if (!chat::hasReticulumDestinationIdentity(group.identity))
        {
            continue;
        }
        char hash[chat::kReticulumPeerHashSize * 2 + 1] = {};
        chat::formatReticulumDestinationHashText(group.identity, hash, sizeof(hash));
        text += group.enabled ? "1\t" : "0\t";
        text += group.name[0] != '\0' ? group.name : "Group";
        text += "\t";
        text += hash;
        text += "\n";
    }

    if (!write_text_atomic(text))
    {
        set_status(out, "Cannot write Reticulum groups", kConfigPath);
        return out;
    }
    out.file_present = true;
    out.saved = true;
    set_status(out, "Reticulum groups saved", kConfigPath);
    return out;
}

} // namespace platform::ui::reticulum_groups
