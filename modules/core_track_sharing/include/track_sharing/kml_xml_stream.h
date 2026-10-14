#pragma once
#include "track_sharing/kml_geometry_events.h"
#include "track_sharing/xml_declaration_guard.h"
#include "track_sharing/xml_name.h"
#include "track_sharing/xml_namespace_scope.h"
#include "yxml.h"
#include <cstring>
namespace track_sharing
{
// Geometry scanner, not yet full publication validation (gx time and XML name
// policy remain separate). Caller lends the XML name stack. Attribute identity
// fingerprints conservatively reject collisions, never miss equal names.
class KmlXmlStream
{
  public:
    KmlXmlStream(void* names, size_t capacity)
    {
        if (!names || capacity < 256) failed_ = true;
        else yxml_init(&xml_, names, capacity);
    }
    bool feed(const uint8_t* bytes, size_t size)
    {
        if (failed_ || finished_ || (!bytes && size) || size > 8388608 - count_) return fail();
        count_ += static_cast<uint32_t>(size);
        for (size_t i = 0; i < size; ++i)
        {
            const auto byte = bytes[i];
            if (!xml_declaration_.feed(byte)) return fail();
            if (!utf8(byte)) return fail();
            std::memmove(declaration_, declaration_ + 1, 8);
            declaration_[8] = byte >= 'a' && byte <= 'z' ? char(byte - 32) : char(byte);
            if (!std::memcmp(declaration_, "<!DOCTYPE", 9) || !std::memcmp(declaration_ + 1, "<!ENTITY", 8)) return fail();
            const auto token = yxml_parse(&xml_, byte);
            if (token < 0) return fail();
            if (token == YXML_OK) continue;
            if (token != YXML_ATTRSTART && token != YXML_ATTRVAL && token != YXML_ATTREND && pending_ && !openElement()) return fail();
            if (token == YXML_ELEMSTART)
            {
                if (!validXmlQName(xml_.elem)) return fail();
                if (++elements_ > 200000 || !namespaces_.enter()) return fail();
                pending_ = xml_.elem;
                attribute_count_ = 0;
            }
            else if (token == YXML_ATTRSTART)
            {
                const std::string_view name = xml_.attr;
                if (!validXmlQName(name)) return fail();
                namespace_attribute_ = name == "xmlns" || name.substr(0, 6) == "xmlns:";
                prefix_ = name == "xmlns" ? std::string_view{} : name.substr(namespace_attribute_ ? 6 : 0);
                uri_size_ = 0;
                if (!namespace_attribute_ && !addAttribute(name)) return fail();
            }
            else if (token == YXML_ATTRVAL && namespace_attribute_)
            {
                const auto length = std::strlen(xml_.data);
                if (length > sizeof(uri_) - uri_size_) return fail();
                std::memcpy(uri_ + uri_size_, xml_.data, length);
                uri_size_ += length;
            }
            else if (token == YXML_ATTREND && namespace_attribute_)
            {
                if (!namespaces_.declare(prefix_, {uri_, uri_size_})) return fail();
                namespace_attribute_ = false;
            }
            else if (token == YXML_PISTART && !validXmlName(xml_.pi, false)) return fail();
            else if (token == YXML_CONTENT)
            {
                if (!geometry_.text(xml_.data, std::strlen(xml_.data))) return fail();
            }
            else if (token == YXML_ELEMEND)
            {
                if (!geometry_.end() || !namespaces_.leave()) return fail();
            }
        }
        return true;
    }
    bool finish(GeometryExtent& extent)
    {
        if (failed_ || finished_ || remaining_ || !xml_declaration_.complete() || yxml_eof(&xml_) < 0 || (pending_ && !openElement()) || !geometry_.finish(extent)) return fail();
        finished_ = true;
        return true;
    }

  private:
    bool fail()
    {
        failed_ = true;
        return false;
    }
    bool openElement()
    {
        for (unsigned i = 0; i < attribute_count_; ++i)
        {
            auto& attribute = attributes_[i];
            if (!namespaces_.attributeNamespace(attribute.prefix, attribute.space)) return false;
            for (unsigned j = 0; j < i; ++j)
                if (attribute.space == attributes_[j].space && attribute.local == attributes_[j].local) return false;
        }
        XmlNamespace space;
        std::string_view local;
        if (!namespaces_.element(pending_, space, local)) return false;
        pending_ = nullptr;
        const auto uri = space == XmlNamespace::Kml ? KmlGeometryEvents::kml : space == XmlNamespace::Gx  ? KmlGeometryEvents::gx
                                                                           : space == XmlNamespace::Other ? std::string_view("urn:trailmate:other")
                                                                                                          : std::string_view{};
        return geometry_.begin(uri, local);
    }
    bool addAttribute(std::string_view name)
    {
        if (attribute_count_ >= 32 || name.empty()) return false;
        const auto colon = name.find(':');
        std::string_view prefix, local = name;
        if (colon != name.npos)
        {
            if (!colon || colon + 1 == name.size() || name.find(':', colon + 1) != name.npos) return false;
            prefix = name.substr(0, colon);
            local = name.substr(colon + 1);
        }
        if (prefix.size() > 31) return false;
        auto& attribute = attributes_[attribute_count_++];
        if (!prefix.empty()) std::memcpy(attribute.prefix, prefix.data(), prefix.size());
        attribute.prefix[prefix.size()] = '\0';
        attribute.local = XmlNamespaceScope::fingerprint(local);
        return true;
    }
    bool utf8(uint8_t byte)
    {
        if (remaining_)
        {
            if ((byte & 0xc0) != 0x80) return false;
            codepoint_ = (codepoint_ << 6) | (byte & 63);
            if (--remaining_) return true;
            if (codepoint_ < minimum_ || codepoint_ > 0x10ffff || (codepoint_ >= 0xd800 && codepoint_ <= 0xdfff) || codepoint_ == 0xfffe || codepoint_ == 0xffff) return false;
            return true;
        }
        if (byte < 128) return byte >= 32 || byte == 9 || byte == 10 || byte == 13;
        if (byte >= 0xc2 && byte <= 0xdf)
        {
            remaining_ = 1;
            codepoint_ = byte & 31;
            minimum_ = 0x80;
        }
        else if (byte >= 0xe0 && byte <= 0xef)
        {
            remaining_ = 2;
            codepoint_ = byte & 15;
            minimum_ = 0x800;
        }
        else if (byte >= 0xf0 && byte <= 0xf4)
        {
            remaining_ = 3;
            codepoint_ = byte & 7;
            minimum_ = 0x10000;
        }
        else return false;
        return true;
    }
    yxml_t xml_{};
    XmlDeclarationGuard xml_declaration_;
    struct Attribute
    {
        char prefix[32]{};
        uint64_t local = 0, space = 0;
    };
    Attribute attributes_[32]{};
    uint8_t attribute_count_ = 0;
    XmlNamespaceScope namespaces_;
    KmlGeometryEvents geometry_;
    const char* pending_ = nullptr;
    std::string_view prefix_;
    char uri_[256]{}, declaration_[9]{};
    size_t uri_size_ = 0;
    uint32_t count_ = 0, elements_ = 0, codepoint_ = 0, minimum_ = 0;
    uint8_t remaining_ = 0;
    bool failed_ = false, finished_ = false, namespace_attribute_ = false;
};
static_assert(sizeof(KmlXmlStream) <= 3584, "XML geometry scanner must fit transient admission");
} // namespace track_sharing
