#include "ui/menu/menu_profile.h"
#include "ui/ui_theme.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <vector>

namespace ui::i18n
{
void set_label_text(lv_obj_t* label, const char* text) { lv_label_set_text(label, text); }
} // namespace ui::i18n
namespace menu
{
class AppScreen
{
  public:
    const lv_image_dsc_t* source;
    const char* name() { return "Map"; }
    const lv_image_dsc_t* icon() { return source; }
};
struct MenuAppUi
{
    AppScreen* app;
    const char* name;
    lv_obj_t* icon;
    lv_obj_t* button;
    lv_obj_t* label;
};
constexpr size_t kMaxMenuApps = 18;
MenuAppUi s_menu_apps[kMaxMenuApps]{};
#define MENU_LAYOUT_DIAG(...) ((void)0)
// Actual card, icon and label creation. Only catalog/callback wiring is omitted.
#include "menu_icon_actual.inc"
} // namespace menu

int main()
{
    lv_init();
    auto* display = lv_display_create(320, 240);
    std::vector<uint16_t> pixels(320 * 240);
    lv_display_set_buffers(display, pixels.data(), nullptr, pixels.size() * 2, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, [](lv_display_t* value, const lv_area_t*, uint8_t*)
                            { lv_display_flush_ready(value); });
    auto* group = lv_group_create();
    lv_group_set_default(group);
    for (unsigned side : {48U, 64U, 80U})
    {
        lv_obj_clean(lv_screen_active());
        std::vector<uint16_t> data(side * side, 0x07e0);
        for (unsigned y = 0; y < side; ++y)
            for (unsigned x = 0; x < side; ++x)
                if (y < 6) data[y * side + x] = 0xf800;
                else if (y >= side - 6) data[y * side + x] = 0x001f;
        lv_image_dsc_t source{};
        source.header.magic = LV_IMAGE_HEADER_MAGIC;
        source.header.cf = LV_COLOR_FORMAT_RGB565;
        source.header.w = source.header.h = side;
        source.header.stride = side * 2;
        source.data_size = data.size() * 2;
        source.data = reinterpret_cast<uint8_t*>(data.data());
        auto* parent = lv_obj_create(lv_screen_active());
        lv_obj_set_size(parent, 320, 100);
        lv_obj_set_pos(parent, 0, 30);
        lv_obj_set_style_bg_opa(parent, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_all(parent, 0, 0);
        lv_obj_set_style_border_width(parent, 0, 0);
        lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
        menu::AppScreen app{&source};
        menu::createAppButton(parent, &app, 0);
        auto* card = menu::s_menu_apps[0].button;
        auto* icon = menu::s_menu_apps[0].icon;
        auto* reference = lv_image_create(lv_screen_active());
        lv_image_set_src(reference, &source);
        lv_obj_set_pos(reference, 180, 40);
        // Independently draw the complete source at the icon's chosen scale,
        // with its natural object dimensions and no clipping parent.
        lv_image_set_pivot(reference, 0, 0);
        const auto count = [&pixels](unsigned x1, unsigned x2)
        {
            std::array<unsigned, 3> result{};
            for (unsigned y = 0; y < 170; ++y)
                for (unsigned x = x1; x < x2; ++x)
                {
                    const auto color = pixels[y * 320 + x];
                    if (color == 0xf800) ++result[0];
                    if (color == 0x07e0) ++result[1];
                    if (color == 0x001f) ++result[2];
                }
            return result;
        };
        for (const auto state : {LV_STATE_DEFAULT, LV_STATE_FOCUSED, LV_STATE_FOCUS_KEY})
        {
            lv_obj_remove_state(card, static_cast<lv_state_t>(LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY));
            lv_obj_add_state(card, state);
            lv_obj_update_layout(parent);
            lv_image_set_scale(reference, lv_image_get_scale(icon));
            lv_refr_now(display);
            const auto actual = count(0, 90), expected = count(170, 300);
            std::printf("source=%u state=%u icon=%ldx%ld card=%ldx%ld edge/body pixels actual=%u,%u,%u reference=%u,%u,%u\n", side, unsigned(state), long(lv_obj_get_width(icon)), long(lv_obj_get_height(icon)), long(lv_obj_get_width(card)), long(lv_obj_get_height(card)), actual[0], actual[1], actual[2], expected[0], expected[1], expected[2]);
            assert(actual == expected && actual[0] > 0 && actual[2] > 0);
        }
    }
    lv_obj_clean(lv_screen_active());
    lv_group_delete(group);
    lv_display_delete(display);
}
