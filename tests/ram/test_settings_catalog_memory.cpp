#include "platform/memory/psram_ptr.h"
#include <algorithm>
#include <array>
#include <cassert>
namespace settings::ui
{
struct SettingItem
{
    int* value;
    unsigned id;
};
} // namespace settings::ui
struct CategoryDef
{
    const char* label;
    const settings::ui::SettingItem* items;
    size_t item_count;
};
int values[3]{};
static const settings::ui::SettingItem first[] = {{&values[0], 1}, {&values[1], 2}};
static const settings::ui::SettingItem second[] = {{&values[2], 3}};
CategoryDef kCategories[] = {{"first", first, 2}, {"second", second, 1}};
constexpr size_t kSettingCatalogCount = 3;
#include "settings_catalog.inc"
int main()
{
    assert(live_bytes == 0);
    allocation_fails = true;
    assert(!prepare_setting_catalog() && live_bytes == 0);
    assert(kCategories[0].items == first && kCategories[1].items == second);
    allocation_fails = false;
    assert(prepare_setting_catalog() && live_bytes == sizeof(first) + sizeof(second));
    assert(kCategories[0].items != first && kCategories[1].items != second);
    assert(kCategories[1].items == kCategories[0].items + 2);
    auto* item = const_cast<settings::ui::SettingItem*>(kCategories[0].items);
    item->id = 42;
    *item->value = 7;
    const auto calls = allocations;
    assert(prepare_setting_catalog() && allocations == calls);
    assert(kCategories[0].items == item && item->id == 42 && values[0] == 7);
    assert(first[0].id == 1 && second[0].id == 3); // Immutable templates never rebound.
}
