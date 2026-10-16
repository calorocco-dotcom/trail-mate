#pragma once
#include "ui/components/bottom_bar.h"
#include "ui/localization.h"
#include <cstdint>
#include <string>

namespace ui::support
{
// Borrowed controls in the Map navigation dialog. A completed/cancelled job
// updates an already-open dialog, so a stale Cancel label cannot start a new job.
struct NativeNavigationControls
{
    struct View
    {
        bool active = false, ready = false, session = false, replanning = false;
        bool failed = false, arrived = false, automatic = true, storage = true, planning_supported = true;
    };
    lv_obj_t* status = nullptr;
    lv_obj_t* replan = nullptr;
    lv_obj_t* reload = nullptr;
    lv_obj_t* automatic = nullptr;
    lv_obj_t* stop = nullptr;
    lv_obj_t* close = nullptr;
    lv_obj_t* createStop(lv_obj_t* panel, const char* label, lv_event_cb_t callback, void* context, bool large_touch)
    {
        auto* footer = ::ui::components::bottom_bar::create(panel);
        const auto height = large_touch ? 52 : ::ui::components::bottom_bar::kHeight;
        lv_obj_set_height(footer, height);
        stop = ::ui::components::bottom_bar::add_button(footer, label, callback, context);
        lv_obj_set_height(stop, height - 4);
        lv_obj_set_flex_grow(stop, 1);
        return stop;
    }
    void reset()
    {
        status = replan = reload = automatic = stop = close = nullptr;
        valid_ = false;
    }
    void update(const View& view, const char* path)
    {
        if (!status || !lv_obj_is_valid(status)) return;
        const uint32_t flags = view.active | (view.ready << 1) | (view.session << 2) | (view.replanning << 3) |
                               (view.failed << 4) | (view.arrived << 5) | (view.automatic << 6) | (view.storage << 7) | (view.planning_supported << 8);
        uint32_t key = 2166136261U ^ flags;
        for (const auto* p = path; p && *p; ++p) key = (key ^ static_cast<uint8_t>(*p)) * 16777619U;
        if (valid_ && key == key_) return;
        if (valid_ && view.replanning != planning_ && replan && close && lv_obj_has_state(replan, LV_STATE_FOCUSED))
            lv_group_focus_obj(close);
        key_ = key;
        valid_ = true;
        planning_ = view.replanning;
        const std::string full = path ? path : "";
        const auto slash = full.find_last_of("/\\");
        const auto name = full.substr(slash == std::string::npos ? 0 : slash + 1);
        const auto label = std::string(i18n::tr(view.arrived ? "Completed" : view.active ? "Current"
                                                                                         : "Preview")) +
                           ": " + name;
        i18n::set_content_label_text_raw(status, label.c_str());
        text(replan, view.replanning ? "Cancel replan" : view.failed ? "Retry replan"
                                                                     : "Replan from here");
        enabled(replan, view.planning_supported && view.session && view.ready && view.storage && !view.arrived);
        enabled(reload, view.storage);
        text(stop, view.arrived ? "Finish navigation" : view.active ? "Stop navigation"
                                                                    : "Close preview");
        if (automatic)
        {
            if (view.planning_supported && view.session && !view.arrived) lv_obj_clear_flag(automatic, LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(automatic, LV_OBJ_FLAG_HIDDEN);
            if (view.automatic) lv_obj_add_state(automatic, LV_STATE_CHECKED);
            else lv_obj_clear_state(automatic, LV_STATE_CHECKED);
        }
    }

  private:
    static void text(lv_obj_t* button, const char* key)
    {
        if (button) i18n::set_label_text(lv_obj_get_child(button, 0), key);
    }
    static void enabled(lv_obj_t* object, bool value)
    {
        if (!object) return;
        if (value) lv_obj_clear_state(object, LV_STATE_DISABLED);
        else lv_obj_add_state(object, LV_STATE_DISABLED);
    }
    uint32_t key_ = 0;
    bool valid_ = false, planning_ = false;
};
} // namespace ui::support
