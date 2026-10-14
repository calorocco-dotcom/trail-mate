#include "track_sharing/xml_name.h"
#include <cassert>
int main()
{
    using track_sharing::validXmlName;
    using track_sharing::validXmlQName;
    assert(validXmlQName("中文:属性") && validXmlQName("a_1.2") && validXmlName("a:b", false));
    assert(!validXmlQName(":a") && !validXmlQName("a:") && !validXmlQName("a:b:c") && !validXmlQName("1a"));
    assert(!validXmlName("\xcc\x80"));
    assert(validXmlName("a\xcc\x80"));
    assert(!validXmlName("\xc0\xaf") && !validXmlName("\xed\xa0\x80") && !validXmlName("\xf4\x8f\xbf\xbf"));
    assert(!validXmlName("\xc2\xb7") && validXmlName("a\xc2\xb7"));
}
