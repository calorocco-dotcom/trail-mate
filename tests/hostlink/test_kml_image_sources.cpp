#include "gps/kml/image_sources.h"
#include <cassert>
#include <cstring>
#include <string>
#include <vector>
namespace navigation
{
#include "gps_image_sources_actual.inc"
} // namespace navigation
namespace preview
{
#include "tracker_image_sources_actual.inc"
} // namespace preview
int main()
{
    const auto check = [](const std::string& input, const std::vector<std::string>& expected)
    {
        std::vector<std::string> a, b;
        navigation::extract_img_srcs(input, a);
        preview::extract_img_srcs(input, b);
        assert(a == expected && b == expected);
    };
    check("<img data-src='wrong' SRC = \"right>a.jpg\" alt='src=wrong'>", {"right>a.jpg"});
    check("<img srcset='wrong' data-src=x aria-src=y><img src='right.jpg'>", {"right.jpg"});
    check("<imgfoo src=x><a data-x='<img src=wrong>'>text</a><IMG src=right.jpg />", {"right.jpg"});
    check("<img src='first' src='second'><img src='' src=wrong><img src src=wrong>", {"first"});
    check("<img alt='x>y' src='a.jpg'><img src=b.jpg/>", {"a.jpg", "b.jpg/"});
    check("<!-- <img src=wrong> --><script>\"<img src=wrong>\"</script><style><img src=wrong></style><textarea><img src=wrong></textarea><img src=right>", {"right"});
    check("<script><img src=wrong></scripture><img src=wrong></SCRIPT><img src=right>", {"right"});
    check("<description><![CDATA[<img src='a&amp;b.jpg'><img src=right>]]></description>", {"a&b.jpg", "right"});
    check("<img src='a&amp;quot;b&#34;c&#38;d.jpg'>", {"a&quot;b\"c&d.jpg"});
    check("<img src='a&#38;amp;b&#x26;quot;c&apos;d.jpg'>", {"a&amp;b&quot;c'd.jpg"});
    check("<img src='&#x5c71;&#128512;&#128;.jpg'>", {"山😀€.jpg"});
    check("<img src='&#0;&#xD800;&#x110000;&#-1;&#xZZ;&unknown;.jpg'>", {"���&#-1;&#xZZ;&unknown;.jpg"});
    assert(gps::kml::decodeImageSource(std::string(100000, '&')) == std::string(100000, '&'));
    check("<img src='unclosed><img src=wrong>", {});
    check("<img src=x", {});
    check("<img broken<attribute src=x>", {});
    check("<!-- unclosed <img src=x>", {});
    check("<img src='资源/山.jpg'>", {"资源/山.jpg"});
    std::string many;
    for (unsigned i = 0; i < 10000; ++i) many += "<img src='" + std::to_string(i) + "'>";
    std::vector<std::string> a, b;
    navigation::extract_img_srcs(many, a);
    preview::extract_img_srcs(many, b);
    assert(a == b && a.size() == 64 && a.front() == "0" && a.back() == "63");
    navigation::extract_img_srcs("<img src=extra>", a);
    assert(a.size() == 64);
    unsigned callbacks = 0;
    gps::kml::forEachImageSource("<img src=a><img src=b>", [&](std::string_view value)
                                 { assert(value == "a"); ++callbacks; return false; });
    assert(callbacks == 1);
}
