extern "C"
{
#include "yxml.h"
}
#include <cassert>
#include <cstring>
#include <string>
static yxml_t parser;
static char stack[1024];
int main()
{
    const std::string source = "<?xml version=\"1.0\"?><kml xmlns=\"http://www.opengis.net/kml/2.2\"><Document><name>A&amp;B</name><Placemark><Point><coordinates>120,30,10</coordinates></Point></Placemark></Document></kml>";
    for (size_t split = 0; split <= source.size(); ++split)
    {
        yxml_init(&parser, stack, sizeof(stack));
        unsigned starts = 0, ends = 0;
        std::string content;
        for (unsigned chunk = 0; chunk < 2; ++chunk)
        {
            const size_t begin = chunk ? split : 0, end = chunk ? source.size() : split;
            for (size_t i = begin; i < end; ++i)
            {
                const auto result = yxml_parse(&parser, static_cast<unsigned char>(source[i]));
                assert(result >= 0);
                if (result == YXML_ELEMSTART) ++starts;
                if (result == YXML_ELEMEND) ++ends;
                if (result == YXML_CONTENT) content += parser.data;
            }
        }
        assert(yxml_eof(&parser) >= 0 && starts == ends && starts == 6 && content == "A&B120,30,10");
    }
    for (const char* text : {"<a><b></a>", "<a>", "<a>&unknown;</a>"})
    {
        yxml_init(&parser, stack, sizeof(stack));
        bool invalid = false;
        for (size_t i = 0; i < std::strlen(text); ++i)
            if (yxml_parse(&parser, text[i]) < 0)
            {
                invalid = true;
                break;
            }
        assert(invalid || yxml_eof(&parser) < 0);
    }
}
