#include "ui/screens/network/network_page_shell.h"

#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "chat/infra/mesh_protocol_utils.h"
#include "platform/ui/reticulum_directory_runtime.h"
#include "platform/ui/reticulum_network_projection_policy.h"
#include "platform/ui/reticulum_page_runtime.h"
#include "ui/app_runtime.h"
#include "ui/assets/fonts/font_utils.h"
#include "ui/components/bottom_bar.h"
#include "ui/components/floating_search_box.h"
#include "ui/components/shortcut_help_modal.h"
#include "ui/components/two_pane_layout.h"
#include "ui/components/two_pane_styles.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"
#include "ui/runtime/ui_feedback.h"
#include "ui/screens/network/micron_markup_contract.h"
#include "ui/ui_common.h"
#include "ui/widgets/top_bar.h"
#include "ui_lvgl_ux_packs/common/touch_text_editor.h"

#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

#if defined(ESP_PLATFORM)
#include "esp_heap_caps.h"
#endif

#if !defined(LV_FONT_MONTSERRAT_10) || !LV_FONT_MONTSERRAT_10
#define lv_font_montserrat_10 lv_font_montserrat_12
#endif
#if !defined(LV_FONT_MONTSERRAT_12) || !LV_FONT_MONTSERRAT_12
#define lv_font_montserrat_12 lv_font_montserrat_14
#endif
#if !defined(LV_FONT_MONTSERRAT_16) || !LV_FONT_MONTSERRAT_16
#define lv_font_montserrat_16 lv_font_montserrat_14
#endif
#ifndef LV_SYMBOL_SEARCH
#define LV_SYMBOL_SEARCH "S"
#endif
#ifndef LV_SYMBOL_REFRESH
#define LV_SYMBOL_REFRESH "R"
#endif
#ifndef LV_SYMBOL_HOME
#define LV_SYMBOL_HOME "H"
#endif
#ifndef LV_SYMBOL_RIGHT
#define LV_SYMBOL_RIGHT ">"
#endif
#ifndef LV_SYMBOL_LEFT
#define LV_SYMBOL_LEFT "<"
#endif
#ifndef LV_SYMBOL_STAR
#define LV_SYMBOL_STAR "*"
#endif
#ifndef LV_SYMBOL_LIST
#define LV_SYMBOL_LIST "D"
#endif

namespace
{

namespace rtdir = ::platform::ui::reticulum_directory;
namespace rtnet = ::platform::ui::reticulum_network;
namespace rtpage = ::platform::ui::reticulum_page;
namespace micron = ::ui::screens::network::micron;

constexpr std::size_t kMaxVisibleAnnounces = 100;
constexpr std::size_t kMaxVisibleAddresses = 100;
constexpr std::size_t kMaxDirectoryRows = 100;
constexpr std::size_t kInitialPageLinks = 8;
constexpr std::size_t kMaxPageLinks = 32;
constexpr std::size_t kInitialPageAnchors = 8;
constexpr std::size_t kMaxPageAnchors = 32;
constexpr std::size_t kMaxFormFields = 12;
constexpr std::size_t kMaxFormPairs = 15;
constexpr std::size_t kMaxFormPayloadBytes = 256;
constexpr std::size_t kMaxHistoryEntries = 8;
constexpr std::size_t kAddressTextLen = 160;
constexpr std::size_t kSearchTextLen = 32;
constexpr std::size_t kHashTextLen = (rtdir::kReticulumHashSize * 2U) + 1U;
constexpr std::size_t kMicronTextChunkBytes = 112;
constexpr std::size_t kMicronTableMaxCells = micron::kMaxTableCells;
constexpr std::size_t kMicronTableCellTextBytes = 96;
constexpr std::size_t kMicronUnsupportedTagSamples = 3;
constexpr std::size_t kMicronUnsupportedTagSampleBytes = 8;
constexpr uint16_t kMicronMaxRenderedRows = 180;
constexpr uint16_t kMicronMaxRenderedObjects = 520;

void copy_range(char* out, std::size_t out_len, const char* start, std::size_t len);

enum class DirectoryMode : uint8_t
{
    Favourites,
    Announces,
};

enum class DirectoryRowKind : uint8_t
{
    Favourite,
    Announce,
    BuiltInFavourite,
};

enum class MicronAlign : uint8_t
{
    Left,
    Center,
    Right,
};

struct DirectoryRowContext
{
    DirectoryRowKind kind = DirectoryRowKind::Announce;
    std::size_t index = 0;
};

struct LinkContext
{
    char target[kAddressTextLen] = {};
    char request_fields[96] = {};
    lv_obj_t* source = nullptr;
};

enum class FormFieldKind : uint8_t
{
    Text,
    Checkbox,
    Radio,
};

struct FormFieldContext
{
    char name[32] = {};
    char value[64] = {};
    lv_obj_t* object = nullptr;
    FormFieldKind kind = FormFieldKind::Text;
};

struct MicronAnchor
{
    char name[micron::kMaxAnchorNameBytes] = {};
    lv_obj_t* target = nullptr;
    bool heading = false;
};

struct HistoryEntry
{
    char address[kAddressTextLen] = {};
};

struct RemotePageAddress
{
    bool valid = false;
    char destination[kHashTextLen] = {};
    char path[rtpage::kReticulumPagePathSize] = {};
    char anchor[micron::kMaxAnchorNameBytes] = {};
};

struct BuiltInFavourite
{
    const char* address = nullptr;
    const char* title = nullptr;
    const char* meta = nullptr;
    const char* icon = nullptr;
};

struct MicronStyle
{
    uint32_t fg = 0xDDDDDD;
    uint32_t bg = 0x050505;
    bool bold = false;
    bool underline = false;
    bool italic = false;
};

struct MicronRenderState
{
    MicronStyle style{};
    uint32_t default_fg = 0xDDDDDD;
    uint32_t default_bg = 0x050505;
    MicronAlign default_align = MicronAlign::Left;
    MicronAlign align = MicronAlign::Left;
    MicronStyle table_restore_style{};
    MicronAlign table_restore_align = MicronAlign::Left;
    std::array<MicronAlign, kMicronTableMaxCells> table_column_align{};
    uint8_t depth = 0;
    uint8_t table_column_count = 0;
    uint16_t rendered_rows = 0;
    uint16_t rendered_objects = 0;
    uint16_t table_max_width_chars = 0;
    uint16_t unsupported_unknown_tags = 0;
    uint16_t unsupported_inline_blocks = 0;
    uint16_t unsupported_block_count = 0;
    uint16_t unsupported_partial_count = 0;
    uint16_t unsupported_resource_links = 0;
    uint16_t unsupported_unknown_links = 0;
    std::array<std::array<char, kMicronUnsupportedTagSampleBytes>,
               kMicronUnsupportedTagSamples>
        unsupported_tag_samples{};
    MicronAlign table_default_align = MicronAlign::Left;
    uint8_t unsupported_tag_sample_count = 0;
    uint8_t table_row_index = 0;
    bool literal = false;
    bool table_mode = false;
    bool table_first_row = false;
    bool row_limit_reached = false;
    bool object_limit_reached = false;
    bool link_limit_reached = false;
    bool saw_fields = false;
    bool saw_submit_links = false;
};

struct NetworkPageState
{
    ::ui::components::bottom_bar::Menu menu{};
    lv_obj_t* bottom_bar = nullptr;
    lv_obj_t* root = nullptr;
    lv_obj_t* header = nullptr;
    lv_obj_t* content = nullptr;
    lv_obj_t* directory_panel = nullptr;
    lv_obj_t* directory_tabs = nullptr;
    lv_obj_t* favourites_tab = nullptr;
    lv_obj_t* announces_tab = nullptr;
    lv_obj_t* directory_search_btn = nullptr;
    lv_obj_t* directory_list = nullptr;
    lv_obj_t* browser_panel = nullptr;
    lv_obj_t* browser_toolbar = nullptr;
    lv_obj_t* browser_body = nullptr;
    lv_obj_t* browser_rail = nullptr;
    lv_obj_t* browser_back_btn = nullptr;
    lv_obj_t* browser_forward_btn = nullptr;
    lv_obj_t* home_btn = nullptr;
    lv_obj_t* refresh_btn = nullptr;
    lv_obj_t* cancel_btn = nullptr;
    lv_obj_t* rail_directory_btn = nullptr;
    lv_obj_t* rail_search_btn = nullptr;
    lv_obj_t* address_area = nullptr;
    lv_obj_t* go_btn = nullptr;
    lv_obj_t* viewport = nullptr;
    lv_timer_t* page_load_timer = nullptr;
    ::ui::widgets::TopBar top_bar;
    ::ui::components::floating_search_box::State search_box;
    ::ui::components::shortcut_help_modal::State help_modal;
    std::vector<rtdir::AnnounceRecord> announces;
    std::vector<rtdir::LxmfAddressRecord> addresses;
    std::array<DirectoryRowContext, kMaxDirectoryRows> row_contexts{};
    LinkContext* link_contexts = nullptr;
    MicronAnchor* page_anchors = nullptr;
    std::array<HistoryEntry, kMaxHistoryEntries> history{};
    std::array<FormFieldContext, kMaxFormFields> form_fields{};
    std::array<char, rtpage::kReticulumPageBodyMaxBytes + 1U> page_body{};
    std::size_t announce_count = 0;
    std::size_t address_count = 0;
    std::size_t row_context_count = 0;
    std::size_t page_anchor_capacity = 0;
    std::size_t page_anchor_count = 0;
    std::size_t link_context_capacity = 0;
    std::size_t link_context_count = 0;
    std::size_t history_count = 0;
    std::size_t form_field_count = 0;
    std::size_t history_pos = 0;
    DirectoryMode directory_mode = DirectoryMode::Announces;
    bool immersive = false;
    bool directory_collapsed = true;
    bool browser_collapsed = false;
    bool viewport_scroll_locked = false;
    bool suppress_history = false;
    bool cached_page_body_valid = false;
    bool page_cache_load_requested = false;
    rtdir::Status announce_status{};
    rtdir::Status address_status{};
    rtpage::Status cached_page_status{};
    std::size_t cached_page_body_len = 0;
    char cached_page_destination[kHashTextLen] = {};
    char cached_page_path[rtpage::kReticulumPagePathSize] = {};
    char current_address[kAddressTextLen] = "home:/";
    char rendered_shell_address[kAddressTextLen] = {};
    char search_query[kSearchTextLen] = {};
};

// This page has no background owner outside its CallbackAppScreen lifecycle.
// Keeping its large fixed buffers in a file-static object permanently consumes
// internal RAM even when Network is never opened.  It is instead constructed
// in strict PSRAM on enter() and destroyed on exit().
NetworkPageState* s_page_state = nullptr;

#define g_state (*s_page_state)

constexpr uint32_t kAmber = ::ui::components::two_pane_styles::kAccent;
constexpr uint32_t kAmberDark = 0xC98118;
constexpr uint32_t kWarmBg = ::ui::components::two_pane_styles::kSidePanelBg;
constexpr uint32_t kPanelBg = ::ui::components::two_pane_styles::kMainPanelBg;
constexpr uint32_t kLine = ::ui::components::two_pane_styles::kBorder;
constexpr uint32_t kText = ::ui::components::two_pane_styles::kTextPrimary;
constexpr uint32_t kTextDim = ::ui::components::two_pane_styles::kTextMuted;
constexpr uint32_t kTerminalBg = 0x050505;
constexpr uint32_t kTerminalText = 0xE8E0CC;
constexpr uint32_t kTerminalDim = 0x9A917F;
constexpr uint32_t kTerminalGreen = 0x4EA646;
constexpr uint32_t kTerminalAmber = 0xF4B443;
constexpr uint32_t kMicronDefaultFg = 0xDDDDDD;
constexpr uint32_t kMicronHeading1Fg = 0x222222;
constexpr uint32_t kMicronHeading1Bg = 0xBBBBBB;
constexpr uint32_t kMicronHeading2Fg = 0x111111;
constexpr uint32_t kMicronHeading2Bg = 0x999999;
constexpr uint32_t kMicronHeading3Fg = 0x000000;
constexpr uint32_t kMicronHeading3Bg = 0x777777;
// Keep unsupported Micron constructs visible without expanding the renderer
// into a full NomadNet client on constrained devices.
constexpr const char* kMicronUnsupportedInline = "[unsupported inline]";
constexpr const char* kMicronUnsupportedBlock = "[unsupported block]";
constexpr uint32_t kPageLoadPollMs = 500;
constexpr uint32_t kPagerRotateUpKey = 19;
constexpr uint32_t kPagerRotateDownKey = 20;
constexpr lv_coord_t kViewportScrollStep = 28;
constexpr lv_coord_t kViewportPageScrollPadding = 12;
constexpr BuiltInFavourite kBuiltInFavourites[] = {
    {
        "47850a3b99243cfb1147e8856bab2691:/page/index.mu",
        "47850A3B /page",
        "built-in Nomad",
        LV_SYMBOL_STAR,
    },
    {
        "287567d883e0a893ab468c547bc8f09e:/page/index.mu",
        "Trail Mate Showcase",
        "vicliu-pi Nomad",
        LV_SYMBOL_STAR,
    },
};

#ifndef NETWORK_PAGE_TRACE
#define NETWORK_PAGE_TRACE 0
#endif

#if NETWORK_PAGE_TRACE
#define NETWORK_PAGE_LOG(...)                             \
    do                                                    \
    {                                                     \
        std::printf("[UI][Network][Nomad] " __VA_ARGS__); \
    } while (0)
#else
#define NETWORK_PAGE_LOG(...) \
    do                        \
    {                         \
    } while (0)
#endif

const char* safe_tr(const char* text)
{
    return ::ui::i18n::tr(text);
}

void copy_text(char* out, std::size_t out_len, const char* text)
{
    if (!out || out_len == 0)
    {
        return;
    }
    if (!text)
    {
        out[0] = '\0';
        return;
    }
    std::size_t copy_len = std::strlen(text);
    if (copy_len >= out_len)
    {
        copy_len = out_len - 1U;
    }
    std::memmove(out, text, copy_len);
    out[copy_len] = '\0';
}

void* network_page_alloc(std::size_t size)
{
    if (size == 0)
    {
        return nullptr;
    }
#if defined(ESP_PLATFORM)
    return heap_caps_malloc_prefer(size,
                                   2,
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                   MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#else
    return std::malloc(size);
#endif
}

void network_page_free(void* ptr)
{
    if (!ptr)
    {
        return;
    }
#if defined(ESP_PLATFORM)
    heap_caps_free(ptr);
#else
    std::free(ptr);
#endif
}

NetworkPageState* allocate_network_page_state()
{
#if defined(ESP_PLATFORM)
    void* storage = heap_caps_malloc(sizeof(NetworkPageState), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    void* storage = std::malloc(sizeof(NetworkPageState));
#endif
    if (!storage)
    {
        return nullptr;
    }
    return new (storage) NetworkPageState{};
}

bool ensure_network_page_state()
{
    if (s_page_state)
    {
        return true;
    }

    s_page_state = allocate_network_page_state();
    if (!s_page_state)
    {
        std::printf("[UI][Network] enter denied reason=psram_state_alloc bytes=%lu\n",
                    static_cast<unsigned long>(sizeof(NetworkPageState)));
        return false;
    }
    return true;
}

void release_network_page_state()
{
    if (!s_page_state)
    {
        return;
    }

    s_page_state->~NetworkPageState();
    network_page_free(s_page_state);
    s_page_state = nullptr;
}

template <typename T>
T* allocate_object_array(std::size_t count)
{
    void* storage = network_page_alloc(count * sizeof(T));
    if (!storage)
    {
        return nullptr;
    }

    T* objects = static_cast<T*>(storage);
    for (std::size_t i = 0; i < count; ++i)
    {
        new (&objects[i]) T{};
    }
    return objects;
}

template <typename T>
void release_object_array(T* objects, std::size_t count)
{
    if (!objects)
    {
        return;
    }
    for (std::size_t i = 0; i < count; ++i)
    {
        objects[i].~T();
    }
    network_page_free(objects);
}

void release_link_context_pool()
{
    release_object_array(g_state.link_contexts, g_state.link_context_capacity);
    g_state.link_contexts = nullptr;
    g_state.link_context_capacity = 0;
    g_state.link_context_count = 0;
}

void release_micron_anchor_registry()
{
    release_object_array(g_state.page_anchors, g_state.page_anchor_capacity);
    g_state.page_anchors = nullptr;
    g_state.page_anchor_capacity = 0;
    g_state.page_anchor_count = 0;
}

void clear_micron_anchor_registry()
{
    g_state.page_anchor_count = 0;
    for (std::size_t i = 0; i < g_state.page_anchor_capacity; ++i)
    {
        g_state.page_anchors[i] = MicronAnchor{};
    }
}

bool reserve_micron_anchors(std::size_t required)
{
    if (required <= g_state.page_anchor_capacity)
    {
        return true;
    }
    if (required > kMaxPageAnchors)
    {
        return false;
    }

    std::size_t next_capacity =
        g_state.page_anchor_capacity == 0 ? kInitialPageAnchors
                                          : g_state.page_anchor_capacity * 2U;
    if (next_capacity < required)
    {
        next_capacity = required;
    }
    if (next_capacity > kMaxPageAnchors)
    {
        next_capacity = kMaxPageAnchors;
    }

    MicronAnchor* next_anchors = allocate_object_array<MicronAnchor>(next_capacity);
    if (!next_anchors)
    {
        return false;
    }
    if (g_state.page_anchors && g_state.page_anchor_count > 0)
    {
        for (std::size_t i = 0; i < g_state.page_anchor_count; ++i)
        {
            next_anchors[i] = g_state.page_anchors[i];
        }
    }
    release_object_array(g_state.page_anchors, g_state.page_anchor_capacity);
    g_state.page_anchors = next_anchors;
    g_state.page_anchor_capacity = next_capacity;
    return true;
}

bool reserve_link_contexts(std::size_t required)
{
    if (required <= g_state.link_context_capacity)
    {
        return true;
    }
    if (required > kMaxPageLinks)
    {
        return false;
    }

    std::size_t next_capacity =
        g_state.link_context_capacity == 0 ? kInitialPageLinks
                                           : g_state.link_context_capacity * 2U;
    if (next_capacity < required)
    {
        next_capacity = required;
    }
    if (next_capacity > kMaxPageLinks)
    {
        next_capacity = kMaxPageLinks;
    }

    LinkContext* next_contexts = allocate_object_array<LinkContext>(next_capacity);
    if (!next_contexts)
    {
        return false;
    }
    if (g_state.link_contexts && g_state.link_context_count > 0)
    {
        for (std::size_t i = 0; i < g_state.link_context_count; ++i)
        {
            next_contexts[i] = g_state.link_contexts[i];
        }
    }
    release_object_array(g_state.link_contexts, g_state.link_context_capacity);
    g_state.link_contexts = next_contexts;
    g_state.link_context_capacity = next_capacity;
    return true;
}

LinkContext* claim_link_context()
{
    if (g_state.link_context_count >= kMaxPageLinks)
    {
        return nullptr;
    }
    if (!reserve_link_contexts(g_state.link_context_count + 1U))
    {
        return nullptr;
    }
    LinkContext* context = &g_state.link_contexts[g_state.link_context_count++];
    *context = LinkContext{};
    return context;
}

bool micron_anchor_name_exists(const char* name)
{
    if (!name || name[0] == '\0')
    {
        return false;
    }
    for (std::size_t i = 0; i < g_state.page_anchor_count; ++i)
    {
        if (std::strcmp(g_state.page_anchors[i].name, name) == 0)
        {
            return true;
        }
    }
    return false;
}

void register_micron_anchor(const char* name, lv_obj_t* target, bool heading)
{
    if (!name || name[0] == '\0' || !target)
    {
        return;
    }
    if (micron_anchor_name_exists(name))
    {
        return;
    }
    if (g_state.page_anchor_count >= kMaxPageAnchors ||
        !reserve_micron_anchors(g_state.page_anchor_count + 1U))
    {
        return;
    }

    MicronAnchor& anchor = g_state.page_anchors[g_state.page_anchor_count++];
    copy_text(anchor.name, sizeof(anchor.name), name);
    anchor.target = target;
    anchor.heading = heading;
}

void register_micron_heading_anchor(const char* base_name, lv_obj_t* target)
{
    if (!base_name || base_name[0] == '\0' || !target)
    {
        return;
    }
    if (!micron_anchor_name_exists(base_name))
    {
        register_micron_anchor(base_name, target, true);
        return;
    }
    for (uint8_t suffix = 2; suffix <= kMaxPageAnchors; ++suffix)
    {
        char candidate[micron::kMaxAnchorNameBytes] = {};
        char suffix_text[8] = {};
        std::snprintf(suffix_text, sizeof(suffix_text), "-%u", static_cast<unsigned>(suffix));
        const std::size_t suffix_len = std::strlen(suffix_text);
        const std::size_t max_base_len =
            sizeof(candidate) > suffix_len + 1U ? sizeof(candidate) - suffix_len - 1U : 0;
        const std::size_t base_len = std::strlen(base_name);
        copy_range(candidate,
                   sizeof(candidate),
                   base_name,
                   base_len < max_base_len ? base_len : max_base_len);
        std::snprintf(candidate + std::strlen(candidate),
                      sizeof(candidate) - std::strlen(candidate),
                      "%s",
                      suffix_text);
        if (!micron_anchor_name_exists(candidate))
        {
            register_micron_anchor(candidate, target, true);
            return;
        }
    }
}

lv_obj_t* find_micron_anchor(const char* name)
{
    if (!name || name[0] == '\0')
    {
        return nullptr;
    }
    for (std::size_t i = 0; i < g_state.page_anchor_count; ++i)
    {
        MicronAnchor& anchor = g_state.page_anchors[i];
        if (std::strcmp(anchor.name, name) == 0 && anchor.target &&
            lv_obj_is_valid(anchor.target))
        {
            return anchor.target;
        }
    }
    return nullptr;
}

int32_t viewport_child_index(lv_obj_t* obj)
{
    if (!obj || !g_state.viewport || !lv_obj_is_valid(g_state.viewport))
    {
        return -1;
    }

    lv_obj_t* direct_child = obj;
    while (direct_child && lv_obj_get_parent(direct_child) != g_state.viewport)
    {
        direct_child = lv_obj_get_parent(direct_child);
    }
    if (!direct_child)
    {
        return -1;
    }

    const uint32_t child_count = lv_obj_get_child_count(g_state.viewport);
    for (uint32_t index = 0; index < child_count; ++index)
    {
        if (lv_obj_get_child(g_state.viewport, index) == direct_child)
        {
            return static_cast<int32_t>(index);
        }
    }
    return -1;
}

lv_obj_t* find_next_micron_heading_anchor(lv_obj_t* source)
{
    const int32_t source_index = viewport_child_index(source);
    for (std::size_t i = 0; i < g_state.page_anchor_count; ++i)
    {
        MicronAnchor& anchor = g_state.page_anchors[i];
        if (!anchor.heading || !anchor.target || !lv_obj_is_valid(anchor.target))
        {
            continue;
        }
        const int32_t anchor_index = viewport_child_index(anchor.target);
        if (source_index < 0 || anchor_index > source_index)
        {
            return anchor.target;
        }
    }
    return nullptr;
}

bool jump_to_micron_anchor(const char* target, lv_obj_t* source)
{
    if (!target || target[0] != '#')
    {
        return false;
    }

    lv_obj_t* anchor =
        target[1] == '\0' ? find_next_micron_heading_anchor(source)
                          : find_micron_anchor(target + 1);
    if (!anchor)
    {
        return false;
    }
    lv_obj_scroll_to_view(anchor, LV_ANIM_ON);
    return true;
}

const char* log_text(const char* text)
{
    return text ? text : "";
}

bool request_progress_retryable_failed(const rtpage::RequestProgress& progress)
{
    return progress.failure ==
           rtpage::RequestProgress::FailureKind::Retryable;
}

bool request_progress_terminal_failed(const rtpage::RequestProgress& progress)
{
    return progress.failure == rtpage::RequestProgress::FailureKind::Terminal;
}

const char* directory_mode_label(DirectoryMode mode)
{
    return mode == DirectoryMode::Favourites ? "favourites" : "announces";
}

void log_page_status(const char* stage,
                     const RemotePageAddress& page_address,
                     const rtpage::Status& status,
                     std::size_t body_len,
                     uint32_t elapsed_ms)
{
    NETWORK_PAGE_LOG(
        "%s dest=%s path=%s elapsed_ms=%lu supported=%u sd=%u file=%u checked=%u loaded=%u saved=%u request=%u busy=%u progress=%d truncated=%u body=%lu message=\"%s\" detail=\"%s\"\n",
        log_text(stage),
        page_address.destination,
        page_address.path,
        static_cast<unsigned long>(elapsed_ms),
        status.supported ? 1U : 0U,
        status.sd_present ? 1U : 0U,
        status.file_present ? 1U : 0U,
        status.cache_checked ? 1U : 0U,
        status.loaded ? 1U : 0U,
        status.saved ? 1U : 0U,
        status.request_started ? 1U : 0U,
        status.busy ? 1U : 0U,
        status.progress_percent,
        status.truncated ? 1U : 0U,
        static_cast<unsigned long>(body_len),
        status.message,
        status.detail);
}

const lv_font_t* body_font()
{
    return ::ui::page_profile::resolve_body_font();
}

const lv_font_t* caption_font()
{
    return ::ui::page_profile::resolve_caption_font();
}

const lv_font_t* tiny_font()
{
    return ::ui::page_profile::resolve_tiny_font();
}

void add_to_group(lv_obj_t* obj)
{
    if (app_g && obj)
    {
        lv_group_add_obj(app_g, obj);
    }
}

bool activation_event(lv_event_t* event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_CLICKED)
    {
        return true;
    }
    if (code != LV_EVENT_KEY)
    {
        return false;
    }
    const uint32_t key = lv_event_get_key(event);
    return key == LV_KEY_ENTER || key == LV_KEY_RIGHT;
}

void set_label(lv_obj_t* label,
               const char* text,
               uint32_t color,
               const lv_font_t* font = nullptr)
{
    if (!label)
    {
        return;
    }
    const char* value = text ? text : "";
    lv_label_set_text(label, value);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    ::ui::fonts::apply_localized_font(label, value, font ? font : body_font());
}

void style_plain_container(lv_obj_t* obj, uint32_t bg, lv_opa_t opa = LV_OPA_COVER)
{
    if (!obj)
    {
        return;
    }
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, opa, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN);
}

void style_panel(lv_obj_t* obj, uint32_t bg = kPanelBg)
{
    if (!obj)
    {
        return;
    }
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, lv_color_hex(kLine), LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 8, LV_PART_MAIN);
}

void style_focusable(lv_obj_t* obj)
{
    if (!obj)
    {
        return;
    }
    lv_obj_set_style_border_color(obj, lv_color_hex(kAmberDark), LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(obj, 2, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_color(obj, lv_color_hex(kAmberDark), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_pad(obj, 1, LV_STATE_FOCUS_KEY);
}

void style_chrome_button(lv_obj_t* btn, bool compact = true)
{
    if (!btn)
    {
        return;
    }
    ::ui::components::two_pane_styles::apply_btn_basic(btn);
    lv_obj_set_style_radius(btn, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(kPanelBg), LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(kAmber), LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(btn, lv_color_hex(kAmber), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_bg_color(btn, lv_color_hex(kAmberDark), LV_STATE_PRESSED);
    style_focusable(btn);
    if (compact)
    {
        lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);
    }
}

uint8_t hex_nibble(char ch)
{
    if (ch >= '0' && ch <= '9')
    {
        return static_cast<uint8_t>(ch - '0');
    }
    if (ch >= 'A' && ch <= 'F')
    {
        return static_cast<uint8_t>(ch - 'A' + 10);
    }
    if (ch >= 'a' && ch <= 'f')
    {
        return static_cast<uint8_t>(ch - 'a' + 10);
    }
    return 0xFF;
}

bool is_hex_text(const char* text, std::size_t len)
{
    if (!text)
    {
        return false;
    }
    for (std::size_t i = 0; i < len; ++i)
    {
        if (hex_nibble(text[i]) == 0xFF)
        {
            return false;
        }
    }
    return true;
}

void format_hash_hex(const uint8_t* hash, char* out, std::size_t out_len)
{
    static constexpr char kHex[] = "0123456789ABCDEF";
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    if (!hash || out_len < kHashTextLen)
    {
        copy_text(out, out_len, "--");
        return;
    }
    for (std::size_t i = 0; i < rtdir::kReticulumHashSize; ++i)
    {
        out[i * 2U] = kHex[(hash[i] >> 4U) & 0x0FU];
        out[(i * 2U) + 1U] = kHex[hash[i] & 0x0FU];
    }
    out[rtdir::kReticulumHashSize * 2U] = '\0';
}

void format_hash_prefix(const uint8_t* hash, char* out, std::size_t out_len)
{
    char full[kHashTextLen] = {};
    format_hash_hex(hash, full, sizeof(full));
    if (!out || out_len == 0)
    {
        return;
    }
    std::snprintf(out, out_len, "%.8s", full);
}

bool hash_matches_text(const uint8_t* hash, const char* hash_text)
{
    char full[kHashTextLen] = {};
    format_hash_hex(hash, full, sizeof(full));
    return hash_text && std::strncmp(full, hash_text, rtdir::kReticulumHashSize * 2U) == 0;
}

const char* aspect_label(rtdir::AnnounceAspect aspect)
{
    switch (aspect)
    {
    case rtdir::AnnounceAspect::LxmfDelivery:
        return "LXMF";
    case rtdir::AnnounceAspect::LxmfPropagation:
        return "Relay";
    case rtdir::AnnounceAspect::CallAudio:
        return "Call";
    case rtdir::AnnounceAspect::NomadNetworkNode:
        return "Web";
    case rtdir::AnnounceAspect::Unknown:
    default:
        return "Unknown";
    }
}

const char* announce_display_label(const rtdir::AnnounceRecord& announce,
                                   const char* fallback)
{
    if (announce.display_name[0] != '\0')
    {
        return announce.display_name;
    }
    if (announce.aspect == rtdir::AnnounceAspect::NomadNetworkNode)
    {
        return "Anonymous Node";
    }
    if (announce.aspect == rtdir::AnnounceAspect::LxmfPropagation)
    {
        return "Message Relay";
    }
    if (announce.aspect == rtdir::AnnounceAspect::LxmfDelivery ||
        announce.aspect == rtdir::AnnounceAspect::CallAudio)
    {
        return "Anonymous Peer";
    }
    return fallback ? fallback : "Unknown Service";
}

const char* address_display_label(const rtdir::LxmfAddressRecord& address,
                                  const char* fallback)
{
    if (address.display_name[0] != '\0')
    {
        return address.display_name;
    }
    return fallback ? fallback : "Anonymous Peer";
}

bool announce_visible_in_directory(const rtdir::AnnounceRecord& announce)
{
    return rtnet::visible(announce);
}

std::size_t visible_announce_count()
{
    std::size_t count = 0;
    for (const auto& announce : g_state.announces)
    {
        if (announce_visible_in_directory(announce))
        {
            ++count;
        }
    }
    return count;
}

const rtdir::AnnounceRecord* first_visible_announce()
{
    for (const auto& announce : g_state.announces)
    {
        if (announce_visible_in_directory(announce))
        {
            return &announce;
        }
    }
    return nullptr;
}

bool contains_ci(const char* text, const char* query)
{
    if (!query || query[0] == '\0')
    {
        return true;
    }
    if (!text)
    {
        return false;
    }
    const std::size_t query_len = std::strlen(query);
    const std::size_t text_len = std::strlen(text);
    if (query_len == 0)
    {
        return true;
    }
    if (query_len > text_len)
    {
        return false;
    }
    for (std::size_t start = 0; start + query_len <= text_len; ++start)
    {
        bool match = true;
        for (std::size_t offset = 0; offset < query_len; ++offset)
        {
            const auto lhs = static_cast<unsigned char>(text[start + offset]);
            const auto rhs = static_cast<unsigned char>(query[offset]);
            if (std::tolower(lhs) != std::tolower(rhs))
            {
                match = false;
                break;
            }
        }
        if (match)
        {
            return true;
        }
    }
    return false;
}

bool announce_matches_search(const rtdir::AnnounceRecord& announce)
{
    if (g_state.search_query[0] == '\0')
    {
        return true;
    }
    char destination[kHashTextLen] = {};
    char identity[kHashTextLen] = {};
    format_hash_hex(announce.destination_hash, destination, sizeof(destination));
    format_hash_hex(announce.identity_hash, identity, sizeof(identity));
    return contains_ci(announce_display_label(announce, ""), g_state.search_query) ||
           contains_ci(destination, g_state.search_query) ||
           contains_ci(identity, g_state.search_query) ||
           contains_ci(aspect_label(announce.aspect), g_state.search_query);
}

bool address_matches_search(const rtdir::LxmfAddressRecord& address)
{
    if (g_state.search_query[0] == '\0')
    {
        return true;
    }
    char destination[kHashTextLen] = {};
    char identity[kHashTextLen] = {};
    format_hash_hex(address.destination_hash, destination, sizeof(destination));
    format_hash_hex(address.identity_hash, identity, sizeof(identity));
    return contains_ci(address_display_label(address, ""), g_state.search_query) ||
           contains_ci(destination, g_state.search_query) ||
           contains_ci(identity, g_state.search_query);
}

bool built_in_favourite_matches_search(const BuiltInFavourite& favourite)
{
    if (g_state.search_query[0] == '\0')
    {
        return true;
    }
    return contains_ci(favourite.title, g_state.search_query) ||
           contains_ci(favourite.address, g_state.search_query) ||
           contains_ci(favourite.meta, g_state.search_query);
}

bool reticulum_active()
{
    if (!app::hasAppFacade())
    {
        return false;
    }
    const app::AppConfig& config = app::configFacade().readConfig();
    return chat::infra::isReticulumMeshProtocol(config.mesh_protocol);
}

void page_load_timer_cb(lv_timer_t* timer);

void clear_loaded_page_body()
{
    g_state.cached_page_body_valid = false;
    g_state.page_cache_load_requested = false;
    g_state.cached_page_status = {};
    g_state.cached_page_body_len = 0;
    std::memset(g_state.cached_page_destination,
                0,
                sizeof(g_state.cached_page_destination));
    std::memset(g_state.cached_page_path, 0, sizeof(g_state.cached_page_path));
    g_state.page_body[0] = '\0';
}

bool cached_page_matches(const RemotePageAddress& page_address)
{
    return g_state.cached_page_body_valid &&
           std::strcmp(g_state.cached_page_destination,
                       page_address.destination) == 0 &&
           std::strcmp(g_state.cached_page_path, page_address.path) == 0;
}

bool parse_remote_page_address(const char* address, RemotePageAddress& out);

bool same_remote_page_identity(const char* lhs, const char* rhs)
{
    RemotePageAddress lhs_page{};
    RemotePageAddress rhs_page{};
    return parse_remote_page_address(lhs, lhs_page) && lhs_page.valid &&
           parse_remote_page_address(rhs, rhs_page) && rhs_page.valid &&
           std::strcmp(lhs_page.destination, rhs_page.destination) == 0 &&
           std::strcmp(lhs_page.path, rhs_page.path) == 0;
}

void clear_request_progress_for_address(const char* address)
{
    RemotePageAddress page_address{};
    if (parse_remote_page_address(address, page_address) && page_address.valid)
    {
        rtpage::clear_request_progress(page_address.destination,
                                       page_address.path);
    }
}

void clear_current_remote_page_cache()
{
    RemotePageAddress page_address{};
    if (!parse_remote_page_address(g_state.current_address, page_address) ||
        !page_address.valid)
    {
        return;
    }

    clear_loaded_page_body();
    rtpage::clear_request_progress(page_address.destination, page_address.path);
    const rtpage::Status status =
        rtpage::clear_cached_page(page_address.destination, page_address.path);
    NETWORK_PAGE_LOG("clear page cache dest=%s path=%s supported=%u sd=%u checked=%u file=%u busy=%u msg=%s detail=%s\n",
                     page_address.destination,
                     page_address.path,
                     status.supported ? 1U : 0U,
                     status.sd_present ? 1U : 0U,
                     status.cache_checked ? 1U : 0U,
                     status.file_present ? 1U : 0U,
                     status.busy ? 1U : 0U,
                     status.message,
                     status.detail);
}

void stop_page_load_timer()
{
    if (g_state.page_load_timer)
    {
        lv_timer_pause(g_state.page_load_timer);
    }
}

void ensure_page_load_timer()
{
    if (g_state.page_load_timer)
    {
        lv_timer_resume(g_state.page_load_timer);
        return;
    }
    g_state.page_load_timer =
        lv_timer_create(page_load_timer_cb, kPageLoadPollMs, nullptr);
    if (g_state.page_load_timer)
    {
        lv_timer_set_repeat_count(g_state.page_load_timer, -1);
    }
}

void set_current_address(const char* address)
{
    const char* next_address =
        (address && address[0] != '\0') ? address : "home:/";
    if (std::strncmp(g_state.current_address,
                     next_address,
                     sizeof(g_state.current_address)) != 0 &&
        !same_remote_page_identity(g_state.current_address, next_address))
    {
        clear_request_progress_for_address(g_state.current_address);
        clear_loaded_page_body();
    }
    copy_text(g_state.current_address, sizeof(g_state.current_address),
              next_address);
    if (g_state.address_area && lv_obj_is_valid(g_state.address_area))
    {
        lv_textarea_set_text(g_state.address_area, g_state.current_address);
    }
}

void push_history(const char* address)
{
    if (!address || address[0] == '\0' || g_state.suppress_history)
    {
        return;
    }
    if (g_state.history_count != 0 &&
        std::strncmp(g_state.history[g_state.history_pos].address,
                     address,
                     sizeof(g_state.history[g_state.history_pos].address)) == 0)
    {
        return;
    }
    if (g_state.history_count != 0 &&
        g_state.history_pos + 1U < g_state.history_count)
    {
        g_state.history_count = g_state.history_pos + 1U;
    }
    if (g_state.history_count < g_state.history.size())
    {
        ++g_state.history_count;
        g_state.history_pos = g_state.history_count - 1U;
    }
    else
    {
        for (std::size_t i = 1; i < g_state.history.size(); ++i)
        {
            g_state.history[i - 1U] = g_state.history[i];
        }
        g_state.history_pos = g_state.history.size() - 1U;
    }
    copy_text(g_state.history[g_state.history_pos].address,
              sizeof(g_state.history[g_state.history_pos].address),
              address);
}

bool extract_destination_text(const char* address, char* out, std::size_t out_len)
{
    if (!address || !out || out_len < kHashTextLen)
    {
        return false;
    }
    const char* start = address;
    if (std::strncmp(start, "rn://", 5) == 0)
    {
        start += 5;
    }
    if (std::strncmp(start, "reticulum://", 12) == 0)
    {
        start += 12;
    }
    if (!is_hex_text(start, rtdir::kReticulumHashSize * 2U))
    {
        return false;
    }
    std::snprintf(out, out_len, "%.*s",
                  static_cast<int>(rtdir::kReticulumHashSize * 2U),
                  start);
    return true;
}

const char* remote_address_body(const char* address)
{
    if (!address)
    {
        return nullptr;
    }
    const char* start = address;
    if (std::strncmp(start, "rn://", 5) == 0)
    {
        start += 5;
    }
    if (std::strncmp(start, "reticulum://", 12) == 0)
    {
        start += 12;
    }
    return start;
}

bool parse_remote_page_address(const char* address, RemotePageAddress& out)
{
    out = RemotePageAddress{};
    const char* start = remote_address_body(address);
    if (!start || !is_hex_text(start, rtdir::kReticulumHashSize * 2U))
    {
        return false;
    }

    std::snprintf(out.destination,
                  sizeof(out.destination),
                  "%.*s",
                  static_cast<int>(rtdir::kReticulumHashSize * 2U),
                  start);

    const char* cursor = start + (rtdir::kReticulumHashSize * 2U);
    if (*cursor == ':')
    {
        ++cursor;
    }
    const char* path = (*cursor != '\0') ? cursor : "/page/index.mu";
    const char* anchor = std::strchr(path, '#');
    char path_without_anchor[rtpage::kReticulumPagePathSize] = {};
    if (anchor)
    {
        const std::size_t path_len = static_cast<std::size_t>(anchor - path);
        copy_range(path_without_anchor,
                   sizeof(path_without_anchor),
                   path_len == 0 ? "/page/index.mu" : path,
                   path_len == 0 ? std::strlen("/page/index.mu") : path_len);
        const char* anchor_start = anchor + 1;
        std::size_t anchor_len = 0;
        while (anchor_start[anchor_len] != '\0' &&
               micron::anchor_name_char(anchor_start[anchor_len]))
        {
            ++anchor_len;
        }
        copy_range(out.anchor, sizeof(out.anchor), anchor_start, anchor_len);
        path = path_without_anchor;
    }
    if (!rtpage::normalize_path(path, out.path, sizeof(out.path)))
    {
        return false;
    }
    out.valid = true;
    return true;
}

const rtdir::AnnounceRecord* find_announce_by_destination_text(const char* hash_text)
{
    if (!hash_text)
    {
        return nullptr;
    }
    for (std::size_t i = 0; i < g_state.announce_count; ++i)
    {
        const auto& announce = g_state.announces[i];
        if (announce.valid && hash_matches_text(announce.destination_hash, hash_text))
        {
            return &announce;
        }
    }
    return nullptr;
}

const rtdir::LxmfAddressRecord* find_address_by_destination_text(const char* hash_text)
{
    if (!hash_text)
    {
        return nullptr;
    }
    for (std::size_t i = 0; i < g_state.address_count; ++i)
    {
        const auto& address = g_state.addresses[i];
        if (address.valid && hash_matches_text(address.destination_hash, hash_text))
        {
            return &address;
        }
    }
    return nullptr;
}

std::size_t favourite_count()
{
    std::size_t count = sizeof(kBuiltInFavourites) / sizeof(kBuiltInFavourites[0]);
    for (std::size_t i = 0; i < g_state.address_count; ++i)
    {
        if (g_state.addresses[i].valid && g_state.addresses[i].favorite)
        {
            ++count;
        }
    }
    return count;
}

void render_current_page();
void render_directory_list();
void page_shortcut_event_cb(lv_event_t* event);
void page_load_timer_cb(lv_timer_t* timer);
void rebuild_focus_group(lv_obj_t* preferred = nullptr);
void focus_browser_viewport();
void focus_directory_panel();
void focus_locked_viewport();
void apply_layout_state();
void open_node_list_page();
lv_coord_t resolve_directory_width();
void open_network_help_modal();
void close_network_help_modal();
void clear_loaded_page_body();
void ensure_page_load_timer();
void stop_page_load_timer();

void reset_state_after_destroy()
{
    g_state.root = nullptr;
    g_state.header = nullptr;
    g_state.content = nullptr;
    g_state.directory_panel = nullptr;
    g_state.directory_tabs = nullptr;
    g_state.favourites_tab = nullptr;
    g_state.announces_tab = nullptr;
    g_state.directory_search_btn = nullptr;
    g_state.directory_list = nullptr;
    g_state.browser_panel = nullptr;
    g_state.browser_toolbar = nullptr;
    g_state.browser_body = nullptr;
    g_state.browser_rail = nullptr;
    g_state.browser_back_btn = nullptr;
    g_state.browser_forward_btn = nullptr;
    g_state.home_btn = nullptr;
    g_state.refresh_btn = nullptr;
    g_state.cancel_btn = nullptr;
    g_state.rail_directory_btn = nullptr;
    g_state.rail_search_btn = nullptr;
    g_state.address_area = nullptr;
    g_state.go_btn = nullptr;
    g_state.viewport = nullptr;
    g_state.page_load_timer = nullptr;
    g_state.top_bar = {};
    g_state.search_box = {};
    g_state.help_modal = {};
    std::vector<rtdir::AnnounceRecord>().swap(g_state.announces);
    std::vector<rtdir::LxmfAddressRecord>().swap(g_state.addresses);
    release_link_context_pool();
    release_micron_anchor_registry();
    g_state.row_contexts.fill(DirectoryRowContext{});
    g_state.history.fill(HistoryEntry{});
    g_state.page_body.fill('\0');
    g_state.announce_count = 0;
    g_state.address_count = 0;
    g_state.row_context_count = 0;
    g_state.link_context_capacity = 0;
    g_state.link_context_count = 0;
    g_state.history_count = 0;
    g_state.history_pos = 0;
    g_state.directory_mode = DirectoryMode::Announces;
    g_state.immersive = false;
    g_state.directory_collapsed = true;
    g_state.browser_collapsed = false;
    g_state.viewport_scroll_locked = false;
    g_state.suppress_history = false;
    g_state.cached_page_body_valid = false;
    g_state.page_cache_load_requested = false;
    g_state.announce_status = {};
    g_state.address_status = {};
    g_state.cached_page_status = {};
    g_state.cached_page_body_len = 0;
    std::memset(g_state.cached_page_destination,
                0,
                sizeof(g_state.cached_page_destination));
    std::memset(g_state.cached_page_path, 0, sizeof(g_state.cached_page_path));
    copy_text(g_state.current_address, sizeof(g_state.current_address), "home:/");
    std::memset(g_state.rendered_shell_address, 0, sizeof(g_state.rendered_shell_address));
    std::memset(g_state.search_query, 0, sizeof(g_state.search_query));
}

bool object_in_subtree(lv_obj_t* root, lv_obj_t* obj)
{
    if (!root || !obj || !lv_obj_is_valid(root) || !lv_obj_is_valid(obj))
    {
        return false;
    }
    for (lv_obj_t* cursor = obj; cursor; cursor = lv_obj_get_parent(cursor))
    {
        if (cursor == root)
        {
            return true;
        }
    }
    return false;
}

bool object_visible(lv_obj_t* obj)
{
    if (!obj || !lv_obj_is_valid(obj))
    {
        return false;
    }
    for (lv_obj_t* cursor = obj; cursor; cursor = lv_obj_get_parent(cursor))
    {
        if (lv_obj_has_flag(cursor, LV_OBJ_FLAG_HIDDEN))
        {
            return false;
        }
    }
    return true;
}

void add_visible_to_group(lv_obj_t* obj)
{
    if (app_g && object_visible(obj))
    {
        lv_group_add_obj(app_g, obj);
    }
}

void add_directory_focusables()
{
    if (g_state.immersive || g_state.directory_collapsed)
    {
        return;
    }
    add_visible_to_group(g_state.favourites_tab);
    add_visible_to_group(g_state.announces_tab);
    add_visible_to_group(g_state.directory_search_btn);
    if (!g_state.directory_list || !lv_obj_is_valid(g_state.directory_list))
    {
        return;
    }
    const uint32_t child_count = lv_obj_get_child_count(g_state.directory_list);
    for (uint32_t index = 0; index < child_count; ++index)
    {
        lv_obj_t* child = lv_obj_get_child(g_state.directory_list, index);
        if (child && lv_obj_has_flag(child, LV_OBJ_FLAG_CLICKABLE))
        {
            add_visible_to_group(child);
        }
    }
}

void add_clickable_descendants_to_group(lv_obj_t* obj)
{
    if (!obj || !lv_obj_is_valid(obj) || !object_visible(obj))
    {
        return;
    }
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_CLICKABLE))
    {
        add_visible_to_group(obj);
    }
    const uint32_t child_count = lv_obj_get_child_count(obj);
    for (uint32_t index = 0; index < child_count; ++index)
    {
        add_clickable_descendants_to_group(lv_obj_get_child(obj, index));
    }
}

void add_viewport_links_to_group()
{
    if (!g_state.viewport || !lv_obj_is_valid(g_state.viewport))
    {
        return;
    }
    const uint32_t child_count = lv_obj_get_child_count(g_state.viewport);
    for (uint32_t index = 0; index < child_count; ++index)
    {
        lv_obj_t* child = lv_obj_get_child(g_state.viewport, index);
        add_clickable_descendants_to_group(child);
    }
}

void add_browser_focusables()
{
    if (g_state.browser_collapsed && !g_state.immersive)
    {
        return;
    }
    if (!g_state.immersive)
    {
        add_visible_to_group(g_state.address_area);
        add_visible_to_group(g_state.go_btn);
    }
    add_visible_to_group(g_state.viewport);
    add_viewport_links_to_group();
    if (!g_state.immersive)
    {
        add_visible_to_group(g_state.browser_back_btn);
        add_visible_to_group(g_state.browser_forward_btn);
        add_visible_to_group(g_state.home_btn);
        add_visible_to_group(g_state.refresh_btn);
        add_visible_to_group(g_state.cancel_btn);
        add_visible_to_group(g_state.rail_directory_btn);
        add_visible_to_group(g_state.rail_search_btn);
    }
}

bool focus_preferred_or_fallback(lv_obj_t* preferred, lv_obj_t* previous)
{
    lv_obj_t* target = nullptr;
    if (object_visible(preferred))
    {
        target = preferred;
    }
    else if (object_visible(previous))
    {
        target = previous;
    }
    else if (object_visible(g_state.viewport))
    {
        target = g_state.viewport;
    }

    if (!target || !app_g)
    {
        return false;
    }
    lv_group_focus_obj(target);
    return true;
}

void rebuild_focus_group(lv_obj_t* preferred)
{
    if (!app_g ||
        ::ui::components::floating_search_box::is_open(g_state.search_box) ||
        ::ui::components::shortcut_help_modal::is_open(g_state.help_modal))
    {
        return;
    }

    lv_obj_t* previous = lv_group_get_focused(app_g);
    lv_group_remove_all_objs(app_g);

    if (g_state.menu.is_open())
    {
        add_directory_focusables();
        set_default_group(app_g);
        lv_group_focus_obj(g_state.directory_mode == DirectoryMode::Favourites ? g_state.favourites_tab : g_state.announces_tab);
        return;
    }

    if (!g_state.immersive)
    {
        add_visible_to_group(g_state.top_bar.back_btn);
    }
    add_directory_focusables();
    add_browser_focusables();
    add_visible_to_group(g_state.menu.button());
    set_default_group(app_g);
    (void)focus_preferred_or_fallback(preferred, previous);
}

void focus_browser_viewport()
{
    g_state.menu.close();
    rebuild_focus_group(g_state.viewport);
}

void focus_directory_panel()
{
    if (g_state.menu.button() && !g_state.menu.is_open())
    {
        g_state.menu.open();
        return;
    }
    if (g_state.directory_collapsed)
    {
        return;
    }
    lv_obj_t* target = nullptr;
    if (g_state.directory_list && lv_obj_is_valid(g_state.directory_list))
    {
        const uint32_t child_count = lv_obj_get_child_count(g_state.directory_list);
        for (uint32_t index = 0; index < child_count; ++index)
        {
            lv_obj_t* child = lv_obj_get_child(g_state.directory_list, index);
            if (child && lv_obj_has_flag(child, LV_OBJ_FLAG_CLICKABLE))
            {
                target = child;
                break;
            }
        }
    }
    if (!target)
    {
        target = g_state.directory_mode == DirectoryMode::Favourites
                     ? g_state.favourites_tab
                     : g_state.announces_tab;
    }
    rebuild_focus_group(target);
}

void navigate_to_address(const char* address, bool push)
{
    g_state.menu.close();
    const char* target = (address && address[0] != '\0') ? address : "home:/";
    if (push)
    {
        push_history(target);
    }
    set_current_address(target);
    render_current_page();
    rebuild_focus_group(g_state.viewport);
}

void resolve_link_target(const char* target, char* out, std::size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    if (!target || target[0] == '\0')
    {
        copy_text(out, out_len, "home:/");
        return;
    }
    if (target[0] == '/')
    {
        char hash_text[kHashTextLen] = {};
        if (extract_destination_text(g_state.current_address, hash_text, sizeof(hash_text)))
        {
            copy_text(out, out_len, hash_text);
            std::size_t used = std::strlen(out);
            if (used + 1U < out_len)
            {
                out[used++] = ':';
                out[used] = '\0';
                copy_text(out + used, out_len - used, target);
            }
            return;
        }
    }
    copy_text(out, out_len, target);
}

struct FormPair
{
    char key[40] = {};
    char value[72] = {};
};

bool append_form_pair(FormPair* pairs,
                      std::size_t* pair_count,
                      const char* key,
                      const char* value)
{
    if (!pairs || !pair_count || !key || key[0] == '\0' ||
        *pair_count >= kMaxFormPairs)
    {
        return false;
    }
    FormPair& pair = pairs[(*pair_count)++];
    copy_text(pair.key, sizeof(pair.key), key);
    copy_text(pair.value, sizeof(pair.value), value ? value : "");
    return true;
}

bool form_field_selected(const char* selectors, const char* name)
{
    if (!selectors || !name)
    {
        return false;
    }
    const char* cursor = selectors;
    while (*cursor != '\0')
    {
        const char* end = std::strchr(cursor, '|');
        const std::size_t len = end ? static_cast<std::size_t>(end - cursor)
                                    : std::strlen(cursor);
        if ((len == 1 && cursor[0] == '*') ||
            (std::strlen(name) == len && std::strncmp(cursor, name, len) == 0))
        {
            return true;
        }
        cursor = end ? end + 1 : cursor + len;
    }
    return false;
}

bool collect_form_pairs(const char* selectors,
                        FormPair* pairs,
                        std::size_t* pair_count)
{
    if (!selectors || !pairs || !pair_count)
    {
        return false;
    }
    *pair_count = 0;
    for (std::size_t index = 0; index < g_state.form_field_count; ++index)
    {
        const FormFieldContext& field = g_state.form_fields[index];
        if (!field.object || !lv_obj_is_valid(field.object) ||
            !form_field_selected(selectors, field.name))
        {
            continue;
        }
        if (field.kind == FormFieldKind::Text)
        {
            char key[40] = {};
            std::snprintf(key, sizeof(key), "field_%s", field.name);
            if (!append_form_pair(pairs,
                                  pair_count,
                                  key,
                                  lv_textarea_get_text(field.object)))
            {
                return false;
            }
        }
        else if (lv_obj_has_state(field.object, LV_STATE_CHECKED))
        {
            char key[40] = {};
            std::snprintf(key, sizeof(key), "field_%s", field.name);
            FormPair* existing = nullptr;
            for (std::size_t pair_index = 0; pair_index < *pair_count; ++pair_index)
            {
                if (std::strcmp(pairs[pair_index].key, key) == 0)
                {
                    existing = &pairs[pair_index];
                    break;
                }
            }
            if (existing)
            {
                const std::size_t used = std::strlen(existing->value);
                std::snprintf(existing->value + used,
                              sizeof(existing->value) - used,
                              ",%s",
                              field.value[0] != '\0' ? field.value : "1");
            }
            else if (!append_form_pair(pairs,
                                       pair_count,
                                       key,
                                       field.value[0] != '\0' ? field.value : "1"))
            {
                return false;
            }
        }
    }

    const char* cursor = selectors;
    while (*cursor != '\0')
    {
        const char* end = std::strchr(cursor, '|');
        const std::size_t len = end ? static_cast<std::size_t>(end - cursor)
                                    : std::strlen(cursor);
        const char* equal = static_cast<const char*>(std::memchr(cursor, '=', len));
        if (equal && equal != cursor && equal + 1 < cursor + len)
        {
            char key[40] = "var_";
            copy_range(key + 4,
                       sizeof(key) - 4,
                       cursor,
                       static_cast<std::size_t>(equal - cursor));
            char value[72] = {};
            copy_range(value,
                       sizeof(value),
                       equal + 1,
                       static_cast<std::size_t>((cursor + len) - equal - 1));
            if (!append_form_pair(pairs, pair_count, key, value))
            {
                return false;
            }
        }
        cursor = end ? end + 1 : cursor + len;
    }
    return true;
}

bool append_msgpack_text(const char* value,
                         uint8_t* out,
                         std::size_t capacity,
                         std::size_t* used)
{
    const std::size_t len = value ? std::strlen(value) : 0;
    const std::size_t header = len <= 31U ? 1U : 2U;
    if (!out || !used || len > 255U || *used + header + len > capacity)
    {
        return false;
    }
    out[(*used)++] = len <= 31U ? static_cast<uint8_t>(0xA0U | len) : 0xD9U;
    if (len > 31U)
    {
        out[(*used)++] = static_cast<uint8_t>(len);
    }
    if (len != 0)
    {
        std::memcpy(out + *used, value, len);
        *used += len;
    }
    return true;
}

bool encode_form_payload(const char* selectors,
                         uint8_t* out,
                         std::size_t capacity,
                         std::size_t* out_len)
{
    FormPair pairs[kMaxFormPairs] = {};
    std::size_t pair_count = 0;
    if (!out || !out_len || capacity == 0 ||
        !collect_form_pairs(selectors, pairs, &pair_count) ||
        pair_count > 15U)
    {
        return false;
    }
    std::size_t used = 0;
    out[used++] = static_cast<uint8_t>(0x80U | pair_count);
    for (std::size_t index = 0; index < pair_count; ++index)
    {
        if (!append_msgpack_text(pairs[index].key, out, capacity, &used) ||
            !append_msgpack_text(pairs[index].value, out, capacity, &used))
        {
            return false;
        }
    }
    *out_len = used;
    return true;
}

void link_event_cb(lv_event_t* event)
{
    if (!activation_event(event))
    {
        return;
    }
    if (g_state.viewport_scroll_locked)
    {
        focus_locked_viewport();
        lv_event_stop_bubbling(event);
        lv_event_stop_processing(event);
        return;
    }
    auto* context = static_cast<LinkContext*>(lv_event_get_user_data(event));
    if (!context)
    {
        return;
    }
    if (context->target[0] == '#')
    {
        (void)jump_to_micron_anchor(context->target, context->source);
        lv_event_stop_processing(event);
        return;
    }
    char resolved[kAddressTextLen] = {};
    resolve_link_target(context->target, resolved, sizeof(resolved));
    if (context->request_fields[0] != '\0')
    {
        RemotePageAddress page_address{};
        uint8_t payload[kMaxFormPayloadBytes] = {};
        std::size_t payload_len = 0;
        if (!parse_remote_page_address(resolved, page_address) ||
            !encode_form_payload(context->request_fields,
                                 payload,
                                 sizeof(payload),
                                 &payload_len))
        {
            ::ui::feedback::show_notice("Form data exceeds device limits", 1800);
            lv_event_stop_processing(event);
            return;
        }
        (void)rtpage::clear_cached_page(page_address.destination,
                                        page_address.path);
        clear_loaded_page_body();
        const rtpage::Status status = rtpage::request_page_with_data(
            page_address.destination,
            page_address.path,
            payload,
            payload_len);
        if (!status.request_started)
        {
            ::ui::feedback::show_notice(
                status.message[0] != '\0' ? status.message : "Form submit failed",
                1800);
            lv_event_stop_processing(event);
            return;
        }
    }
    navigate_to_address(resolved, true);
    lv_event_stop_processing(event);
}

void clear_viewport()
{
    if (!g_state.viewport)
    {
        return;
    }
    lv_obj_set_style_bg_color(g_state.viewport, lv_color_hex(kTerminalBg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_state.viewport, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clean(g_state.viewport);
    release_micron_anchor_registry();
    release_link_context_pool();
    g_state.form_field_count = 0;
}

lv_obj_t* add_terminal_label(const char* text,
                             uint32_t color = kTerminalText,
                             const lv_font_t* font = nullptr,
                             lv_label_long_mode_t mode = LV_LABEL_LONG_WRAP)
{
    lv_obj_t* label = lv_label_create(g_state.viewport);
    set_label(label, text ? text : "", color, font ? font : caption_font());
    lv_label_set_long_mode(label, mode);
    lv_obj_set_width(label, LV_PCT(100));
    return label;
}

void add_terminal_spacer(lv_coord_t height = 3)
{
    lv_obj_t* spacer = lv_obj_create(g_state.viewport);
    lv_obj_set_size(spacer, LV_PCT(100), height);
    style_plain_container(spacer, kTerminalBg, LV_OPA_TRANSP);
    ::ui::components::two_pane_layout::make_non_scrollable(spacer);
}

void add_terminal_pair(const char* key, const char* value)
{
    char line[192] = {};
    std::snprintf(line, sizeof(line), "%s %s", key ? key : "", value ? value : "");
    add_terminal_label(line, kTerminalDim, tiny_font(), LV_LABEL_LONG_DOT);
}

void add_terminal_link_text(const char* target, const char* label)
{
    char text[192] = {};
    const char* value = label && label[0] ? label : (target ? target : "");
    std::snprintf(text, sizeof(text), "=> %s", value);
    add_terminal_label(text, kTerminalGreen, caption_font(), LV_LABEL_LONG_DOT);
}

void add_terminal_link(const char* target, const char* label, bool interactive = true)
{
    if (!g_state.viewport)
    {
        return;
    }
    if (!interactive)
    {
        add_terminal_link_text(target, label);
        return;
    }
    LinkContext* context = claim_link_context();
    if (!context)
    {
        add_terminal_link_text(target, label);
        return;
    }
    copy_text(context->target, sizeof(context->target), target);

    lv_obj_t* btn = lv_btn_create(g_state.viewport);
    lv_obj_set_width(btn, LV_PCT(100));
    lv_obj_set_height(btn, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(btn, lv_color_hex(kTerminalBg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1B2A12), LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1B2A12), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(btn, 1, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_color(btn, lv_color_hex(kTerminalGreen), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_radius(btn, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_left(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_right(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_top(btn, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(btn, 2, LV_PART_MAIN);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn, link_event_cb, LV_EVENT_CLICKED, context);
    lv_obj_add_event_cb(btn, link_event_cb, LV_EVENT_KEY, context);
    lv_obj_add_event_cb(btn, page_shortcut_event_cb, LV_EVENT_KEY, nullptr);
    lv_obj_add_event_cb(btn, page_shortcut_event_cb, LV_EVENT_ROTARY, nullptr);
    add_to_group(btn);

    char text[192] = {};
    std::snprintf(text, sizeof(text), "=> %s", label && label[0] ? label : target);
    lv_obj_t* link_label = lv_label_create(btn);
    set_label(link_label, text, kTerminalGreen, caption_font());
    lv_label_set_long_mode(link_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(link_label, LV_PCT(100));
    lv_obj_center(link_label);
}

void copy_range(char* out,
                std::size_t out_len,
                const char* start,
                std::size_t len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    if (!start)
    {
        out[0] = '\0';
        return;
    }
    const std::size_t copy_len = len < out_len - 1U ? len : out_len - 1U;
    std::memcpy(out, start, copy_len);
    out[copy_len] = '\0';
}

bool micron_ascii_space(char ch)
{
    return ch == ' ' || ch == '\t';
}

std::size_t utf8_step_len(unsigned char lead)
{
    if ((lead & 0x80U) == 0)
    {
        return 1;
    }
    if ((lead & 0xE0U) == 0xC0U)
    {
        return 2;
    }
    if ((lead & 0xF0U) == 0xE0U)
    {
        return 3;
    }
    if ((lead & 0xF8U) == 0xF0U)
    {
        return 4;
    }
    return 1;
}

std::size_t bounded_utf8_chunk_len(const char* text, std::size_t remaining)
{
    if (!text || remaining == 0)
    {
        return 0;
    }
    if (remaining <= kMicronTextChunkBytes)
    {
        return remaining;
    }

    std::size_t last_space = 0;
    std::size_t last_boundary = 0;
    std::size_t index = 0;
    while (index < remaining && index < kMicronTextChunkBytes)
    {
        const unsigned char lead = static_cast<unsigned char>(text[index]);
        const std::size_t step = utf8_step_len(lead);
        if (index + step > remaining || index + step > kMicronTextChunkBytes)
        {
            break;
        }
        index += step;
        last_boundary = index;
        if (micron_ascii_space(text[index - 1U]))
        {
            last_space = index;
        }
    }

    if (last_space > 0)
    {
        return last_space;
    }
    return last_boundary > 0 ? last_boundary : 1U;
}

MicronAlign micron_align_from_state(const MicronRenderState& state)
{
    return state.align;
}

lv_flex_align_t micron_flex_align(MicronAlign align)
{
    switch (align)
    {
    case MicronAlign::Center:
        return LV_FLEX_ALIGN_CENTER;
    case MicronAlign::Right:
        return LV_FLEX_ALIGN_END;
    case MicronAlign::Left:
    default:
        return LV_FLEX_ALIGN_START;
    }
}

void apply_micron_row_align(lv_obj_t* row, MicronAlign align)
{
    if (!row)
    {
        return;
    }
    lv_obj_set_flex_align(row,
                          micron_flex_align(align),
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
}

void apply_micron_label_style(lv_obj_t* label,
                              const MicronStyle& style,
                              uint32_t default_bg,
                              const lv_font_t* font = nullptr)
{
    if (!label)
    {
        return;
    }
    lv_obj_set_style_text_color(label, lv_color_hex(style.fg), LV_PART_MAIN);
    lv_obj_set_style_text_font(label,
                               font ? font : (style.bold ? body_font() : caption_font()),
                               LV_PART_MAIN);
    if (style.bg != default_bg)
    {
        lv_obj_set_style_bg_color(label, lv_color_hex(style.bg), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(label, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(label, 1, LV_PART_MAIN);
        lv_obj_set_style_pad_left(label, 1, LV_PART_MAIN);
        lv_obj_set_style_pad_right(label, 1, LV_PART_MAIN);
    }
    else
    {
        lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_pad_left(label, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_right(label, 0, LV_PART_MAIN);
    }
    if (style.underline)
    {
        lv_obj_set_style_border_width(label, 1, LV_PART_MAIN);
        lv_obj_set_style_border_side(label, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
        lv_obj_set_style_border_color(label, lv_color_hex(style.fg), LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(label, 1, LV_PART_MAIN);
    }
}

MicronAlign micron_align_from_contract(micron::Align align)
{
    switch (align)
    {
    case micron::Align::Center:
        return MicronAlign::Center;
    case micron::Align::Right:
        return MicronAlign::Right;
    case micron::Align::Left:
    default:
        return MicronAlign::Left;
    }
}

bool claim_micron_objects(MicronRenderState& state, uint16_t count = 1)
{
    if (count == 0)
    {
        return true;
    }
    if (count > kMicronMaxRenderedObjects ||
        state.rendered_objects > kMicronMaxRenderedObjects - count)
    {
        state.object_limit_reached = true;
        return false;
    }
    state.rendered_objects = static_cast<uint16_t>(state.rendered_objects + count);
    return true;
}

bool claim_micron_row(MicronRenderState& state)
{
    if (state.rendered_rows >= kMicronMaxRenderedRows)
    {
        state.row_limit_reached = true;
        return false;
    }
    if (!claim_micron_objects(state))
    {
        return false;
    }
    ++state.rendered_rows;
    return true;
}

lv_obj_t* create_micron_row(MicronRenderState& state,
                            bool use_line_background = false,
                            uint32_t line_bg = kTerminalBg)
{
    if (!claim_micron_row(state))
    {
        return nullptr;
    }
    lv_obj_t* row = lv_obj_create(g_state.viewport);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    style_plain_container(row,
                          use_line_background ? line_bg : state.default_bg,
                          use_line_background ? LV_OPA_COVER : LV_OPA_TRANSP);
    lv_obj_set_style_pad_top(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(
        row,
        state.depth > 1U ? static_cast<lv_coord_t>((state.depth - 1U) * 8U) : 0,
        LV_PART_MAIN);
    lv_obj_set_style_pad_right(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(row, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
    apply_micron_row_align(row, micron_align_from_state(state));
    ::ui::components::two_pane_layout::make_non_scrollable(row);
    return row;
}

void emit_micron_text_token(lv_obj_t* row,
                            const char* text,
                            const MicronStyle& style,
                            uint32_t default_bg,
                            const lv_font_t* font = nullptr,
                            MicronRenderState* budget = nullptr)
{
    if (!row || !text || text[0] == '\0')
    {
        return;
    }
    if (budget && !claim_micron_objects(*budget))
    {
        return;
    }
    lv_obj_t* label = lv_label_create(row);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(label, LV_PCT(100), LV_PART_MAIN);
    apply_micron_label_style(label, style, default_bg, font);
    ::ui::fonts::apply_content_font(label,
                                    text,
                                    font ? font : (style.bold ? body_font() : caption_font()));
}

void emit_micron_text(lv_obj_t* row,
                      const char* text,
                      const MicronStyle& style,
                      uint32_t default_bg,
                      const lv_font_t* font = nullptr,
                      MicronRenderState* budget = nullptr)
{
    if (!row || !text || text[0] == '\0')
    {
        return;
    }
    const std::size_t text_len = std::strlen(text);
    const char* cursor = text;
    std::size_t remaining = text_len;
    while (remaining > 0)
    {
        char chunk[kMicronTextChunkBytes + 1U] = {};
        const std::size_t chunk_len = bounded_utf8_chunk_len(cursor, remaining);
        if (chunk_len == 0)
        {
            break;
        }
        copy_range(chunk, sizeof(chunk), cursor, chunk_len);
        for (char* ch = chunk; *ch != '\0'; ++ch)
        {
            if (*ch == '\t')
            {
                *ch = ' ';
            }
        }
        emit_micron_text_token(row, chunk, style, default_bg, font, budget);
        cursor += chunk_len;
        remaining -= chunk_len;
    }
}

void reset_micron_style(MicronRenderState& state)
{
    state.style.fg = state.default_fg;
    state.style.bg = state.default_bg;
    state.style.bold = false;
    state.style.underline = false;
    state.style.italic = false;
    state.align = state.default_align;
}

void format_micron_tag_sample(char tag, char* out, std::size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    const auto value = static_cast<unsigned char>(tag);
    if (std::isprint(value))
    {
        std::snprintf(out, out_len, "`%c", tag);
        return;
    }
    std::snprintf(out, out_len, "0x%02X", static_cast<unsigned>(value));
}

void record_unsupported_micron_tag(MicronRenderState& state, char tag)
{
    ++state.unsupported_unknown_tags;
    char sample[kMicronUnsupportedTagSampleBytes] = {};
    format_micron_tag_sample(tag, sample, sizeof(sample));
    for (uint8_t i = 0; i < state.unsupported_tag_sample_count; ++i)
    {
        if (std::strcmp(state.unsupported_tag_samples[i].data(), sample) == 0)
        {
            return;
        }
    }
    if (state.unsupported_tag_sample_count < state.unsupported_tag_samples.size())
    {
        copy_text(state.unsupported_tag_samples[state.unsupported_tag_sample_count].data(),
                  state.unsupported_tag_samples[state.unsupported_tag_sample_count].size(),
                  sample);
        ++state.unsupported_tag_sample_count;
    }
}

void normalize_micron_link_target(const char* target,
                                  char* out,
                                  std::size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    const char* value = target ? target : "";
    static constexpr const char* kNomadScheme = "nomadnetwork://";
    static constexpr const char* kLxmfScheme = "lxmf://";
    if (std::strncmp(value, kNomadScheme, std::strlen(kNomadScheme)) == 0)
    {
        value += std::strlen(kNomadScheme);
    }
    else if (std::strncmp(value, kLxmfScheme, std::strlen(kLxmfScheme)) == 0)
    {
        value += std::strlen(kLxmfScheme);
    }

    if (value[0] == ':' && value[1] == '/')
    {
        char hash_text[kHashTextLen] = {};
        if (extract_destination_text(g_state.current_address,
                                     hash_text,
                                     sizeof(hash_text)))
        {
            std::snprintf(out, out_len, "%s%s", hash_text, value);
            return;
        }
    }
    copy_text(out, out_len, value);
}

void emit_micron_link(lv_obj_t* row,
                      const char* target,
                      const char* label,
                      MicronRenderState& state,
                      const char* request_fields)
{
    if (!row || !target || target[0] == '\0')
    {
        return;
    }
    const MicronStyle& style = state.style;
    const uint32_t default_bg = state.default_bg;

    const micron::LinkTargetKind target_kind = micron::classify_link_target(target);
    if (target_kind == micron::LinkTargetKind::Resource ||
        target_kind == micron::LinkTargetKind::Unknown)
    {
        if (target_kind == micron::LinkTargetKind::Resource)
        {
            state.unsupported_resource_links++;
        }
        else
        {
            state.unsupported_unknown_links++;
        }
        emit_micron_text(row,
                         label && label[0] ? label : target,
                         style,
                         default_bg,
                         nullptr,
                         &state);
        MicronStyle note_style = style;
        note_style.fg = kTerminalDim;
        note_style.bg = default_bg;
        note_style.bold = false;
        note_style.underline = false;
        emit_micron_text(row,
                         target_kind == micron::LinkTargetKind::Resource
                             ? " [resource unsupported]"
                             : " [link unsupported]",
                         note_style,
                         default_bg,
                         tiny_font(),
                         &state);
        return;
    }

    if (state.rendered_objects > kMicronMaxRenderedObjects - 2U)
    {
        state.object_limit_reached = true;
        return;
    }

    LinkContext* context = claim_link_context();
    if (!context)
    {
        state.link_limit_reached = true;
        emit_micron_text(row,
                         label && label[0] ? label : target,
                         style,
                         default_bg,
                         nullptr,
                         &state);
        return;
    }

    if (!claim_micron_objects(state, 2))
    {
        return;
    }

    normalize_micron_link_target(target, context->target, sizeof(context->target));
    copy_text(context->request_fields,
              sizeof(context->request_fields),
              request_fields);
    context->source = row;

    lv_obj_t* btn = lv_btn_create(row);
    lv_obj_set_height(btn, LV_SIZE_CONTENT);
    lv_obj_set_width(btn, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(btn, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_left(btn, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_right(btn, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_top(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 2, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, lv_color_hex(style.fg), LV_PART_MAIN);
    lv_obj_set_style_bg_color(
        btn,
        lv_color_hex(style.bg != default_bg ? style.bg : kTerminalBg),
        LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(kTerminalGreen), LV_STATE_PRESSED);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn, link_event_cb, LV_EVENT_CLICKED, context);
    lv_obj_add_event_cb(btn, link_event_cb, LV_EVENT_KEY, context);
    lv_obj_add_event_cb(btn, page_shortcut_event_cb, LV_EVENT_KEY, nullptr);
    lv_obj_add_event_cb(btn, page_shortcut_event_cb, LV_EVENT_ROTARY, nullptr);

    lv_obj_t* text = lv_label_create(btn);
    const char* visible = label && label[0] ? label : target;
    lv_label_set_text(text, visible);
    lv_label_set_long_mode(text, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(text, LV_SIZE_CONTENT);
    apply_micron_label_style(text, style, default_bg);
    ::ui::fonts::apply_content_font(text,
                                    visible,
                                    style.bold ? body_font() : caption_font());
    lv_obj_center(text);
}

void form_field_event_cb(lv_event_t* event)
{
    if (!event || lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED)
    {
        return;
    }
    auto* selected = static_cast<FormFieldContext*>(lv_event_get_user_data(event));
    if (!selected || selected->kind != FormFieldKind::Radio ||
        !lv_obj_has_state(selected->object, LV_STATE_CHECKED))
    {
        return;
    }
    for (std::size_t index = 0; index < g_state.form_field_count; ++index)
    {
        FormFieldContext& field = g_state.form_fields[index];
        if (&field != selected && field.kind == FormFieldKind::Radio &&
            std::strcmp(field.name, selected->name) == 0 && field.object &&
            lv_obj_is_valid(field.object))
        {
            lv_obj_clear_state(field.object, LV_STATE_CHECKED);
        }
    }
}

FormFieldContext* claim_form_field(const char* name,
                                   const char* value,
                                   FormFieldKind kind)
{
    if (!name || name[0] == '\0' || g_state.form_field_count >= kMaxFormFields)
    {
        return nullptr;
    }
    FormFieldContext& field = g_state.form_fields[g_state.form_field_count++];
    field = FormFieldContext{};
    copy_text(field.name, sizeof(field.name), name);
    copy_text(field.value, sizeof(field.value), value);
    field.kind = kind;
    return &field;
}

void emit_micron_field(lv_obj_t* row,
                       const char* spec,
                       const char* data,
                       MicronRenderState& state)
{
    if (!row || !spec)
    {
        return;
    }
    state.saw_fields = true;
    const MicronStyle& style = state.style;
    const uint32_t default_bg = state.default_bg;
    bool checkbox = false;
    bool radio = false;
    bool masked = false;
    bool checked = false;
    uint16_t width_chars = 16;
    const char* name = spec;
    char name_buf[48] = {};
    char value_buf[64] = {};

    const char* first_sep = std::strchr(spec, '|');
    if (first_sep)
    {
        char flags[16] = {};
        copy_range(flags, sizeof(flags), spec, static_cast<std::size_t>(first_sep - spec));
        checkbox = std::strchr(flags, '?') != nullptr;
        radio = std::strchr(flags, '^') != nullptr;
        masked = std::strchr(flags, '!') != nullptr;
        const char* cursor = flags;
        while (*cursor != '\0')
        {
            if (std::isdigit(static_cast<unsigned char>(*cursor)))
            {
                width_chars = static_cast<uint16_t>(
                    width_chars * 0U + static_cast<uint16_t>(std::atoi(cursor)));
                break;
            }
            ++cursor;
        }
        const char* second_sep = std::strchr(first_sep + 1, '|');
        if (second_sep)
        {
            copy_range(name_buf,
                       sizeof(name_buf),
                       first_sep + 1,
                       static_cast<std::size_t>(second_sep - first_sep - 1));
            const char* third_sep = std::strchr(second_sep + 1, '|');
            if (third_sep)
            {
                copy_range(value_buf,
                           sizeof(value_buf),
                           second_sep + 1,
                           static_cast<std::size_t>(third_sep - second_sep - 1));
                checked = std::strcmp(third_sep + 1, "*") == 0;
            }
            else
            {
                copy_text(value_buf, sizeof(value_buf), second_sep + 1);
            }
        }
        else
        {
            copy_text(name_buf, sizeof(name_buf), first_sep + 1);
        }
        name = name_buf[0] ? name_buf : spec;
    }

    if (checkbox || radio)
    {
        if (!claim_micron_objects(state))
        {
            return;
        }
        lv_obj_t* cb = lv_checkbox_create(row);
        lv_checkbox_set_text(cb, data && data[0] ? data : (value_buf[0] ? value_buf : name));
        if (checked)
        {
            lv_obj_add_state(cb, LV_STATE_CHECKED);
        }
        lv_obj_set_style_text_color(cb, lv_color_hex(style.fg), LV_PART_MAIN);
        lv_obj_set_style_text_font(cb, caption_font(), LV_PART_MAIN);
        lv_obj_set_style_text_opa(cb, LV_OPA_70, LV_PART_MAIN);
        lv_obj_set_style_border_width(cb, 1, LV_PART_INDICATOR);
        lv_obj_set_style_border_color(cb, lv_color_hex(kTerminalDim), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(cb, lv_color_hex(0x101010), LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(cb, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(cb,
                                  lv_color_hex(kTerminalDim),
                                  LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(cb, LV_OPA_70, LV_PART_INDICATOR | LV_STATE_CHECKED);
        FormFieldContext* field_context = claim_form_field(
            name,
            value_buf[0] != '\0' ? value_buf : "1",
            radio ? FormFieldKind::Radio : FormFieldKind::Checkbox);
        if (field_context)
        {
            field_context->object = cb;
            lv_obj_add_event_cb(cb,
                                form_field_event_cb,
                                LV_EVENT_VALUE_CHANGED,
                                field_context);
            add_to_group(cb);
        }
        else
        {
            lv_obj_add_state(cb, LV_STATE_DISABLED);
        }
        if (style.bg != default_bg)
        {
            lv_obj_set_style_bg_color(cb, lv_color_hex(style.bg), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(cb, LV_OPA_COVER, LV_PART_MAIN);
        }
        return;
    }

    if (!claim_micron_objects(state))
    {
        return;
    }
    lv_obj_t* field = lv_textarea_create(row);
    ::ui::widgets::attach_touch_text_editor(field);
    lv_textarea_set_one_line(field, true);
    lv_textarea_set_text(field, data ? data : "");
    lv_textarea_set_password_mode(field, masked);
    const lv_coord_t field_w =
        static_cast<lv_coord_t>((width_chars < 6 ? 6 : width_chars) * 6U);
    lv_obj_set_size(field, field_w > 160 ? 160 : field_w, 18);
    lv_obj_set_style_bg_color(
        field,
        lv_color_hex(style.bg != default_bg ? style.bg : 0x222222),
        LV_PART_MAIN);
    lv_obj_set_style_bg_opa(field, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(field, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(field, lv_color_hex(kTerminalDim), LV_PART_MAIN);
    lv_obj_set_style_bg_color(field, lv_color_hex(0x151515), LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_set_style_bg_opa(field, LV_OPA_80, LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_set_style_border_color(field,
                                  lv_color_hex(0x555555),
                                  LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_set_style_radius(field, 2, LV_PART_MAIN);
    lv_obj_set_style_text_color(field, lv_color_hex(style.fg), LV_PART_MAIN);
    lv_obj_set_style_text_opa(field, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_text_font(field, caption_font(), LV_PART_MAIN);
    lv_obj_set_style_pad_all(field, 1, LV_PART_MAIN);
    FormFieldContext* field_context = claim_form_field(
        name, data, FormFieldKind::Text);
    if (field_context)
    {
        field_context->object = field;
        add_to_group(field);
    }
    else
    {
        lv_obj_add_state(field, LV_STATE_DISABLED);
    }
}

void flush_micron_part(lv_obj_t* row,
                       char* part,
                       std::size_t& part_len,
                       MicronRenderState& state,
                       const lv_font_t* font = nullptr)
{
    if (part_len == 0)
    {
        return;
    }
    part[part_len] = '\0';
    emit_micron_text(row, part, state.style, state.default_bg, font, &state);
    part_len = 0;
}

void append_micron_part_char(lv_obj_t* row,
                             char* part,
                             std::size_t& part_len,
                             char ch,
                             MicronRenderState& state,
                             const lv_font_t* font = nullptr)
{
    if (part_len + 1U >= 384U)
    {
        flush_micron_part(row, part, part_len, state, font);
    }
    part[part_len++] = ch;
}

bool parse_micron_link(lv_obj_t* row,
                       const char* line,
                       std::size_t line_len,
                       std::size_t start,
                       MicronRenderState& state,
                       std::size_t* consumed)
{
    if (!row || !line || !consumed)
    {
        return false;
    }
    const char* close = std::strchr(line + start + 1U, ']');
    if (!close)
    {
        return false;
    }
    const std::size_t data_len =
        static_cast<std::size_t>(close - (line + start + 1U));
    char data[224] = {};
    copy_range(data, sizeof(data), line + start + 1U, data_len);

    char label[96] = {};
    char target[kAddressTextLen] = {};
    char fields[96] = {};
    const char* link_data = data[0] == '`' ? data + 1 : data;
    const char* first_tick = std::strchr(link_data, '`');
    if (!first_tick)
    {
        copy_text(target, sizeof(target), link_data);
        copy_text(label, sizeof(label), link_data);
    }
    else
    {
        copy_range(label,
                   sizeof(label),
                   link_data,
                   static_cast<std::size_t>(first_tick - link_data));
        const char* second_tick = std::strchr(first_tick + 1, '`');
        if (second_tick)
        {
            copy_range(target,
                       sizeof(target),
                       first_tick + 1,
                       static_cast<std::size_t>(second_tick - first_tick - 1));
            copy_text(fields, sizeof(fields), second_tick + 1);
        }
        else
        {
            copy_text(target, sizeof(target), first_tick + 1);
        }
        if (label[0] == '\0')
        {
            copy_text(label, sizeof(label), target);
        }
    }

    const micron::LinkFields link_fields = micron::parse_link_fields(fields);
    char navigation_target[kAddressTextLen] = {};
    if (link_fields.has_anchor)
    {
        (void)micron::append_anchor_to_target(
            target, link_fields.anchor, navigation_target, sizeof(navigation_target));
    }
    if (navigation_target[0] == '\0')
    {
        copy_text(navigation_target, sizeof(navigation_target), target);
    }
    if (link_fields.submits_fields)
    {
        state.saw_submit_links = true;
    }

    emit_micron_link(row,
                     navigation_target,
                     label,
                     state,
                     link_fields.submits_fields ? fields : nullptr);
    *consumed = static_cast<std::size_t>(close - (line + start)) + 1U;
    (void)line_len;
    return true;
}

bool parse_micron_field(lv_obj_t* row,
                        const char* line,
                        std::size_t start,
                        MicronRenderState& state,
                        std::size_t* consumed)
{
    if (!row || !line || !consumed)
    {
        return false;
    }
    const char* tick = std::strchr(line + start + 1U, '`');
    if (!tick)
    {
        return false;
    }
    const char* close = std::strchr(tick + 1U, '>');
    if (!close)
    {
        return false;
    }
    char spec[96] = {};
    char data[96] = {};
    copy_range(spec,
               sizeof(spec),
               line + start + 1U,
               static_cast<std::size_t>(tick - (line + start + 1U)));
    copy_range(data,
               sizeof(data),
               tick + 1U,
               static_cast<std::size_t>(close - (tick + 1U)));
    emit_micron_field(row, spec, data, state);
    *consumed = static_cast<std::size_t>(close - (line + start)) + 1U;
    return true;
}

void render_micron_inline(lv_obj_t* row,
                          const char* line,
                          MicronRenderState& state,
                          const lv_font_t* font = nullptr)
{
    if (!row || !line)
    {
        return;
    }
    char part[384] = {};
    std::size_t part_len = 0;
    const std::size_t line_len = std::strlen(line);
    bool escape = false;

    for (std::size_t i = 0; i < line_len;)
    {
        const char ch = line[i];
        if (escape)
        {
            append_micron_part_char(row, part, part_len, ch, state, font);
            escape = false;
            ++i;
            continue;
        }
        if (ch == '\\')
        {
            escape = true;
            ++i;
            continue;
        }
        if (ch != '`')
        {
            append_micron_part_char(row, part, part_len, ch, state, font);
            ++i;
            continue;
        }

        if (i + 1U < line_len && line[i + 1U] == '`')
        {
            flush_micron_part(row, part, part_len, state, font);
            reset_micron_style(state);
            i += 2U;
            apply_micron_row_align(row, state.align);
            continue;
        }

        flush_micron_part(row, part, part_len, state, font);
        ++i;
        if (i >= line_len)
        {
            break;
        }
        const char tag = line[i];
        switch (tag)
        {
        case '!':
            state.style.bold = !state.style.bold;
            ++i;
            break;
        case '_':
            state.style.underline = !state.style.underline;
            ++i;
            break;
        case '*':
            state.style.italic = !state.style.italic;
            ++i;
            break;
        case 'F':
        case 'B':
        {
            const micron::ColorToken color = micron::parse_color_token(line, line_len, i);
            if (color.valid)
            {
                if (tag == 'F')
                {
                    state.style.fg = color.rgb;
                }
                else
                {
                    state.style.bg = color.rgb;
                }
                i += color.consumed;
            }
            else
            {
                record_unsupported_micron_tag(state, tag);
                ++i;
            }
            break;
        }
        case 'f':
            state.style.fg = state.default_fg;
            ++i;
            break;
        case 'b':
            state.style.bg = state.default_bg;
            ++i;
            break;
        case 'c':
            state.align = MicronAlign::Center;
            apply_micron_row_align(row, state.align);
            ++i;
            break;
        case 'l':
            state.align = MicronAlign::Left;
            apply_micron_row_align(row, state.align);
            ++i;
            break;
        case 'r':
            state.align = MicronAlign::Right;
            apply_micron_row_align(row, state.align);
            ++i;
            break;
        case 'a':
            state.align = state.default_align;
            apply_micron_row_align(row, state.align);
            ++i;
            break;
        case '<':
        {
            std::size_t consumed = 0;
            if (parse_micron_field(row, line, i, state, &consumed))
            {
                i += consumed;
            }
            else
            {
                ++i;
            }
            break;
        }
        case '[':
        {
            std::size_t consumed = 0;
            if (parse_micron_link(row, line, line_len, i, state, &consumed))
            {
                i += consumed;
            }
            else
            {
                ++i;
            }
            break;
        }
        case ':':
        {
            char anchor[micron::kMaxAnchorNameBytes] = {};
            const std::size_t anchor_start = i + 1U;
            std::size_t anchor_len = 0;
            while (anchor_start + anchor_len < line_len &&
                   micron::anchor_name_char(line[anchor_start + anchor_len]))
            {
                ++anchor_len;
            }
            if (anchor_len > 0)
            {
                copy_range(anchor, sizeof(anchor), line + anchor_start, anchor_len);
                register_micron_anchor(anchor, row, false);
            }
            i = anchor_start + anchor_len;
            break;
        }
        case '{':
            state.unsupported_inline_blocks++;
            emit_micron_text(row,
                             kMicronUnsupportedInline,
                             state.style,
                             state.default_bg,
                             font,
                             &state);
            while (i < line_len && line[i] != '}')
            {
                ++i;
            }
            if (i < line_len)
            {
                ++i;
            }
            break;
        case '`':
            reset_micron_style(state);
            apply_micron_row_align(row, state.align);
            ++i;
            break;
        default:
            record_unsupported_micron_tag(state, tag);
            ++i;
            break;
        }
    }
    flush_micron_part(row, part, part_len, state, font);
}

MicronStyle micron_heading_style(uint8_t depth)
{
    MicronStyle style{};
    if (depth <= 1U)
    {
        style.fg = kMicronHeading1Fg;
        style.bg = kMicronHeading1Bg;
    }
    else if (depth == 2U)
    {
        style.fg = kMicronHeading2Fg;
        style.bg = kMicronHeading2Bg;
    }
    else
    {
        style.fg = kMicronHeading3Fg;
        style.bg = kMicronHeading3Bg;
    }
    style.bold = depth <= 2U;
    return style;
}

void render_micron_divider(MicronRenderState& state, const char* line)
{
    char divider[96] = {};
    const char fill = line && line[1] != '\0' ? line[1] : '-';
    for (std::size_t i = 0; i + 1U < sizeof(divider); ++i)
    {
        divider[i] = fill >= 32 ? fill : '-';
    }
    lv_obj_t* row = create_micron_row(state);
    emit_micron_text(row,
                     divider,
                     state.style,
                     state.default_bg,
                     caption_font(),
                     &state);
}

void render_micron_empty_line(MicronRenderState& state)
{
    if (!claim_micron_row(state))
    {
        return;
    }
    lv_obj_t* spacer = lv_obj_create(g_state.viewport);
    lv_obj_set_size(spacer, LV_PCT(100), 8);
    style_plain_container(spacer,
                          state.style.bg != state.default_bg ? state.style.bg
                                                             : state.default_bg,
                          state.style.bg != state.default_bg ? LV_OPA_COVER
                                                             : LV_OPA_TRANSP);
    ::ui::components::two_pane_layout::make_non_scrollable(spacer);
}

void render_micron_line(const char* line, MicronRenderState& state)
{
    if (!line)
    {
        return;
    }
    if (line[0] == '\0')
    {
        render_micron_empty_line(state);
        return;
    }
    if (line[0] == '#')
    {
        return;
    }
    if (!state.literal && line[0] == '`' && line[1] == '=' && line[2] == '\0')
    {
        state.literal = true;
        return;
    }
    if (state.literal)
    {
        if (line[0] == '`' && line[1] == '=' && line[2] == '\0')
        {
            state.literal = false;
            return;
        }
        const char* text = line;
        if (line[0] == '\\' && line[1] == '`' && line[2] == '=' && line[3] == '\0')
        {
            text = line + 1;
        }
        lv_obj_t* row = create_micron_row(state, true, 0x101418);
        if (row)
        {
            lv_obj_set_style_pad_left(row, 4, LV_PART_MAIN);
            lv_obj_set_style_pad_top(row, 1, LV_PART_MAIN);
            lv_obj_set_style_pad_bottom(row, 1, LV_PART_MAIN);
            lv_obj_set_style_border_width(row, 2, LV_PART_MAIN);
            lv_obj_set_style_border_side(row, LV_BORDER_SIDE_LEFT, LV_PART_MAIN);
            lv_obj_set_style_border_color(row, lv_color_hex(kTerminalDim), LV_PART_MAIN);
        }
        emit_micron_text(row,
                         text,
                         state.style,
                         state.default_bg,
                         tiny_font(),
                         &state);
        return;
    }
    if (line[0] == '<')
    {
        state.depth = 0;
        if (line[1] == '\0')
        {
            return;
        }
        render_micron_line(line + 1, state);
        return;
    }
    if (line[0] == '>')
    {
        uint8_t depth = 0;
        const char* text = line;
        while (*text == '>' && depth < 8U)
        {
            ++depth;
            ++text;
        }
        state.depth = depth;
        if (*text == '\0')
        {
            return;
        }
        const MicronStyle saved_style = state.style;
        state.style = micron_heading_style(depth);
        lv_obj_t* row = create_micron_row(state, true, state.style.bg);
        char anchor[micron::kMaxAnchorNameBytes] = {};
        micron::slug_from_text(text, anchor, sizeof(anchor));
        register_micron_heading_anchor(anchor, row);
        render_micron_inline(row, text, state, depth <= 1U ? body_font() : caption_font());
        state.style = saved_style;
        return;
    }
    if (line[0] == '-' && (line[1] == '\0' || line[2] == '\0'))
    {
        render_micron_divider(state, line);
        return;
    }
    if (line[0] == '`' && line[1] == '{')
    {
        lv_obj_t* row = create_micron_row(state);
        const micron::PartialReference partial = micron::parse_partial_reference(line);
        char notice[192] = {};
        if (partial.valid)
        {
            std::snprintf(notice,
                          sizeof(notice),
                          "[unsupported %s partial: %s]",
                          micron::link_target_kind_label(partial.target_kind),
                          partial.target);
            state.unsupported_partial_count++;
        }
        else
        {
            copy_text(notice, sizeof(notice), kMicronUnsupportedBlock);
            state.unsupported_block_count++;
        }
        emit_micron_text(row,
                         notice,
                         state.style,
                         state.default_bg,
                         caption_font(),
                         &state);
        if (partial.valid && partial.target_kind == micron::LinkTargetKind::Page)
        {
            emit_micron_text(row,
                             " ",
                             state.style,
                             state.default_bg,
                             caption_font(),
                             &state);
            emit_micron_link(row, partial.target, "Open partial", state, nullptr);
        }
        return;
    }

    lv_obj_t* row = create_micron_row(state);
    render_micron_inline(row, line, state, caption_font());
}

void begin_micron_table(MicronRenderState& state,
                        const micron::TableOpenOptions& options)
{
    state.table_restore_style = state.style;
    state.table_restore_align = state.align;
    state.style.bg = state.style.bg != state.default_bg ? state.style.bg : 0x151515;
    state.table_default_align =
        options.has_align ? micron_align_from_contract(options.align) : MicronAlign::Left;
    state.table_max_width_chars = options.has_max_width ? options.max_width : 0;
    state.align = state.table_default_align;
    state.table_column_count = 0;
    for (auto& align : state.table_column_align)
    {
        align = state.table_default_align;
    }
    state.table_row_index = 0;
    state.table_first_row = true;
    state.table_mode = true;
}

void finish_micron_table(MicronRenderState& state)
{
    state.style = state.table_restore_style;
    state.align = state.table_restore_align;
    state.table_column_count = 0;
    state.table_default_align = MicronAlign::Left;
    state.table_max_width_chars = 0;
    state.table_row_index = 0;
    state.table_first_row = false;
    state.table_mode = false;
}

void apply_micron_table_width(lv_obj_t* row, const MicronRenderState& state)
{
    if (!row || state.table_max_width_chars == 0)
    {
        return;
    }
    const lv_coord_t requested =
        static_cast<lv_coord_t>(state.table_max_width_chars * 6U);
    const lv_coord_t viewport_width =
        g_state.viewport ? lv_obj_get_content_width(g_state.viewport) : 0;
    lv_obj_set_style_max_width(row, LV_PCT(100), LV_PART_MAIN);
    if (viewport_width > 0 && requested > viewport_width)
    {
        lv_obj_set_width(row, LV_PCT(100));
        return;
    }
    lv_obj_set_width(row, requested);
}

void update_micron_table_alignment(
    MicronRenderState& state,
    const std::array<micron::Span, kMicronTableMaxCells>& cells,
    std::size_t count)
{
    state.table_column_count = static_cast<uint8_t>(
        count < state.table_column_align.size() ? count
                                                : state.table_column_align.size());
    for (std::size_t i = 0; i < state.table_column_count; ++i)
    {
        state.table_column_align[i] =
            micron_align_from_contract(micron::table_separator_align(cells[i]));
    }
}

void render_micron_table_row(MicronRenderState& state, const char* row_text)
{
    std::array<micron::Span, kMicronTableMaxCells> cells{};
    const std::size_t cell_count =
        micron::parse_table_cells(row_text, cells.data(), cells.size());
    if (micron::table_separator_row(cells.data(), cell_count))
    {
        update_micron_table_alignment(state, cells, cell_count);
        return;
    }
    if (cell_count > 0)
    {
        const bool header_row = state.table_first_row;
        state.table_first_row = false;
        lv_obj_t* row = create_micron_row(state, true, state.style.bg);
        if (!row)
        {
            return;
        }
        apply_micron_table_width(row, state);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(row, 2, LV_PART_MAIN);
        lv_obj_set_style_pad_top(row, header_row ? 1 : 0, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(row, header_row ? 1 : 0, LV_PART_MAIN);
        const uint32_t row_bg = header_row ? 0x302814
                                : (state.table_row_index % 2U == 0U)
                                    ? state.style.bg
                                    : 0x101010;
        for (std::size_t i = 0; i < cell_count; ++i)
        {
            char cell_text[kMicronTableCellTextBytes] = {};
            copy_range(cell_text, sizeof(cell_text), cells[i].start, cells[i].len);

            if (!claim_micron_objects(state))
            {
                return;
            }
            lv_obj_t* cell = lv_obj_create(row);
            lv_obj_set_width(cell, 0);
            lv_obj_set_height(cell, LV_SIZE_CONTENT);
            lv_obj_set_flex_grow(cell, 1);
            style_plain_container(cell, row_bg, LV_OPA_COVER);
            lv_obj_set_style_border_width(cell, header_row ? 2 : 1, LV_PART_MAIN);
            lv_obj_set_style_border_color(
                cell,
                lv_color_hex(header_row ? kTerminalAmber : 0x3A3A3A),
                LV_PART_MAIN);
            lv_obj_set_style_radius(cell, 1, LV_PART_MAIN);
            lv_obj_set_style_pad_left(cell, header_row ? 3 : 2, LV_PART_MAIN);
            lv_obj_set_style_pad_right(cell, header_row ? 3 : 2, LV_PART_MAIN);
            lv_obj_set_style_pad_top(cell, header_row ? 2 : 1, LV_PART_MAIN);
            lv_obj_set_style_pad_bottom(cell, header_row ? 2 : 1, LV_PART_MAIN);
            lv_obj_set_style_pad_row(cell, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_column(cell, 0, LV_PART_MAIN);
            lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_ROW_WRAP);

            MicronRenderState cell_state = state;
            cell_state.depth = 0;
            cell_state.align = i < state.table_column_count
                                   ? state.table_column_align[i]
                                   : state.table_default_align;
            cell_state.default_align = cell_state.align;
            cell_state.style.bold = cell_state.style.bold || header_row;
            if (header_row)
            {
                cell_state.style.fg = kTerminalAmber;
                cell_state.style.bg = row_bg;
            }
            apply_micron_row_align(cell, cell_state.align);
            render_micron_inline(cell, cell_text, cell_state, tiny_font());
            state.rendered_objects = cell_state.rendered_objects;
            state.unsupported_unknown_tags = cell_state.unsupported_unknown_tags;
            state.unsupported_inline_blocks = cell_state.unsupported_inline_blocks;
            state.unsupported_block_count = cell_state.unsupported_block_count;
            state.unsupported_partial_count = cell_state.unsupported_partial_count;
            state.unsupported_resource_links = cell_state.unsupported_resource_links;
            state.unsupported_unknown_links = cell_state.unsupported_unknown_links;
            state.unsupported_tag_samples = cell_state.unsupported_tag_samples;
            state.unsupported_tag_sample_count = cell_state.unsupported_tag_sample_count;
            state.object_limit_reached =
                state.object_limit_reached || cell_state.object_limit_reached;
            state.link_limit_reached =
                state.link_limit_reached || cell_state.link_limit_reached;
            state.saw_fields = state.saw_fields || cell_state.saw_fields;
            state.saw_submit_links =
                state.saw_submit_links || cell_state.saw_submit_links;
        }
        ++state.table_row_index;
        return;
    }

    lv_obj_t* row = create_micron_row(state, true, state.style.bg);
    apply_micron_table_width(row, state);
    render_micron_inline(row, row_text ? row_text : "", state, tiny_font());
}

void parse_micron_header_colors(const char* body,
                                std::size_t body_len,
                                uint32_t* fg,
                                uint32_t* bg)
{
    if (!body || !fg || !bg)
    {
        return;
    }
    const char* cursor = body;
    const char* end = body + body_len;
    while (cursor < end)
    {
        const char* line_start = cursor;
        while (cursor < end && *cursor != '\n')
        {
            ++cursor;
        }
        const char* line_end = cursor;
        while (line_start < line_end &&
               (*line_start == ' ' || *line_start == '\t' || *line_start == '\r'))
        {
            ++line_start;
        }
        while (line_end > line_start &&
               (line_end[-1] == ' ' || line_end[-1] == '\t' ||
                line_end[-1] == '\r'))
        {
            --line_end;
        }
        if (line_start == line_end)
        {
            if (cursor < end)
            {
                ++cursor;
            }
            continue;
        }
        if (line_end - line_start < 2 || line_start[0] != '#' ||
            line_start[1] != '!')
        {
            break;
        }
        if (line_end - line_start >= 6 &&
            std::strncmp(line_start, "#!fg=", 5) == 0)
        {
            (void)micron::parse_color_text(
                line_start + 5,
                static_cast<std::size_t>(line_end - line_start - 5),
                fg);
        }
        else if (line_end - line_start >= 6 &&
                 std::strncmp(line_start, "#!bg=", 5) == 0)
        {
            (void)micron::parse_color_text(
                line_start + 5,
                static_cast<std::size_t>(line_end - line_start - 5),
                bg);
        }
        if (cursor < end)
        {
            ++cursor;
        }
    }
}

void add_micron_notice(const char* text)
{
    add_terminal_label(text, kTerminalDim, tiny_font(), LV_LABEL_LONG_WRAP);
}

void render_micron_notices(const MicronRenderState& state)
{
    const bool has_notice =
        state.link_limit_reached || state.saw_fields || state.saw_submit_links ||
        state.unsupported_partial_count > 0 || state.unsupported_resource_links > 0 ||
        state.unsupported_unknown_links > 0 || state.unsupported_inline_blocks > 0 ||
        state.unsupported_block_count > 0 || state.unsupported_unknown_tags > 0 ||
        state.row_limit_reached || state.object_limit_reached;
    if (!has_notice)
    {
        return;
    }

    add_terminal_spacer(4);
    if (state.link_limit_reached)
    {
        add_micron_notice("link limit reached");
    }
    if (state.saw_fields)
    {
        add_micron_notice("editable form fields are limited to 12");
    }
    if (state.saw_submit_links)
    {
        add_micron_notice("form submission is limited to 15 values / 256 bytes");
    }
    if (state.unsupported_resource_links > 0)
    {
        char line[64] = {};
        std::snprintf(line,
                      sizeof(line),
                      "unsupported resource links: %u",
                      static_cast<unsigned>(state.unsupported_resource_links));
        add_micron_notice(line);
    }
    if (state.unsupported_unknown_links > 0)
    {
        char line[64] = {};
        std::snprintf(line,
                      sizeof(line),
                      "unsupported links: %u",
                      static_cast<unsigned>(state.unsupported_unknown_links));
        add_micron_notice(line);
    }
    if (state.unsupported_partial_count > 0)
    {
        char line[64] = {};
        std::snprintf(line,
                      sizeof(line),
                      "unsupported partials: %u",
                      static_cast<unsigned>(state.unsupported_partial_count));
        add_micron_notice(line);
    }
    const uint16_t unsupported_blocks = static_cast<uint16_t>(
        state.unsupported_inline_blocks + state.unsupported_block_count);
    if (unsupported_blocks > 0)
    {
        char line[64] = {};
        std::snprintf(line,
                      sizeof(line),
                      "unsupported Micron blocks: %u",
                      static_cast<unsigned>(unsupported_blocks));
        add_micron_notice(line);
    }
    if (state.unsupported_unknown_tags > 0)
    {
        char line[96] = {};
        std::snprintf(line,
                      sizeof(line),
                      "unsupported Micron tags: %u",
                      static_cast<unsigned>(state.unsupported_unknown_tags));
        if (state.unsupported_tag_sample_count > 0)
        {
            std::size_t used = std::strlen(line);
            std::snprintf(line + used, sizeof(line) - used, " (");
            used = std::strlen(line);
            for (uint8_t i = 0; i < state.unsupported_tag_sample_count; ++i)
            {
                std::snprintf(line + used,
                              sizeof(line) - used,
                              "%s%s",
                              i == 0 ? "" : ", ",
                              state.unsupported_tag_samples[i].data());
                used = std::strlen(line);
            }
            std::snprintf(line + used, sizeof(line) - used, ")");
        }
        add_micron_notice(line);
    }
    if (state.row_limit_reached || state.object_limit_reached)
    {
        add_micron_notice("Micron page truncated by device render limit");
    }
}

void render_micron_body(char* body, std::size_t body_len, bool truncated)
{
    if (!body)
    {
        return;
    }
    if (body_len == 0)
    {
        add_terminal_label("empty page", kTerminalDim, caption_font(), LV_LABEL_LONG_WRAP);
        return;
    }

    clear_micron_anchor_registry();
    MicronRenderState state{};
    state.default_fg = kMicronDefaultFg;
    state.default_bg = kTerminalBg;
    parse_micron_header_colors(body, body_len, &state.default_fg, &state.default_bg);
    state.style.fg = state.default_fg;
    state.style.bg = state.default_bg;
    lv_obj_set_style_bg_color(g_state.viewport, lv_color_hex(state.default_bg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_state.viewport, LV_OPA_COVER, LV_PART_MAIN);

    char* line = body;
    char* cursor = body;
    char* end = body + body_len;
    while (cursor <= end)
    {
        if (cursor == end || *cursor == '\n')
        {
            const char saved = cursor == end ? '\0' : *cursor;
            *cursor = '\0';
            const std::size_t len = std::strlen(line);
            if (len != 0 && line[len - 1U] == '\r')
            {
                line[len - 1U] = '\0';
            }
            const micron::TableOpenOptions table_options =
                micron::parse_table_opening_tag(line);
            if (!state.literal && table_options.valid)
            {
                if (state.table_mode)
                {
                    finish_micron_table(state);
                }
                else
                {
                    begin_micron_table(state, table_options);
                }
            }
            else if (state.table_mode)
            {
                render_micron_table_row(state, line);
            }
            else
            {
                render_micron_line(line, state);
            }
            if (state.row_limit_reached || state.object_limit_reached)
            {
                break;
            }
            if (saved == '\0')
            {
                break;
            }
            line = cursor + 1;
        }
        ++cursor;
    }

    if (state.table_mode)
    {
        finish_micron_table(state);
    }

    render_micron_notices(state);

    if (truncated)
    {
        add_terminal_spacer(4);
        add_terminal_label("page truncated", kTerminalDim, caption_font(), LV_LABEL_LONG_WRAP);
    }
}

void render_home_page()
{
    clear_viewport();
    g_state.rendered_shell_address[0] = '\0';
    add_terminal_label("Nomad Network", kTerminalAmber, body_font(), LV_LABEL_LONG_WRAP);
    add_terminal_spacer();
    char line[96] = {};
    std::snprintf(line,
                  sizeof(line),
                  "nodes %u  favourites %u",
                  static_cast<unsigned>(visible_announce_count()),
                  static_cast<unsigned>(favourite_count()));
    add_terminal_label(line, kTerminalText, caption_font(), LV_LABEL_LONG_WRAP);

    const bool active = reticulum_active();
    add_terminal_label(active ? "reticulum active" : "reticulum inactive",
                       active ? kTerminalGreen : kTerminalDim,
                       caption_font(),
                       LV_LABEL_LONG_WRAP);
    if (!g_state.announce_status.sd_present && !g_state.address_status.sd_present)
    {
        add_terminal_label("sd unavailable", kTerminalDim, caption_font(), LV_LABEL_LONG_WRAP);
    }
    add_terminal_spacer(6);
    add_terminal_link("home:/announces", "Nodes");
    add_terminal_link("home:/favourites", "Favourites");

    if (const rtdir::AnnounceRecord* latest = first_visible_announce())
    {
        char dest[kHashTextLen] = {};
        format_hash_hex(latest->destination_hash, dest, sizeof(dest));
        char target[kAddressTextLen] = {};
        std::snprintf(target, sizeof(target), "%s:/page/index.mu", dest);
        const char* label = announce_display_label(*latest, "Latest announce");
        add_terminal_link(target, label);
    }
}

void render_collection_page(bool favourites)
{
    clear_viewport();
    g_state.rendered_shell_address[0] = '\0';
    add_terminal_label(favourites ? "Favourites" : "Nodes",
                       kTerminalAmber,
                       body_font(),
                       LV_LABEL_LONG_WRAP);
    add_terminal_spacer();
    if (favourites)
    {
        std::size_t visible = 0;
        for (const auto& favourite : kBuiltInFavourites)
        {
            add_terminal_link(favourite.address, favourite.title);
            ++visible;
        }
        for (std::size_t i = 0; i < g_state.address_count && visible < 12; ++i)
        {
            const auto& address = g_state.addresses[i];
            if (!address.valid || !address.favorite)
            {
                continue;
            }
            char dest[kHashTextLen] = {};
            char target[kAddressTextLen] = {};
            format_hash_hex(address.destination_hash, dest, sizeof(dest));
            std::snprintf(target, sizeof(target), "%s:/page/index.mu", dest);
            add_terminal_link(target, address_display_label(address, "Anonymous Peer"));
            ++visible;
        }
        if (visible == 0)
        {
            add_terminal_label("no favourites", kTerminalDim, caption_font());
        }
        return;
    }

    std::size_t visible = 0;
    for (std::size_t i = 0; i < g_state.announce_count && visible < 16; ++i)
    {
        const auto& announce = g_state.announces[i];
        if (!announce_visible_in_directory(announce))
        {
            continue;
        }
        char dest[kHashTextLen] = {};
        char target[kAddressTextLen] = {};
        format_hash_hex(announce.destination_hash, dest, sizeof(dest));
        std::snprintf(target, sizeof(target), "%s:/page/index.mu", dest);
        add_terminal_link(target, announce_display_label(announce, dest));
        ++visible;
    }
    if (visible == 0)
    {
        add_terminal_label("no Nomad nodes", kTerminalDim, caption_font());
    }
}

void render_remote_page_shell(const char* address,
                              const RemotePageAddress* page_address,
                              const rtdir::AnnounceRecord* announce,
                              const rtdir::LxmfAddressRecord* address_record,
                              const rtpage::Status* cache_status,
                              const rtpage::Status* request_status,
                              const rtpage::RequestProgress* progress)
{
    if (g_state.cancel_btn)
    {
        if (progress && progress->active)
        {
            lv_obj_clear_flag(g_state.cancel_btn, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(g_state.cancel_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
    const bool preserve_scroll =
        address && address[0] != '\0' &&
        std::strcmp(g_state.rendered_shell_address, address) == 0;
    const lv_coord_t scroll_y =
        preserve_scroll && g_state.viewport ? lv_obj_get_scroll_y(g_state.viewport) : 0;
    clear_viewport();
    copy_text(g_state.rendered_shell_address,
              sizeof(g_state.rendered_shell_address),
              address ? address : "");
    const char* title = "Nomad Page";
    if (address_record)
    {
        title = address_display_label(*address_record, title);
    }
    else if (announce)
    {
        title = announce_display_label(*announce, title);
    }
    add_terminal_label(title, kTerminalAmber, body_font(), LV_LABEL_LONG_WRAP);
    add_terminal_spacer();

    char status_line[128] = {};
    uint32_t status_color = kTerminalDim;
    if (progress && request_progress_terminal_failed(*progress))
    {
        std::snprintf(status_line,
                      sizeof(status_line),
                      "%s",
                      progress->message[0] != '\0'
                          ? progress->message
                          : "Nomad page request failed");
        status_color = kTerminalAmber;
    }
    else if (progress && request_progress_retryable_failed(*progress))
    {
        std::snprintf(status_line,
                      sizeof(status_line),
                      "%s",
                      progress->message[0] != '\0'
                          ? progress->message
                          : "Retrying Nomad page request");
    }
    else if (progress && (progress->active || progress->complete ||
                          progress->message[0] != '\0'))
    {
        if (progress->progress_percent >= 0)
        {
            std::snprintf(status_line,
                          sizeof(status_line),
                          "%s %d%%",
                          progress->message[0] != '\0'
                              ? progress->message
                              : "Loading Nomad page",
                          progress->progress_percent);
        }
        else
        {
            std::snprintf(status_line,
                          sizeof(status_line),
                          "%s",
                          progress->message[0] != '\0'
                              ? progress->message
                              : "Loading Nomad page");
        }
    }
    else if (cache_status && cache_status->busy)
    {
        if (cache_status->progress_percent >= 0)
        {
            std::snprintf(status_line,
                          sizeof(status_line),
                          "Checking cache %d%%",
                          cache_status->progress_percent);
        }
        else
        {
            copy_text(status_line, sizeof(status_line), "Checking cache");
        }
    }
    else if (request_status && request_status->message[0] != '\0')
    {
        copy_text(status_line, sizeof(status_line), request_status->message);
    }
    else if (cache_status && cache_status->cache_checked &&
             !cache_status->loaded)
    {
        copy_text(status_line, sizeof(status_line), "Waiting for Nomad page");
    }
    else
    {
        copy_text(status_line, sizeof(status_line), "Opening Nomad page");
    }

    add_terminal_label(status_line, status_color, caption_font(), LV_LABEL_LONG_WRAP);
    if (preserve_scroll && g_state.viewport && scroll_y > 0)
    {
        lv_obj_update_layout(g_state.viewport);
        lv_obj_scroll_to_y(g_state.viewport, scroll_y, LV_ANIM_OFF);
    }
}

void jump_to_rendered_micron_anchor(const char* anchor)
{
    if (!anchor || anchor[0] == '\0')
    {
        return;
    }
    char target[micron::kMaxAnchorNameBytes + 2U] = {};
    std::snprintf(target, sizeof(target), "#%s", anchor);
    if (g_state.viewport && lv_obj_is_valid(g_state.viewport))
    {
        lv_obj_update_layout(g_state.viewport);
    }
    (void)jump_to_micron_anchor(target, nullptr);
}

void render_cached_page(const rtpage::Status& status,
                        std::size_t body_len,
                        const char* anchor = nullptr)
{
    clear_viewport();
    g_state.rendered_shell_address[0] = '\0';
    const std::size_t render_len =
        body_len < g_state.page_body.size() ? body_len : g_state.page_body.size() - 1U;
    g_state.page_body[render_len] = '\0';
    (void)::ui::i18n::prepare_content_font_for_text(g_state.page_body.data(), true);
    render_micron_body(g_state.page_body.data(), render_len, status.truncated);
    jump_to_rendered_micron_anchor(anchor);
}

void page_load_timer_cb(lv_timer_t* /*timer*/)
{
    if (!g_state.root || !lv_obj_is_valid(g_state.root))
    {
        stop_page_load_timer();
        return;
    }
    render_current_page();
}

void render_current_page()
{
    if (!g_state.viewport)
    {
        return;
    }
    if (g_state.cancel_btn)
    {
        lv_obj_add_flag(g_state.cancel_btn, LV_OBJ_FLAG_HIDDEN);
    }

    const char* address = g_state.current_address;
    NETWORK_PAGE_LOG("render begin address=%s\n", log_text(address));
    if (!address || address[0] == '\0' || std::strcmp(address, "home:/") == 0)
    {
        stop_page_load_timer();
        NETWORK_PAGE_LOG("render route=home announces=%lu favourites=%lu reticulum=%u\n",
                         static_cast<unsigned long>(visible_announce_count()),
                         static_cast<unsigned long>(favourite_count()),
                         reticulum_active() ? 1U : 0U);
        render_home_page();
        return;
    }
    if (std::strcmp(address, "home:/announces") == 0)
    {
        stop_page_load_timer();
        NETWORK_PAGE_LOG("render route=collection type=announces\n");
        render_collection_page(false);
        return;
    }
    if (std::strcmp(address, "home:/favourites") == 0)
    {
        stop_page_load_timer();
        NETWORK_PAGE_LOG("render route=collection type=favourites\n");
        render_collection_page(true);
        return;
    }

    char destination[kHashTextLen] = {};
    if (extract_destination_text(address, destination, sizeof(destination)))
    {
        RemotePageAddress page_address{};
        const auto* announce =
            find_announce_by_destination_text(destination);
        const auto* known_address =
            find_address_by_destination_text(destination);
        NETWORK_PAGE_LOG("address parsed address=%s dest=%s announce=%u contact=%u\n",
                         address,
                         destination,
                         announce ? 1U : 0U,
                         known_address ? 1U : 0U);
        if (!parse_remote_page_address(address, page_address))
        {
            NETWORK_PAGE_LOG("parse failed address=%s dest=%s\n", address, destination);
            rtpage::Status invalid_status{};
            copy_text(invalid_status.message,
                      sizeof(invalid_status.message),
                      "Invalid Nomad page path");
            copy_text(invalid_status.detail, sizeof(invalid_status.detail), address);
            render_remote_page_shell(address,
                                     nullptr,
                                     announce,
                                     known_address,
                                     &invalid_status,
                                     nullptr,
                                     nullptr);
            return;
        }

        std::size_t body_len = 0;
        if (cached_page_matches(page_address))
        {
            NETWORK_PAGE_LOG("render cached-local dest=%s path=%s body=%lu truncated=%u\n",
                             page_address.destination,
                             page_address.path,
                             static_cast<unsigned long>(g_state.cached_page_body_len),
                             g_state.cached_page_status.truncated ? 1U : 0U);
            rtpage::clear_request_progress(page_address.destination,
                                           page_address.path);
            stop_page_load_timer();
            render_cached_page(g_state.cached_page_status,
                               g_state.cached_page_body_len,
                               page_address.anchor);
            return;
        }

        NETWORK_PAGE_LOG("cache poll begin dest=%s path=%s\n",
                         page_address.destination,
                         page_address.path);
        const rtpage::Status cache_status =
            rtpage::poll_cached_page_load(page_address.destination,
                                          page_address.path,
                                          g_state.page_body.data(),
                                          g_state.page_body.size(),
                                          &body_len);
        log_page_status("cache poll result",
                        page_address,
                        cache_status,
                        body_len,
                        0);
        if (cache_status.cache_checked && !cache_status.busy)
        {
            g_state.page_cache_load_requested = false;
        }
        if (cache_status.loaded)
        {
            g_state.cached_page_body_valid = true;
            g_state.page_cache_load_requested = false;
            g_state.cached_page_status = cache_status;
            g_state.cached_page_body_len = body_len;
            copy_text(g_state.cached_page_destination,
                      sizeof(g_state.cached_page_destination),
                      page_address.destination);
            copy_text(g_state.cached_page_path,
                      sizeof(g_state.cached_page_path),
                      page_address.path);
            NETWORK_PAGE_LOG("render cached-async dest=%s path=%s body=%lu truncated=%u\n",
                             page_address.destination,
                             page_address.path,
                             static_cast<unsigned long>(body_len),
                             cache_status.truncated ? 1U : 0U);
            rtpage::clear_request_progress(page_address.destination,
                                           page_address.path);
            stop_page_load_timer();
            render_cached_page(cache_status, body_len, page_address.anchor);
            return;
        }

        rtpage::Status active_cache_status = cache_status;
        rtpage::Status request_status{};
        rtpage::RequestProgress progress =
            rtpage::get_request_progress(page_address.destination,
                                         page_address.path);
        bool cache_poll_requested = false;

        if (!cache_status.cache_checked && !cache_status.busy &&
            !g_state.page_cache_load_requested)
        {
            NETWORK_PAGE_LOG("cache async request dest=%s path=%s\n",
                             page_address.destination,
                             page_address.path);
            active_cache_status =
                rtpage::request_cached_page_load(page_address.destination,
                                                 page_address.path);
            g_state.page_cache_load_requested = active_cache_status.busy;
            log_page_status("cache async request result",
                            page_address,
                            active_cache_status,
                            0,
                            0);
            cache_poll_requested = active_cache_status.busy;
        }
        else if (cache_status.busy)
        {
            cache_poll_requested = true;
        }

        if (active_cache_status.cache_checked && !active_cache_status.loaded &&
            !progress.active && !progress.complete &&
            progress.failure == rtpage::RequestProgress::FailureKind::None)
        {
            NETWORK_PAGE_LOG("request begin dest=%s path=%s\n",
                             page_address.destination,
                             page_address.path);
            const uint32_t request_started_ms = lv_tick_get();
            request_status =
                rtpage::request_page(page_address.destination, page_address.path);
            log_page_status("request result",
                            page_address,
                            request_status,
                            0,
                            lv_tick_elaps(request_started_ms));
            progress = rtpage::get_request_progress(page_address.destination,
                                                    page_address.path);
        }

        if (progress.complete && !g_state.page_cache_load_requested)
        {
            active_cache_status =
                rtpage::request_cached_page_load(page_address.destination,
                                                 page_address.path,
                                                 true);
            g_state.page_cache_load_requested = active_cache_status.busy;
            cache_poll_requested = cache_poll_requested ||
                                   active_cache_status.busy;
        }

        const bool progress_needs_poll =
            progress.active || progress.complete ||
            request_progress_retryable_failed(progress);
        const bool terminal_failed =
            request_progress_terminal_failed(progress);
        const bool should_poll =
            cache_poll_requested || g_state.page_cache_load_requested ||
            request_status.request_started || progress_needs_poll;
        if (terminal_failed && !cache_poll_requested &&
            !g_state.page_cache_load_requested)
        {
            stop_page_load_timer();
        }
        else if (should_poll)
        {
            ensure_page_load_timer();
        }
        else
        {
            stop_page_load_timer();
        }

        NETWORK_PAGE_LOG("render shell reason=cache_miss dest=%s path=%s\n",
                         page_address.destination,
                         page_address.path);
        render_remote_page_shell(address,
                                 &page_address,
                                 announce,
                                 known_address,
                                 &active_cache_status,
                                 request_status.message[0] != '\0' ? &request_status : nullptr,
                                 &progress);
        return;
    }

    stop_page_load_timer();
    NETWORK_PAGE_LOG("render shell reason=unrecognized_address address=%s\n", address);
    render_remote_page_shell(address, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
}

void refresh_directory_data()
{
    if (g_state.announces.capacity() < kMaxVisibleAnnounces)
    {
        g_state.announces.reserve(kMaxVisibleAnnounces);
    }
    if (g_state.addresses.capacity() < kMaxVisibleAddresses)
    {
        g_state.addresses.reserve(kMaxVisibleAddresses);
    }

    std::size_t count = 0;
    g_state.announces.resize(kMaxVisibleAnnounces);
    g_state.announce_status =
        rtdir::load_announces(g_state.announces.data(), g_state.announces.size(), &count);
    g_state.announce_count = count <= g_state.announces.size() ? count : g_state.announces.size();
    g_state.announces.resize(g_state.announce_count);

    count = 0;
    g_state.addresses.resize(kMaxVisibleAddresses);
    g_state.address_status =
        rtdir::load_lxmf_addresses(g_state.addresses.data(), g_state.addresses.size(), &count);
    g_state.address_count = count <= g_state.addresses.size() ? count : g_state.addresses.size();
    g_state.addresses.resize(g_state.address_count);
}

void update_tab_state()
{
    if (!g_state.favourites_tab || !g_state.announces_tab)
    {
        return;
    }
    if (g_state.directory_mode == DirectoryMode::Favourites)
    {
        lv_obj_add_state(g_state.favourites_tab, LV_STATE_CHECKED);
        lv_obj_clear_state(g_state.announces_tab, LV_STATE_CHECKED);
    }
    else
    {
        lv_obj_clear_state(g_state.favourites_tab, LV_STATE_CHECKED);
        lv_obj_add_state(g_state.announces_tab, LV_STATE_CHECKED);
    }
}

void set_directory_mode(DirectoryMode mode)
{
    if (g_state.directory_mode == mode)
    {
        return;
    }
    g_state.directory_mode = mode;
    update_tab_state();
    render_directory_list();
}

void directory_tab_event_cb(lv_event_t* event)
{
    if (event && lv_event_get_code(event) == LV_EVENT_KEY &&
        lv_event_get_key(event) == LV_KEY_RIGHT)
    {
        focus_browser_viewport();
        lv_event_stop_processing(event);
        return;
    }
    if (!activation_event(event))
    {
        return;
    }
    const auto raw = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
    set_directory_mode(raw == 0 ? DirectoryMode::Favourites : DirectoryMode::Announces);
    lv_event_stop_processing(event);
}

void open_directory_row(const DirectoryRowContext& context)
{
    if (context.kind == DirectoryRowKind::BuiltInFavourite)
    {
        if (context.index < (sizeof(kBuiltInFavourites) / sizeof(kBuiltInFavourites[0])))
        {
            navigate_to_address(kBuiltInFavourites[context.index].address, true);
        }
        return;
    }

    char destination[kHashTextLen] = {};
    if (context.kind == DirectoryRowKind::Favourite)
    {
        if (context.index >= g_state.address_count)
        {
            return;
        }
        format_hash_hex(g_state.addresses[context.index].destination_hash,
                        destination,
                        sizeof(destination));
    }
    else
    {
        if (context.index >= g_state.announce_count)
        {
            return;
        }
        format_hash_hex(g_state.announces[context.index].destination_hash,
                        destination,
                        sizeof(destination));
    }

    char address[kAddressTextLen] = {};
    std::snprintf(address, sizeof(address), "%s:/page/index.mu", destination);
    navigate_to_address(address, true);
}

void directory_row_event_cb(lv_event_t* event)
{
    if (!event)
    {
        return;
    }
    if (lv_event_get_code(event) == LV_EVENT_KEY &&
        lv_event_get_key(event) == LV_KEY_RIGHT)
    {
        focus_browser_viewport();
        lv_event_stop_processing(event);
        return;
    }
    if (lv_event_get_code(event) == LV_EVENT_FOCUSED)
    {
        lv_obj_t* target = lv_event_get_target_obj(event);
        if (target && lv_obj_is_valid(target))
        {
            lv_obj_scroll_to_view(target, LV_ANIM_ON);
        }
        return;
    }
    if (!activation_event(event))
    {
        return;
    }
    auto* context = static_cast<DirectoryRowContext*>(lv_event_get_user_data(event));
    if (context)
    {
        open_directory_row(*context);
    }
    lv_event_stop_processing(event);
}

lv_obj_t* create_directory_row(const char* title,
                               const char* meta,
                               const char* icon,
                               DirectoryRowKind kind,
                               std::size_t index)
{
    if (!g_state.directory_list || g_state.row_context_count >= g_state.row_contexts.size())
    {
        return nullptr;
    }

    DirectoryRowContext& context = g_state.row_contexts[g_state.row_context_count++];
    context.kind = kind;
    context.index = index;

    lv_obj_t* row = lv_btn_create(g_state.directory_list);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(row, lv_color_hex(kPanelBg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(row, lv_color_hex(kAmber), LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(row, lv_color_hex(kAmber), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(row, lv_color_hex(kLine), LV_PART_MAIN);
    lv_obj_set_style_radius(row, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, ::ui::page_profile::current().dense ? 4 : 6, LV_PART_MAIN);
    lv_obj_set_style_pad_column(row, 5, LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    style_focusable(row);
    lv_obj_add_event_cb(row, directory_row_event_cb, LV_EVENT_CLICKED, &context);
    lv_obj_add_event_cb(row, directory_row_event_cb, LV_EVENT_KEY, &context);
    lv_obj_add_event_cb(row, directory_row_event_cb, LV_EVENT_FOCUSED, &context);
    lv_obj_add_event_cb(row, page_shortcut_event_cb, LV_EVENT_KEY, nullptr);
    add_to_group(row);

    lv_obj_t* icon_label = lv_label_create(row);
    set_label(icon_label, icon ? icon : "*", kTextDim, caption_font());
    lv_obj_set_width(icon_label, 16);
    lv_label_set_long_mode(icon_label, LV_LABEL_LONG_CLIP);

    lv_obj_t* texts = lv_obj_create(row);
    lv_obj_set_width(texts, 0);
    lv_obj_set_height(texts, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(texts, 1);
    lv_obj_set_flex_flow(texts, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(texts, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(texts, 1, LV_PART_MAIN);
    style_plain_container(texts, kPanelBg, LV_OPA_TRANSP);
    ::ui::components::two_pane_layout::make_non_scrollable(texts);

    lv_obj_t* title_label = lv_label_create(texts);
    set_label(title_label, title ? title : "--", kText, caption_font());
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title_label, LV_PCT(100));

    lv_obj_t* meta_label = lv_label_create(texts);
    set_label(meta_label, meta ? meta : "", kTextDim, tiny_font());
    lv_label_set_long_mode(meta_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(meta_label, LV_PCT(100));
    return row;
}

void render_empty_directory(const char* message)
{
    lv_obj_t* label = lv_label_create(g_state.directory_list);
    set_label(label, safe_tr(message), kTextDim, caption_font());
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_pad_all(label, 6, LV_PART_MAIN);
}

void render_directory_list()
{
    if (!g_state.directory_list)
    {
        NETWORK_PAGE_LOG("directory render skipped reason=no_list\n");
        return;
    }
    lv_obj_clean(g_state.directory_list);
    g_state.row_context_count = 0;
    update_tab_state();

    std::size_t visible = 0;
    if (g_state.directory_mode == DirectoryMode::Favourites)
    {
        for (std::size_t i = 0; i < (sizeof(kBuiltInFavourites) / sizeof(kBuiltInFavourites[0]));
             ++i)
        {
            const auto& favourite = kBuiltInFavourites[i];
            if (!built_in_favourite_matches_search(favourite))
            {
                continue;
            }
            create_directory_row(favourite.title,
                                 favourite.meta,
                                 favourite.icon,
                                 DirectoryRowKind::BuiltInFavourite,
                                 i);
            ++visible;
        }
        for (std::size_t i = 0; i < g_state.address_count; ++i)
        {
            const auto& address = g_state.addresses[i];
            if (!address.valid || !address.favorite || !address_matches_search(address))
            {
                continue;
            }
            char destination[10] = {};
            format_hash_prefix(address.destination_hash, destination, sizeof(destination));
            char meta[64] = {};
            std::snprintf(meta, sizeof(meta), "%s  LXMF", destination);
            create_directory_row(address_display_label(address, "Anonymous Peer"),
                                 meta,
                                 LV_SYMBOL_STAR,
                                 DirectoryRowKind::Favourite,
                                 i);
            ++visible;
        }
    }
    else
    {
        for (std::size_t i = 0; i < g_state.announce_count; ++i)
        {
            const auto& announce = g_state.announces[i];
            if (!announce_visible_in_directory(announce) ||
                !announce_matches_search(announce))
            {
                continue;
            }
            char destination[10] = {};
            format_hash_prefix(announce.destination_hash, destination, sizeof(destination));
            char meta[72] = {};
            std::snprintf(meta,
                          sizeof(meta),
                          "%s  %s  %uh",
                          destination,
                          aspect_label(announce.aspect),
                          static_cast<unsigned>(announce.hops));
            create_directory_row(announce_display_label(announce, destination),
                                 meta,
                                 "A",
                                 DirectoryRowKind::Announce,
                                 i);
            ++visible;
        }
    }

    NETWORK_PAGE_LOG(
        "directory render mode=%s total=%lu visible=%lu search=\"%s\" announce_status=\"%s\" address_status=\"%s\"\n",
        directory_mode_label(g_state.directory_mode),
        static_cast<unsigned long>(g_state.directory_mode == DirectoryMode::Favourites
                                       ? favourite_count()
                                       : g_state.announce_count),
        static_cast<unsigned long>(visible),
        g_state.search_query,
        g_state.announce_status.message,
        g_state.address_status.message);

    if (visible == 0)
    {
        render_empty_directory(g_state.search_query[0] != '\0'
                                   ? "No matches"
                                   : (g_state.directory_mode == DirectoryMode::Favourites
                                          ? "No favourites"
                                          : "No Nomad nodes"));
    }
    rebuild_focus_group(nullptr);
}

void refresh_all(bool force_current_page_reload = false)
{
    refresh_directory_data();
    render_directory_list();
    if (force_current_page_reload)
    {
        clear_current_remote_page_cache();
    }
    render_current_page();
    rebuild_focus_group(nullptr);
}

void browser_back_event_cb(lv_event_t* event)
{
    if (!activation_event(event))
    {
        return;
    }
    if (g_state.history_count > 1 && g_state.history_pos > 0)
    {
        --g_state.history_pos;
        g_state.suppress_history = true;
        navigate_to_address(g_state.history[g_state.history_pos].address, false);
        g_state.suppress_history = false;
    }
    lv_event_stop_processing(event);
}

void browser_forward_event_cb(lv_event_t* event)
{
    if (!activation_event(event))
    {
        return;
    }
    if (g_state.history_pos + 1U < g_state.history_count)
    {
        ++g_state.history_pos;
        g_state.suppress_history = true;
        navigate_to_address(g_state.history[g_state.history_pos].address, false);
        g_state.suppress_history = false;
    }
    lv_event_stop_processing(event);
}

void home_event_cb(lv_event_t* event)
{
    if (!activation_event(event))
    {
        return;
    }
    navigate_to_address("home:/", true);
    lv_event_stop_processing(event);
}

void refresh_event_cb(lv_event_t* event)
{
    if (!activation_event(event))
    {
        return;
    }
    refresh_all(true);
    ::ui::feedback::show_notice(safe_tr("Network refreshed"), 1200);
    lv_event_stop_processing(event);
}

void cancel_request_event_cb(lv_event_t* event)
{
    if (!activation_event(event))
    {
        return;
    }
    RemotePageAddress page_address{};
    if (parse_remote_page_address(g_state.current_address, page_address))
    {
        const rtpage::Status status = rtpage::cancel_request(
            page_address.destination, page_address.path);
        ::ui::feedback::show_notice(
            safe_tr(status.message[0] != '\0' ? status.message
                                              : "No active page request"),
            1600);
        stop_page_load_timer();
        render_current_page();
    }
    lv_event_stop_processing(event);
}

void go_from_address_bar()
{
    const char* text = g_state.address_area ? lv_textarea_get_text(g_state.address_area) : nullptr;
    navigate_to_address(text && text[0] != '\0' ? text : "home:/", true);
}

void go_event_cb(lv_event_t* event)
{
    if (!activation_event(event))
    {
        return;
    }
    go_from_address_bar();
    lv_event_stop_processing(event);
}

void directory_rail_event_cb(lv_event_t* event)
{
    if (event && lv_event_get_code(event) == LV_EVENT_KEY &&
        lv_event_get_key(event) == LV_KEY_LEFT)
    {
        focus_browser_viewport();
        lv_event_stop_processing(event);
        return;
    }
    if (!activation_event(event))
    {
        return;
    }
    open_node_list_page();
    lv_event_stop_processing(event);
}

void address_key_event_cb(lv_event_t* event)
{
    if (!event || lv_event_get_code(event) != LV_EVENT_KEY)
    {
        return;
    }
    const uint32_t key = lv_event_get_key(event);
    if (key == LV_KEY_ENTER || key == LV_KEY_RIGHT)
    {
        go_from_address_bar();
        lv_event_stop_processing(event);
    }
    else if (key == LV_KEY_ESC && app_g)
    {
        lv_group_focus_obj(g_state.viewport ? g_state.viewport : g_state.root);
        lv_event_stop_processing(event);
    }
}

void on_search_apply(const char* text, void* /*user_data*/)
{
    copy_text(g_state.search_query, sizeof(g_state.search_query), text ? text : "");
    render_directory_list();
}

void on_search_clear(void* /*user_data*/)
{
    g_state.search_query[0] = '\0';
    render_directory_list();
}

void on_search_cancel(void* /*user_data*/)
{
}

void open_search_modal()
{
    if (::ui::components::floating_search_box::is_open(g_state.search_box))
    {
        ::ui::components::floating_search_box::focus(g_state.search_box);
        return;
    }
    if (::ui::components::shortcut_help_modal::is_open(g_state.help_modal))
    {
        return;
    }

    ::ui::components::floating_search_box::Config config{};
    config.title = "Search";
    config.initial_text = g_state.search_query;
    config.max_length = static_cast<uint16_t>(sizeof(g_state.search_query) - 1U);
    config.height = ::ui::page_profile::current().dense ? 122 : 150;
    config.restore_group = app_g;
    config.callbacks.apply = on_search_apply;
    config.callbacks.clear = on_search_clear;
    config.callbacks.cancel = on_search_cancel;
    (void)::ui::components::floating_search_box::open(
        g_state.search_box,
        g_state.root ? g_state.root : lv_scr_act(),
        config);
}

void search_button_event_cb(lv_event_t* event)
{
    if (event && lv_event_get_code(event) == LV_EVENT_KEY)
    {
        const uint32_t key = lv_event_get_key(event);
        lv_obj_t* target = lv_event_get_target_obj(event);
        if ((key == LV_KEY_RIGHT && object_in_subtree(g_state.directory_panel, target)) ||
            (key == LV_KEY_LEFT && object_in_subtree(g_state.browser_rail, target)))
        {
            focus_browser_viewport();
            lv_event_stop_processing(event);
            return;
        }
    }
    if (!activation_event(event))
    {
        return;
    }
    open_search_modal();
    lv_event_stop_processing(event);
}

void apply_layout_state()
{
    const bool immersive = g_state.immersive;
    const bool hide_directory = immersive || g_state.directory_collapsed;
    const bool hide_browser = !g_state.menu.button() && !immersive && g_state.browser_collapsed;
    if (g_state.header)
    {
        immersive ? lv_obj_add_flag(g_state.header, LV_OBJ_FLAG_HIDDEN)
                  : lv_obj_clear_flag(g_state.header, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_state.directory_panel && !g_state.menu.button())
    {
        hide_directory ? lv_obj_add_flag(g_state.directory_panel, LV_OBJ_FLAG_HIDDEN)
                       : lv_obj_clear_flag(g_state.directory_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_width(g_state.directory_panel,
                         hide_browser ? LV_PCT(100) : resolve_directory_width());
        lv_obj_set_flex_grow(g_state.directory_panel, hide_browser ? 1 : 0);
    }
    if (g_state.browser_panel)
    {
        hide_browser ? lv_obj_add_flag(g_state.browser_panel, LV_OBJ_FLAG_HIDDEN)
                     : lv_obj_clear_flag(g_state.browser_panel, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_state.bottom_bar)
    {
        immersive ? lv_obj_add_flag(g_state.bottom_bar, LV_OBJ_FLAG_HIDDEN)
                  : lv_obj_clear_flag(g_state.bottom_bar, LV_OBJ_FLAG_HIDDEN);
        if (immersive) g_state.menu.close();
    }
    if (g_state.browser_toolbar)
    {
        immersive ? lv_obj_add_flag(g_state.browser_toolbar, LV_OBJ_FLAG_HIDDEN)
                  : lv_obj_clear_flag(g_state.browser_toolbar, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_state.browser_rail)
    {
        immersive ? lv_obj_add_flag(g_state.browser_rail, LV_OBJ_FLAG_HIDDEN)
                  : lv_obj_clear_flag(g_state.browser_rail, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_state.content)
    {
        lv_obj_set_style_pad_left(g_state.content, immersive ? 0 : 2, LV_PART_MAIN);
        lv_obj_set_style_pad_right(g_state.content, immersive ? 0 : 2, LV_PART_MAIN);
        lv_obj_set_style_pad_top(g_state.content, immersive ? 0 : 2, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(g_state.content, immersive ? 0 : 2, LV_PART_MAIN);
        lv_obj_set_style_pad_column(g_state.content,
                                    (hide_directory || hide_browser) ? 0 : 4,
                                    LV_PART_MAIN);
    }
    if (g_state.browser_panel)
    {
        lv_obj_set_style_pad_all(g_state.browser_panel, immersive ? 0 : 2, LV_PART_MAIN);
        lv_obj_set_style_radius(g_state.browser_panel, immersive ? 0 : 8, LV_PART_MAIN);
        lv_obj_set_style_border_width(g_state.browser_panel, immersive ? 0 : 1, LV_PART_MAIN);
    }
    if (g_state.browser_body)
    {
        lv_obj_set_style_pad_column(g_state.browser_body, immersive ? 0 : 3, LV_PART_MAIN);
    }
    if (g_state.viewport)
    {
        lv_obj_set_style_radius(g_state.viewport, immersive ? 0 : 8, LV_PART_MAIN);
        lv_obj_set_style_border_width(g_state.viewport, immersive ? 0 : 1, LV_PART_MAIN);
        if (app_g && !hide_browser)
        {
            lv_group_focus_obj(g_state.viewport);
        }
    }
    rebuild_focus_group(hide_browser ? g_state.directory_list : g_state.viewport);
}

void toggle_immersive()
{
    g_state.immersive = !g_state.immersive;
    apply_layout_state();
}

void open_node_list_page()
{
    if (g_state.immersive)
    {
        g_state.immersive = false;
    }
    g_state.directory_collapsed = true;
    g_state.browser_collapsed = false;
    apply_layout_state();
    navigate_to_address("home:/announces", true);
}

void toggle_browser_panel()
{
    if (g_state.menu.button())
    {
        g_state.menu.toggle();
        return;
    }
    if (g_state.immersive)
    {
        g_state.immersive = false;
    }
    const bool hide_browser = !g_state.browser_collapsed;
    g_state.browser_collapsed = hide_browser;
    if (hide_browser)
    {
        g_state.directory_collapsed = false;
    }
    apply_layout_state();
    ::ui::feedback::show_notice(g_state.browser_collapsed ? safe_tr("Browser hidden")
                                                          : safe_tr("Browser shown"),
                                1200);
    if (g_state.browser_collapsed)
    {
        focus_directory_panel();
    }
    else
    {
        focus_browser_viewport();
    }
}

bool is_help_shortcut_key(uint32_t key)
{
    return key == 'h' || key == 'H';
}

bool is_directory_toggle_shortcut_key(uint32_t key)
{
    return key == 'c' || key == 'C';
}

bool is_browser_toggle_shortcut_key(uint32_t key)
{
    (void)key;
    return false;
}

void close_network_help_modal()
{
    ::ui::components::shortcut_help_modal::close(g_state.help_modal);
}

void open_network_help_modal()
{
    if (::ui::components::shortcut_help_modal::is_open(g_state.help_modal))
    {
        close_network_help_modal();
        return;
    }
    if (::ui::components::floating_search_box::is_open(g_state.search_box))
    {
        return;
    }

    ::ui::components::shortcut_help_modal::Row rows[10] = {};
    std::size_t row_count = 0;
    rows[row_count++] = {"S", "/", "Search announces"};
    rows[row_count++] = {"C", nullptr, "Nodes"};
    rows[row_count++] = {"I", nullptr, "Immersive browser"};
    rows[row_count++] = {"Rotary", nullptr, "Scroll page"};
    rows[row_count++] = {"Enter", nullptr, "Open link or lock page"};
    rows[row_count++] = {"Back", nullptr, "Unlock page or return"};
    rows[row_count++] = {"R", nullptr, "Refresh network"};
    rows[row_count++] = {"F", nullptr, "Favourites"};
    rows[row_count++] = {"A", nullptr, "Nodes"};
    rows[row_count++] = {"H", nullptr, "Close help"};

    ::ui::components::shortcut_help_modal::Config config{};
    config.title = "Network Help";
    config.rows = rows;
    config.row_count = row_count;
    config.width = 304;
    config.height = 176;
    config.restore_group = app_g;
    (void)::ui::components::shortcut_help_modal::open(
        g_state.help_modal,
        g_state.root ? g_state.root : lv_screen_active(),
        config);
}

lv_coord_t viewport_page_scroll_step()
{
    if (!g_state.viewport || !lv_obj_is_valid(g_state.viewport))
    {
        return kViewportScrollStep;
    }
    const lv_coord_t height = lv_obj_get_height(g_state.viewport);
    const lv_coord_t page_step = height - kViewportPageScrollPadding;
    return page_step > kViewportScrollStep ? page_step : kViewportScrollStep;
}

bool viewport_scroll_delta_for_key(uint32_t key, lv_coord_t* delta)
{
    if (!delta)
    {
        return false;
    }
    if (key == LV_KEY_UP || key == kPagerRotateUpKey)
    {
        *delta = -kViewportScrollStep;
        return true;
    }
    if (key == LV_KEY_DOWN || key == kPagerRotateDownKey)
    {
        *delta = kViewportScrollStep;
        return true;
    }
    if (key == LV_KEY_PREV)
    {
        *delta = -viewport_page_scroll_step();
        return true;
    }
    if (key == LV_KEY_NEXT)
    {
        *delta = viewport_page_scroll_step();
        return true;
    }
    return false;
}

bool target_in_viewport(lv_obj_t* target)
{
    return object_in_subtree(g_state.viewport, target);
}

void focus_locked_viewport()
{
    if (!app_g || !g_state.viewport || !lv_obj_is_valid(g_state.viewport))
    {
        return;
    }
    lv_group_focus_obj(g_state.viewport);
    lv_group_set_editing(app_g, true);
}

bool scroll_viewport_by_key(lv_event_t* event)
{
    if (!event || lv_event_get_code(event) != LV_EVENT_KEY || !g_state.viewport ||
        !lv_obj_is_valid(g_state.viewport))
    {
        return false;
    }
    lv_coord_t delta = 0;
    if (!viewport_scroll_delta_for_key(lv_event_get_key(event), &delta) || delta == 0)
    {
        return false;
    }
    lv_obj_scroll_by(g_state.viewport, 0, delta, LV_ANIM_OFF);
    lv_event_stop_bubbling(event);
    lv_event_stop_processing(event);
    return true;
}

bool route_key_to_viewport_scroll(lv_event_t* event)
{
    if (!event || lv_event_get_code(event) != LV_EVENT_KEY)
    {
        return false;
    }
    lv_coord_t delta = 0;
    const uint32_t key = lv_event_get_key(event);
    if (!viewport_scroll_delta_for_key(key, &delta))
    {
        return false;
    }
    lv_obj_t* target = lv_event_get_target_obj(event);
    if (!g_state.viewport_scroll_locked && !target_in_viewport(target))
    {
        return false;
    }
    if (g_state.viewport_scroll_locked)
    {
        focus_locked_viewport();
    }
    return scroll_viewport_by_key(event);
}

bool scroll_viewport_by_rotary(lv_event_t* event)
{
    if (!event || lv_event_get_code(event) != LV_EVENT_ROTARY || !g_state.viewport ||
        !lv_obj_is_valid(g_state.viewport))
    {
        return false;
    }
    const int32_t diff = lv_event_get_rotary_diff(event);
    if (diff == 0)
    {
        return false;
    }
    const lv_coord_t step =
        static_cast<lv_coord_t>(kViewportScrollStep *
                                (diff > 0 ? diff : -diff));
    lv_obj_scroll_by(g_state.viewport, 0, diff > 0 ? -step : step, LV_ANIM_OFF);
    lv_event_stop_bubbling(event);
    lv_event_stop_processing(event);
    return true;
}

bool route_rotary_to_viewport_scroll(lv_event_t* event)
{
    if (!event || lv_event_get_code(event) != LV_EVENT_ROTARY)
    {
        return false;
    }
    lv_obj_t* target = lv_event_get_target_obj(event);
    if (!g_state.viewport_scroll_locked && !target_in_viewport(target))
    {
        return false;
    }
    if (g_state.viewport_scroll_locked)
    {
        focus_locked_viewport();
    }
    return scroll_viewport_by_rotary(event);
}

void unlock_viewport_scroll(lv_event_t* event = nullptr)
{
    g_state.viewport_scroll_locked = false;
    if (app_g)
    {
        lv_group_set_editing(app_g, false);
    }
    if (event)
    {
        lv_event_stop_bubbling(event);
        lv_event_stop_processing(event);
    }
}

void lock_viewport_scroll(lv_event_t* event)
{
    if (!app_g || !g_state.viewport || !lv_obj_is_valid(g_state.viewport))
    {
        return;
    }
    g_state.viewport_scroll_locked = true;
    focus_locked_viewport();
    if (event)
    {
        lv_event_stop_bubbling(event);
        lv_event_stop_processing(event);
    }
}

bool pager_focus_key(uint32_t key)
{
    return key == kPagerRotateUpKey || key == kPagerRotateDownKey;
}

bool move_focus_by_pager_key(lv_event_t* event)
{
    if (!event || lv_event_get_code(event) != LV_EVENT_KEY || !app_g ||
        lv_group_get_editing(app_g))
    {
        return false;
    }
    const uint32_t key = lv_event_get_key(event);
    if (!pager_focus_key(key))
    {
        return false;
    }
    if (key == kPagerRotateUpKey)
    {
        lv_group_focus_prev(app_g);
    }
    else
    {
        lv_group_focus_next(app_g);
    }
    lv_event_stop_bubbling(event);
    lv_event_stop_processing(event);
    return true;
}

bool viewport_is_editing()
{
    return g_state.viewport_scroll_locked ||
           (app_g && lv_group_get_focused(app_g) == g_state.viewport &&
            lv_group_get_editing(app_g));
}

void viewport_event_cb(lv_event_t* event)
{
    if (!event)
    {
        return;
    }
    if (lv_event_get_code(event) == LV_EVENT_FOCUSED)
    {
        if (app_g)
        {
            lv_group_set_editing(app_g, g_state.viewport_scroll_locked);
        }
        return;
    }
    if (lv_event_get_code(event) == LV_EVENT_DEFOCUSED)
    {
        if (app_g && !g_state.viewport_scroll_locked)
        {
            lv_group_set_editing(app_g, false);
        }
        return;
    }
    if ((lv_event_get_code(event) == LV_EVENT_CLICKED ||
         lv_event_get_code(event) == LV_EVENT_PRESSED) &&
        lv_event_get_target_obj(event) == g_state.viewport)
    {
        lock_viewport_scroll(event);
        return;
    }
    if (lv_event_get_code(event) == LV_EVENT_ROTARY)
    {
        (void)route_rotary_to_viewport_scroll(event);
        return;
    }
    if (lv_event_get_code(event) != LV_EVENT_KEY || !app_g)
    {
        return;
    }

    const uint32_t key = lv_event_get_key(event);
    if (key == LV_KEY_ENTER)
    {
        lock_viewport_scroll(event);
        return;
    }
    if (viewport_is_editing() && (key == LV_KEY_BACKSPACE || key == LV_KEY_ESC))
    {
        unlock_viewport_scroll(event);
        return;
    }
    if (viewport_is_editing() || target_in_viewport(lv_event_get_target_obj(event)))
    {
        (void)route_key_to_viewport_scroll(event);
    }
}

void page_shortcut_event_cb(lv_event_t* event)
{
    if (!event)
    {
        return;
    }
    if (lv_event_get_code(event) == LV_EVENT_ROTARY)
    {
        (void)route_rotary_to_viewport_scroll(event);
        return;
    }
    if (lv_event_get_code(event) != LV_EVENT_KEY)
    {
        return;
    }
    const uint32_t key = lv_event_get_key(event);
    lv_obj_t* target = lv_event_get_target_obj(event);
    if (g_state.menu.is_open() && (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE))
    {
        g_state.menu.close();
        lv_event_stop_processing(event);
        return;
    }
    if (g_state.viewport_scroll_locked && (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE))
    {
        unlock_viewport_scroll(event);
        return;
    }
    if (route_key_to_viewport_scroll(event))
    {
        return;
    }
    if (g_state.viewport_scroll_locked &&
        (key == LV_KEY_LEFT || key == LV_KEY_RIGHT || key == LV_KEY_ENTER))
    {
        focus_locked_viewport();
        lv_event_stop_bubbling(event);
        lv_event_stop_processing(event);
        return;
    }
    if (is_help_shortcut_key(key))
    {
        open_network_help_modal();
        lv_event_stop_processing(event);
        return;
    }
    if (move_focus_by_pager_key(event))
    {
        return;
    }
    if (is_directory_toggle_shortcut_key(key))
    {
        open_node_list_page();
        lv_event_stop_processing(event);
        return;
    }
    if (is_browser_toggle_shortcut_key(key))
    {
        toggle_browser_panel();
        lv_event_stop_processing(event);
        return;
    }
    if (key == LV_KEY_RIGHT && object_in_subtree(g_state.directory_panel, target))
    {
        if (g_state.browser_collapsed)
        {
            g_state.browser_collapsed = false;
            apply_layout_state();
        }
        focus_browser_viewport();
        lv_event_stop_processing(event);
        return;
    }
    if (key == LV_KEY_LEFT && object_in_subtree(g_state.browser_panel, target))
    {
        if (!g_state.directory_collapsed)
        {
            focus_directory_panel();
            lv_event_stop_processing(event);
        }
        return;
    }
    if (key == 'i' || key == 'I')
    {
        toggle_immersive();
        lv_event_stop_processing(event);
        return;
    }
    if (g_state.immersive && (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE))
    {
        g_state.immersive = false;
        apply_layout_state();
        lv_event_stop_processing(event);
        return;
    }
    if (key == '/' || key == 's' || key == 'S')
    {
        open_search_modal();
        lv_event_stop_processing(event);
        return;
    }
    if (key == 'r' || key == 'R')
    {
        refresh_all(true);
        ::ui::feedback::show_notice(safe_tr("Network refreshed"), 1200);
        lv_event_stop_processing(event);
        return;
    }
    if (key == 'f' || key == 'F')
    {
        navigate_to_address("home:/favourites", true);
        lv_event_stop_processing(event);
        return;
    }
    if (key == 'a' || key == 'A')
    {
        open_node_list_page();
        lv_event_stop_processing(event);
        return;
    }
}

void request_exit()
{
    ui_request_exit_to_menu();
}

void top_bar_back_requested(void* /*user_data*/)
{
    if (g_state.immersive)
    {
        g_state.immersive = false;
        apply_layout_state();
        return;
    }
    request_exit();
}

void back_event_cb(lv_event_t* event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_KEY)
    {
        const uint32_t key = lv_event_get_key(event);
        if (key != LV_KEY_ESC && key != LV_KEY_BACKSPACE)
        {
            return;
        }
    }
    else if (code != LV_EVENT_CLICKED)
    {
        return;
    }
    top_bar_back_requested(nullptr);
    lv_event_stop_processing(event);
}

lv_obj_t* create_icon_button(lv_obj_t* parent,
                             const char* text,
                             lv_event_cb_t cb,
                             lv_coord_t size)
{
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, size, size);
    style_chrome_button(btn);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_KEY, nullptr);
    lv_obj_add_event_cb(btn, page_shortcut_event_cb, LV_EVENT_KEY, nullptr);
    lv_obj_t* label = lv_label_create(btn);
    set_label(label, text, kText, caption_font());
    lv_obj_center(label);
    add_to_group(btn);
    return btn;
}

lv_obj_t* create_tab_button(lv_obj_t* parent,
                            const char* text,
                            DirectoryMode mode,
                            lv_coord_t height)
{
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, LV_PCT(50), height);
    style_chrome_button(btn, false);
    lv_obj_set_style_bg_color(btn, lv_color_hex(kAmber), LV_STATE_CHECKED);
    lv_obj_set_style_pad_left(btn, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_right(btn, 3, LV_PART_MAIN);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_add_event_cb(btn,
                        directory_tab_event_cb,
                        LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(mode == DirectoryMode::Favourites ? 0U : 1U));
    lv_obj_add_event_cb(btn,
                        directory_tab_event_cb,
                        LV_EVENT_KEY,
                        reinterpret_cast<void*>(mode == DirectoryMode::Favourites ? 0U : 1U));
    lv_obj_add_event_cb(btn, page_shortcut_event_cb, LV_EVENT_KEY, nullptr);
    add_to_group(btn);

    lv_obj_t* label = lv_label_create(btn);
    set_label(label, safe_tr(text), kText, tiny_font());
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_center(label);
    return btn;
}

lv_coord_t resolve_directory_width()
{
    lv_coord_t width = lv_display_get_physical_horizontal_resolution(nullptr);
    if (width <= 0)
    {
        width = lv_obj_get_width(lv_scr_act());
    }
    if (width <= 340)
    {
        return 96;
    }
    if (width <= 420)
    {
        return 118;
    }
    return 142;
}

void create_directory_panel(lv_obj_t* parent)
{
    const auto& profile = ::ui::page_profile::current();
    ::ui::components::two_pane_layout::SidePanelSpec side_spec;
    side_spec.width = resolve_directory_width();
    side_spec.pad_row = profile.dense ? 3 : 5;
    side_spec.scrollbar_mode = LV_SCROLLBAR_MODE_OFF;
    g_state.directory_panel =
        ::ui::components::two_pane_layout::create_side_panel(parent, side_spec);
    style_panel(g_state.directory_panel, kWarmBg);
    lv_obj_set_style_pad_all(g_state.directory_panel, profile.dense ? 3 : 5, LV_PART_MAIN);
    lv_obj_set_style_pad_row(g_state.directory_panel, profile.dense ? 3 : 5, LV_PART_MAIN);

    g_state.directory_tabs = lv_obj_create(g_state.directory_panel);
    lv_obj_set_size(g_state.directory_tabs, LV_PCT(100), profile.dense ? 22 : 26);
    style_plain_container(g_state.directory_tabs, kWarmBg, LV_OPA_TRANSP);
    lv_obj_set_flex_flow(g_state.directory_tabs, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(g_state.directory_tabs, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(g_state.directory_tabs, 3, LV_PART_MAIN);
    ::ui::components::two_pane_layout::make_non_scrollable(g_state.directory_tabs);

    g_state.favourites_tab = create_tab_button(g_state.directory_tabs,
                                               "Fav",
                                               DirectoryMode::Favourites,
                                               profile.dense ? 22 : 26);
    g_state.announces_tab = create_tab_button(g_state.directory_tabs,
                                              "Ann",
                                              DirectoryMode::Announces,
                                              profile.dense ? 22 : 26);

    g_state.directory_search_btn = lv_btn_create(g_state.directory_panel);
    lv_obj_set_size(g_state.directory_search_btn, LV_PCT(100), profile.dense ? 22 : 26);
    style_chrome_button(g_state.directory_search_btn, false);
    lv_obj_add_event_cb(g_state.directory_search_btn,
                        search_button_event_cb,
                        LV_EVENT_CLICKED,
                        nullptr);
    lv_obj_add_event_cb(g_state.directory_search_btn,
                        search_button_event_cb,
                        LV_EVENT_KEY,
                        nullptr);
    lv_obj_add_event_cb(g_state.directory_search_btn,
                        page_shortcut_event_cb,
                        LV_EVENT_KEY,
                        nullptr);
    add_to_group(g_state.directory_search_btn);

    lv_obj_t* search_label = lv_label_create(g_state.directory_search_btn);
    set_label(search_label, LV_SYMBOL_SEARCH, kText, caption_font());
    lv_obj_center(search_label);

    g_state.directory_list = lv_obj_create(g_state.directory_panel);
    lv_obj_set_width(g_state.directory_list, LV_PCT(100));
    lv_obj_set_height(g_state.directory_list, 0);
    lv_obj_set_flex_grow(g_state.directory_list, 1);
    style_plain_container(g_state.directory_list, kWarmBg, LV_OPA_TRANSP);
    lv_obj_set_style_pad_all(g_state.directory_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(g_state.directory_list, profile.dense ? 3 : 5, LV_PART_MAIN);
    lv_obj_set_flex_flow(g_state.directory_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(g_state.directory_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_state.directory_list, LV_SCROLLBAR_MODE_AUTO);
}

void create_browser_panel(lv_obj_t* parent)
{
    const auto& profile = ::ui::page_profile::current();
    ::ui::components::two_pane_layout::MainPanelSpec main_spec;
    main_spec.pad_all = profile.dense ? 2 : 3;
    main_spec.pad_row = profile.dense ? 3 : 5;
    main_spec.scrollbar_mode = LV_SCROLLBAR_MODE_OFF;
    g_state.browser_panel =
        ::ui::components::two_pane_layout::create_main_panel(parent, main_spec);
    style_panel(g_state.browser_panel, kPanelBg);

    g_state.browser_toolbar = lv_obj_create(g_state.browser_panel);
    lv_obj_set_width(g_state.browser_toolbar, LV_PCT(100));
    lv_obj_set_height(g_state.browser_toolbar, profile.dense ? 24 : 28);
    style_plain_container(g_state.browser_toolbar, kPanelBg, LV_OPA_TRANSP);
    lv_obj_set_style_pad_all(g_state.browser_toolbar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(g_state.browser_toolbar, profile.dense ? 2 : 4, LV_PART_MAIN);
    lv_obj_set_flex_flow(g_state.browser_toolbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_state.browser_toolbar,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    ::ui::components::two_pane_layout::make_non_scrollable(g_state.browser_toolbar);

    const lv_coord_t button_size = profile.dense ? 22 : 26;
    const lv_coord_t rail_button_size = profile.dense ? 20 : 22;

    g_state.address_area = lv_textarea_create(g_state.browser_toolbar);
    ::ui::widgets::attach_touch_text_editor(g_state.address_area);
    lv_textarea_set_one_line(g_state.address_area, true);
    lv_textarea_set_max_length(g_state.address_area,
                               static_cast<uint16_t>(sizeof(g_state.current_address) - 1U));
    lv_textarea_set_text(g_state.address_area, g_state.current_address);
    lv_obj_set_height(g_state.address_area, button_size);
    lv_obj_set_width(g_state.address_area, 0);
    lv_obj_set_flex_grow(g_state.address_area, 1);
    lv_obj_set_style_bg_color(g_state.address_area, lv_color_hex(0xFFF8E8), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_state.address_area, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_state.address_area, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(g_state.address_area, lv_color_hex(kLine), LV_PART_MAIN);
    lv_obj_set_style_radius(g_state.address_area, 8, LV_PART_MAIN);
    lv_obj_set_style_text_color(g_state.address_area, lv_color_hex(kText), LV_PART_MAIN);
    lv_obj_set_style_text_font(g_state.address_area, tiny_font(), LV_PART_MAIN);
    lv_obj_set_style_pad_left(g_state.address_area, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_right(g_state.address_area, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_top(g_state.address_area, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(g_state.address_area, 3, LV_PART_MAIN);
    style_focusable(g_state.address_area);
    lv_obj_add_event_cb(g_state.address_area, address_key_event_cb, LV_EVENT_KEY, nullptr);
    lv_obj_add_event_cb(g_state.address_area, page_shortcut_event_cb, LV_EVENT_KEY, nullptr);
    add_to_group(g_state.address_area);

    g_state.go_btn =
        create_icon_button(g_state.browser_toolbar, LV_SYMBOL_RIGHT, go_event_cb, button_size);

    g_state.browser_body = lv_obj_create(g_state.browser_panel);
    lv_obj_set_width(g_state.browser_body, LV_PCT(100));
    lv_obj_set_height(g_state.browser_body, 0);
    lv_obj_set_flex_grow(g_state.browser_body, 1);
    style_plain_container(g_state.browser_body, kPanelBg, LV_OPA_TRANSP);
    lv_obj_set_style_pad_all(g_state.browser_body, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(g_state.browser_body, 3, LV_PART_MAIN);
    lv_obj_set_flex_flow(g_state.browser_body, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_state.browser_body,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    ::ui::components::two_pane_layout::make_non_scrollable(g_state.browser_body);

    g_state.viewport = lv_obj_create(g_state.browser_body);
    lv_obj_set_width(g_state.viewport, 0);
    lv_obj_set_height(g_state.viewport, LV_PCT(100));
    lv_obj_set_flex_grow(g_state.viewport, 1);
    lv_obj_set_style_bg_color(g_state.viewport, lv_color_hex(kTerminalBg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_state.viewport, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_state.viewport, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(g_state.viewport, lv_color_hex(kLine), LV_PART_MAIN);
    lv_obj_set_style_radius(g_state.viewport, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(g_state.viewport, profile.dense ? 5 : 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(g_state.viewport, profile.dense ? 2 : 3, LV_PART_MAIN);
    lv_obj_set_flex_flow(g_state.viewport, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(g_state.viewport, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_state.viewport, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(g_state.viewport, LV_OBJ_FLAG_CLICKABLE);
    style_focusable(g_state.viewport);
    lv_obj_add_event_cb(g_state.viewport, viewport_event_cb, LV_EVENT_FOCUSED, nullptr);
    lv_obj_add_event_cb(g_state.viewport, viewport_event_cb, LV_EVENT_DEFOCUSED, nullptr);
    lv_obj_add_event_cb(g_state.viewport, viewport_event_cb, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(g_state.viewport, viewport_event_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(g_state.viewport, viewport_event_cb, LV_EVENT_ROTARY, nullptr);
    lv_obj_add_event_cb(g_state.viewport, viewport_event_cb, LV_EVENT_KEY, nullptr);
    lv_obj_add_event_cb(g_state.viewport, page_shortcut_event_cb, LV_EVENT_KEY, nullptr);
    add_to_group(g_state.viewport);

    g_state.browser_rail = lv_obj_create(g_state.browser_body);
    lv_obj_set_width(g_state.browser_rail, rail_button_size);
    lv_obj_set_height(g_state.browser_rail, LV_PCT(100));
    style_plain_container(g_state.browser_rail, kPanelBg, LV_OPA_TRANSP);
    lv_obj_set_style_pad_all(g_state.browser_rail, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(g_state.browser_rail, profile.dense ? 2 : 3, LV_PART_MAIN);
    lv_obj_set_flex_flow(g_state.browser_rail, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_state.browser_rail,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    ::ui::components::two_pane_layout::make_non_scrollable(g_state.browser_rail);

    g_state.browser_back_btn = create_icon_button(g_state.browser_rail,
                                                  LV_SYMBOL_LEFT,
                                                  browser_back_event_cb,
                                                  rail_button_size);
    g_state.browser_forward_btn = create_icon_button(g_state.browser_rail,
                                                     LV_SYMBOL_RIGHT,
                                                     browser_forward_event_cb,
                                                     rail_button_size);
    g_state.home_btn = create_icon_button(g_state.browser_rail,
                                          LV_SYMBOL_HOME,
                                          home_event_cb,
                                          rail_button_size);
    g_state.refresh_btn = create_icon_button(g_state.browser_rail,
                                             LV_SYMBOL_REFRESH,
                                             refresh_event_cb,
                                             rail_button_size);
    g_state.cancel_btn = create_icon_button(g_state.browser_rail,
                                            "X",
                                            cancel_request_event_cb,
                                            rail_button_size);
    lv_obj_add_flag(g_state.cancel_btn, LV_OBJ_FLAG_HIDDEN);
    g_state.rail_directory_btn = create_icon_button(g_state.browser_rail,
                                                    LV_SYMBOL_LIST,
                                                    directory_rail_event_cb,
                                                    rail_button_size);
    g_state.rail_search_btn = create_icon_button(g_state.browser_rail,
                                                 LV_SYMBOL_SEARCH,
                                                 search_button_event_cb,
                                                 rail_button_size);
}

} // namespace

namespace network::ui::shell
{

void enter(void* /*user_data*/, lv_obj_t* parent)
{
    if (!parent || !ensure_network_page_state() ||
        (g_state.root && lv_obj_is_valid(g_state.root)))
    {
        return;
    }

    const auto& profile = ::ui::page_profile::current();
    ::ui::components::two_pane_layout::RootSpec root_spec;
    root_spec.pad_row = profile.dense ? 1 : profile.top_content_gap;
    g_state.root = ::ui::components::two_pane_layout::create_root(parent, root_spec);
    lv_obj_set_style_bg_color(g_state.root, lv_color_hex(kWarmBg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_state.root, LV_OPA_COVER, LV_PART_MAIN);

    ::ui::components::two_pane_layout::HeaderSpec header_spec;
    header_spec.height = profile.top_bar_height;
    header_spec.bg_hex = kWarmBg;
    header_spec.pad_all = 0;
    g_state.header =
        ::ui::components::two_pane_layout::create_header_container(g_state.root,
                                                                   header_spec);

    ::ui::widgets::TopBarConfig top_bar_config;
    top_bar_config.height = profile.top_bar_height;
    ::ui::widgets::top_bar_init(g_state.top_bar, g_state.header, top_bar_config);
    ::ui::widgets::top_bar_set_title(g_state.top_bar, safe_tr("Network"));
    ::ui::widgets::top_bar_set_back_callback(g_state.top_bar,
                                             top_bar_back_requested,
                                             nullptr);
    ui_update_top_bar_battery(g_state.top_bar);

    ::ui::components::two_pane_layout::ContentSpec content_spec;
    content_spec.pad_left = 2;
    content_spec.pad_right = 2;
    content_spec.pad_top = 2;
    content_spec.pad_bottom = 2;
    g_state.content =
        ::ui::components::two_pane_layout::create_content_row(g_state.root,
                                                              content_spec);
    lv_obj_set_style_pad_column(g_state.content, 4, LV_PART_MAIN);

    create_directory_panel(g_state.content);
    create_browser_panel(g_state.content);
    g_state.bottom_bar = ::ui::components::bottom_bar::create(g_state.root);
    auto* menu_button = ::ui::components::bottom_bar::add_button(g_state.bottom_bar, "Menu");
    lv_obj_add_event_cb(menu_button, page_shortcut_event_cb, LV_EVENT_KEY, nullptr);
    lv_obj_set_style_min_width(g_state.directory_panel, 180, LV_PART_MAIN);
    lv_obj_set_style_min_height(g_state.directory_panel, 160, LV_PART_MAIN);
    g_state.menu.init(g_state.root, menu_button, g_state.directory_panel,
                      [](void*, bool open)
                      {
                          g_state.directory_collapsed = !open;
                          rebuild_focus_group(g_state.viewport);
                      });

    lv_obj_add_event_cb(g_state.root, back_event_cb, LV_EVENT_KEY, nullptr);
    lv_obj_add_event_cb(g_state.root, page_shortcut_event_cb, LV_EVENT_KEY, nullptr);
    if (g_state.top_bar.back_btn)
    {
        lv_obj_add_event_cb(g_state.top_bar.back_btn, back_event_cb, LV_EVENT_KEY, nullptr);
        lv_obj_add_event_cb(g_state.top_bar.back_btn, page_shortcut_event_cb, LV_EVENT_KEY, nullptr);
        add_to_group(g_state.top_bar.back_btn);
    }
    if (app_g)
    {
        set_default_group(app_g);
    }
    apply_layout_state();

    push_history(g_state.current_address);
    refresh_all();
    update_tab_state();
    if (app_g && g_state.directory_list)
    {
        lv_group_focus_obj(g_state.viewport ? g_state.viewport : g_state.address_area);
    }
}

void exit(void* /*user_data*/, lv_obj_t* /*parent*/)
{
    if (!s_page_state)
    {
        return;
    }
    g_state.menu.reset();

    NETWORK_PAGE_LOG("exit begin root=%u rows=%u links=%u history=%u\n",
                     g_state.root && lv_obj_is_valid(g_state.root) ? 1U : 0U,
                     static_cast<unsigned>(g_state.row_context_count),
                     static_cast<unsigned>(g_state.link_context_count),
                     static_cast<unsigned>(g_state.history_count));
    if (g_state.page_load_timer)
    {
        lv_timer_del(g_state.page_load_timer);
        g_state.page_load_timer = nullptr;
    }
    ::ui::components::floating_search_box::close(g_state.search_box);
    close_network_help_modal();
    if (app_g)
    {
        lv_group_remove_all_objs(app_g);
    }
    if (g_state.root && lv_obj_is_valid(g_state.root))
    {
        lv_obj_del(g_state.root);
    }
    reset_state_after_destroy();
    release_network_page_state();
    NETWORK_PAGE_LOG("exit done\n");
}

} // namespace network::ui::shell
