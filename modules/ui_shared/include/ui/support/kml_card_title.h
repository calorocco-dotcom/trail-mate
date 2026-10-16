#pragma once

#include "yxml.h"
#include <cstdint>
#include <cstring>
#include <string>

namespace ui::support
{
// Selected-file display metadata only. This does not rename or validate assets.
// The owner allocates this bounded workspace in PSRAM, never on an ESP stack.
class KmlCardTitle
{
  public:
    KmlCardTitle() { yxml_init(&xml_, names_, sizeof(names_)); }
    void feed(uint8_t byte)
    {
        if (failed_) return;
        const auto event = yxml_parse(&xml_, byte);
        if (event < 0)
        {
            failed_ = true;
            return;
        }
        if (event == YXML_ELEMSTART)
        {
            if (depth_ == sizeof(nodes_))
            {
                failed_ = true;
                return;
            }
            const char* local = std::strrchr(xml_.elem, ':');
            local = local ? local + 1 : xml_.elem;
            const auto parent = depth_ ? nodes_[depth_ - 1] : Other;
            const Node node = !std::strcmp(local, "Document") ? Document : !std::strcmp(local, "Placemark")                                         ? Placemark
                                                                       : !std::strcmp(local, "name") && (parent == Document || parent == Placemark) ? Name
                                                                                                                                                    : Other;
            nodes_[depth_++] = node;
            if (node == Name)
            {
                capture_ = parent == Document ? 2 : 1;
                size_ = 0;
                truncated_ = false;
            }
        }
        else if (event == YXML_CONTENT && depth_ && nodes_[depth_ - 1] == Name)
        {
            const auto length = std::strlen(xml_.data);
            if (!truncated_ && size_ + length < sizeof(candidate_))
            {
                std::memcpy(candidate_ + size_, xml_.data, length);
                size_ += length;
            }
            else truncated_ = true;
        }
        else if (event == YXML_ELEMEND && depth_)
        {
            if (nodes_[--depth_] == Name && capture_ > priority_)
            {
                // Do not retain an incomplete final UTF-8 code point.
                size_t end = size_;
                while (end && (static_cast<unsigned char>(candidate_[end - 1]) & 0xC0) == 0x80) --end;
                if (end && static_cast<unsigned char>(candidate_[end - 1]) >= 0xC0)
                {
                    const auto lead = static_cast<unsigned char>(candidate_[end - 1]);
                    const size_t needed = lead < 0xE0 ? 2 : lead < 0xF0 ? 3
                                                                        : 4;
                    if (size_ - end + 1 < needed) size_ = end - 1;
                }
                candidate_[size_] = 0;
                size_t begin = 0;
                while (begin < size_ && static_cast<unsigned char>(candidate_[begin]) <= ' ') ++begin;
                while (size_ > begin && static_cast<unsigned char>(candidate_[size_ - 1]) <= ' ') --size_;
                if (size_ > begin)
                {
                    std::memcpy(title_, candidate_ + begin, size_ - begin);
                    title_[size_ - begin] = 0;
                    priority_ = capture_;
                }
            }
        }
    }
    const char* title() const { return title_; }
    bool hasDocumentTitle() const { return priority_ == 2; }

  private:
    enum Node : uint8_t
    {
        Other,
        Document,
        Placemark,
        Name
    };
    yxml_t xml_{};
    char names_[512]{}, candidate_[193]{}, title_[193]{};
    Node nodes_[32]{};
    size_t depth_ = 0, size_ = 0;
    uint8_t capture_ = 0, priority_ = 0;
    bool failed_ = false, truncated_ = false;
};

inline std::string cardFileTitleFallback(const std::string& filename, const char* route_label)
{
    const auto dot = filename.find_last_of('.');
    const auto stem = filename.substr(0, dot);
    const bool installed = stem.size() == 66 && stem.compare(0, 2, "p-") == 0 &&
                           stem.find_first_not_of("0123456789abcdef", 2) == std::string::npos;
    if (installed) return std::string(route_label) + " " + stem.substr(2, 8);
    return stem.empty() ? route_label : stem;
}
} // namespace ui::support
