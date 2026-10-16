#pragma once
namespace platform::esp::idf_common
{
class Sx126xRadio
{
  public:
    static Sx126xRadio& instance()
    {
        static Sx126xRadio radio;
        return radio;
    }
    bool online = true;
    bool isOnline() const { return online; }
};
} // namespace platform::esp::idf_common
