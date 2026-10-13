#include "tmap/administrative_path.h"
#include <cassert>
#include <cstring>

int main()
{
    char path[513] = "中国 / 浙江 / 杭州 / 西湖";
    tmap::commonAdministrativePath(path, "中国 / 浙江 / 杭州 / 临安");
    assert(std::strcmp(path, "中国 / 浙江 / 杭州") == 0);
    tmap::commonAdministrativePath(path, "中国 / 浙江");
    assert(std::strcmp(path, "中国 / 浙江") == 0);
    tmap::commonAdministrativePath(path, "中国 / 浙江 / 宁波");
    assert(std::strcmp(path, "中国 / 浙江") == 0);
    tmap::commonAdministrativePath(path, "中国 / 江西");
    assert(std::strcmp(path, "中国") == 0);
    tmap::commonAdministrativePath(path, "中国香港");
    assert(path[0] == 0);
    std::strcpy(path, "CHN / Zhejiang / Hangzhou");
    tmap::commonAdministrativePath(path, "CHN/Zhejiang/Hangzhou");
    assert(std::strcmp(path, "CHN / Zhejiang / Hangzhou") == 0);
    tmap::commonAdministrativePath(path, "");
    assert(path[0] == 0);
}
