#include <cassert>
#include <cstring>
#include <fcntl.h>
using oflag_t = int;
#include "sd_open_flags_actual.inc"
int main()
{
    assert(sdfat_open_flags("wx+") == (O_RDWR | O_CREAT | O_EXCL));
    assert(sdfat_open_flags("w+x") == (O_RDWR | O_CREAT | O_EXCL));
    assert(!(sdfat_open_flags("wx+") & O_TRUNC));
    assert(sdfat_open_flags("r") == O_RDONLY);
    assert(sdfat_open_flags("w+") == (O_RDWR | O_CREAT | O_TRUNC));
    assert(sdfat_open_flags("a+") == (O_RDWR | O_CREAT | O_APPEND));
    assert(idf::sdfat_open_flags("wx+") == (O_RDWR | O_CREAT | O_EXCL));
    assert(idf::sdfat_open_flags("w+x") == (O_RDWR | O_CREAT | O_EXCL));
    assert(!(idf::sdfat_open_flags("wx+") & O_TRUNC));
    assert(idf::sdfat_open_flags("r") == O_RDONLY);
    assert(idf::sdfat_open_flags("w+") == (O_RDWR | O_CREAT | O_TRUNC));
    assert(idf::sdfat_open_flags("a+") == (O_RDWR | O_CREAT | O_APPEND));
}
