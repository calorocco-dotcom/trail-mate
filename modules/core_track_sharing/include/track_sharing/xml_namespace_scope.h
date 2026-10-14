#pragma once
#include <cstdint>
#include <cstring>
#include <string_view>
namespace track_sharing
{
enum class XmlNamespace : uint8_t
{
    None,
    Kml,
    Gx,
    Other
};
// Classifies element namespaces only. Does not identify arbitrary namespaces
// for expanded-name duplicate-attribute checks; the XML wrapper owns that.
class XmlNamespaceScope
{
  public:
    bool enter()
    {
        if (failed_ || depth_ >= 64) return fail();
        ++depth_;
        return true;
    }
    bool leave()
    {
        if (failed_ || !depth_) return fail();
        while (count_ && bindings_[count_ - 1].depth == depth_) --count_;
        --depth_;
        return true;
    }
    bool declare(std::string_view prefix, std::string_view uri)
    {
        constexpr std::string_view xml = "http://www.w3.org/XML/1998/namespace", xmlns = "http://www.w3.org/2000/xmlns/";
        if (failed_ || !depth_ || count_ >= 16 || prefix.size() > 31 || prefix.find('\0') != prefix.npos || prefix.find(':') != prefix.npos || prefix == "xmlns" || uri == xmlns ||
            (prefix == "xml" ? uri != xml : uri == xml) || (!prefix.empty() && uri.empty())) return fail();
        for (size_t i = count_; i > 0 && bindings_[i - 1].depth == depth_; --i)
            if (std::string_view(bindings_[i - 1].prefix) == prefix) return fail();
        auto& binding = bindings_[count_++];
        if (!prefix.empty()) std::memcpy(binding.prefix, prefix.data(), prefix.size());
        binding.prefix[prefix.size()] = '\0';
        binding.depth = depth_;
        binding.identity = uri.empty() ? 0 : fingerprint(uri);
        binding.value = uri.empty() ? XmlNamespace::None : uri == "http://www.opengis.net/kml/2.2"  ? XmlNamespace::Kml
                                                       : uri == "http://www.google.com/kml/ext/2.2" ? XmlNamespace::Gx
                                                                                                    : XmlNamespace::Other;
        return true;
    }
    bool element(std::string_view qname, XmlNamespace& space, std::string_view& local) const
    {
        if (failed_ || !depth_ || qname.empty()) return false;
        const auto colon = qname.find(':');
        std::string_view prefix;
        if (colon != qname.npos)
        {
            if (!colon || colon + 1 == qname.size() || qname.find(':', colon + 1) != qname.npos) return false;
            prefix = qname.substr(0, colon);
            local = qname.substr(colon + 1);
        }
        else local = qname;
        if (prefix == "xmlns") return false;
        if (prefix == "xml")
        {
            space = XmlNamespace::Other;
            return true;
        }
        for (size_t i = count_; i > 0; --i)
            if (std::string_view(bindings_[i - 1].prefix) == prefix)
            {
                space = bindings_[i - 1].value;
                return true;
            }
        space = XmlNamespace::None;
        return prefix.empty();
    }

    static uint64_t fingerprint(std::string_view value)
    {
        uint64_t result = 14695981039346656037ULL;
        for (const unsigned char byte : value)
        {
            result ^= byte;
            result *= 1099511628211ULL;
        }
        return result;
    }
    bool attributeNamespace(std::string_view prefix, uint64_t& identity) const
    {
        if (failed_ || !depth_) return false;
        if (prefix.empty())
        {
            identity = 0;
            return true;
        }
        if (prefix == "xmlns") return false;
        if (prefix == "xml")
        {
            identity = fingerprint("http://www.w3.org/XML/1998/namespace");
            return true;
        }
        for (size_t i = count_; i > 0; --i)
            if (std::string_view(bindings_[i - 1].prefix) == prefix)
            {
                identity = bindings_[i - 1].identity;
                return true;
            }
        return false;
    }

  private:
    bool fail()
    {
        failed_ = true;
        return false;
    }
    struct Binding
    {
        char prefix[32]{};
        uint64_t identity = 0;
        XmlNamespace value = XmlNamespace::None;
        uint8_t depth = 0;
    };
    Binding bindings_[16]{};
    uint8_t count_ = 0, depth_ = 0;
    bool failed_ = false;
};
static_assert(sizeof(XmlNamespaceScope) <= 784, "Namespace scopes must stay bounded");
} // namespace track_sharing
