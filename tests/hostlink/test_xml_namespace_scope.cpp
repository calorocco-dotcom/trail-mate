#include "track_sharing/xml_namespace_scope.h"
#include <cassert>
using namespace track_sharing;
int main()
{
    XmlNamespaceScope scope;
    XmlNamespace space;
    std::string_view local;
    assert(scope.enter() && scope.declare("", "http://www.opengis.net/kml/2.2") && scope.declare("gx", "http://www.google.com/kml/ext/2.2"));
    assert(scope.element("kml", space, local) && space == XmlNamespace::Kml && local == "kml");
    assert(scope.element("gx:Track", space, local) && space == XmlNamespace::Gx);
    assert(!scope.element("unknown:Point", space, local));
    assert(scope.enter() && scope.declare("gx", "urn:other") && scope.declare("", ""));
    assert(scope.element("gx:Track", space, local) && space == XmlNamespace::Other);
    assert(scope.element("Point", space, local) && space == XmlNamespace::None);
    assert(scope.leave() && scope.element("gx:Track", space, local) && space == XmlNamespace::Gx && scope.element("Point", space, local) && space == XmlNamespace::Kml);
    XmlNamespaceScope duplicate;
    assert(duplicate.enter() && duplicate.declare("gx", "urn:first") && !duplicate.declare("gx", "urn:second"));
    XmlNamespaceScope reserved;
    assert(reserved.enter() && !reserved.declare("xml", "urn:fake"));
    XmlNamespaceScope builtin;
    assert(builtin.enter() && builtin.element("xml:lang", space, local) && space == XmlNamespace::Other);
    XmlNamespaceScope budget;
    for (unsigned i = 0; i < 64; ++i) assert(budget.enter());
    assert(!budget.enter());
}
