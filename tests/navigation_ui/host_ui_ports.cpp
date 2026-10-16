#include "ui/assets/fonts/font_utils.h"
#include "ui/page/page_profile.h"
#include "ui/widgets/top_bar_power_presenter.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
namespace platform::ui::navigation
{
void trace(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    std::vprintf(format, args);
    va_end(args);
}
} // namespace platform::ui::navigation
// Host English fonts and power ports. The dialog frame, top bar, bottom bar
// and navigation event handlers themselves are the production implementations.
namespace ui::fonts
{
LocalizedFontBinding* localized_font_binding_storage() { return nullptr; }
std::size_t localized_font_binding_storage_size() { return 0; }
} // namespace ui::fonts
namespace ui::i18n
{
bool ensure_content_font_for_text(const char*) { return true; }
const lv_font_t* active_content_font_fallback() { return nullptr; }
const lv_font_t* active_ui_font_fallback() { return nullptr; }
} // namespace ui::i18n
namespace ui::page_profile
{
const lv_font_t* resolve_body_font() { return &lv_font_montserrat_14; }
const lv_font_t* resolve_title_font() { return &lv_font_montserrat_16; }
} // namespace ui::page_profile
namespace ui::widgets::top_bar_power
{
void bind(TopBar&) { assert(false && "Navigation dialog must not bind a second power presenter"); }
void unbind(TopBar&) {}
} // namespace ui::widgets::top_bar_power
