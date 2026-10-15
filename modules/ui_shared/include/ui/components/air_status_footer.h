#pragma once

#include "lvgl.h"

namespace ui::components::air_status_footer
{

struct Footer
{
    lv_obj_t* container = nullptr;
    lv_obj_t* summary_label = nullptr;
    lv_obj_t* detail_label = nullptr;
};

Footer create(lv_obj_t* parent);
void refresh(Footer& footer);
// Build the same information on demand inside a bottom-menu Info page.
void populate_info(lv_obj_t* parent);

} // namespace ui::components::air_status_footer
