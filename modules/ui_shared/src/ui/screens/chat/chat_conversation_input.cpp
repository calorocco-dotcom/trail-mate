#if !defined(ARDUINO_T_WATCH_S3)
#include "ui/screens/chat/chat_conversation_input.h"
#include "ui/screens/chat/chat_conversation_components.h"

#include <algorithm>
#include <cstdio>

#define CHAT_CONV_INPUT_DEBUG 0
#if CHAT_CONV_INPUT_DEBUG
#define CHAT_CONV_INPUT_LOG(...) std::printf(__VA_ARGS__)
#else
#define CHAT_CONV_INPUT_LOG(...)
#endif

namespace chat::ui::conversation::input
{

namespace
{
constexpr int kEncoderKeyRotateUp = LV_KEY_LEFT;
constexpr int kEncoderKeyRotateDown = LV_KEY_RIGHT;
constexpr lv_coord_t kScrollStep = 24;
constexpr lv_coord_t kPageScrollPadding = 12;
} // namespace

static void consume(lv_event_t* e)
{
    if (!e)
    {
        return;
    }
    lv_event_stop_bubbling(e);
    lv_event_stop_processing(e);
}

static lv_coord_t page_scroll_step(ChatConversationScreen* screen)
{
    lv_obj_t* msg_list = screen ? screen->getMsgList() : nullptr;
    const lv_coord_t height =
        msg_list && lv_obj_is_valid(msg_list) ? lv_obj_get_height(msg_list) : 0;
    return std::max<lv_coord_t>(kScrollStep, height - kPageScrollPadding);
}

static bool scroll_messages(ChatConversationScreen* screen,
                            lv_event_t* e,
                            lv_coord_t delta)
{
    lv_obj_t* msg_list = screen ? screen->getMsgList() : nullptr;
    if (!msg_list || !lv_obj_is_valid(msg_list) || delta == 0)
    {
        return false;
    }
    const lv_coord_t current = lv_obj_get_scroll_y(msg_list);
    const lv_coord_t maximum = current + lv_obj_get_scroll_bottom(msg_list);
    const lv_coord_t target = std::max<lv_coord_t>(0, std::min<lv_coord_t>(maximum, current + delta));
    lv_obj_scroll_to_y(msg_list, target, LV_ANIM_OFF);
    consume(e);
    return true;
}

static bool jump_messages(ChatConversationScreen* screen,
                          lv_event_t* e,
                          bool bottom)
{
    lv_obj_t* msg_list = screen ? screen->getMsgList() : nullptr;
    if (!msg_list || !lv_obj_is_valid(msg_list))
    {
        return false;
    }
    lv_obj_scroll_to_y(msg_list, bottom ? LV_COORD_MAX : 0, LV_ANIM_OFF);
    consume(e);
    return true;
}

static bool send_back(ChatConversationScreen* screen, lv_event_t* e)
{
    if (lv_obj_t* back_btn = screen ? screen->getBackBtn() : nullptr)
    {
        lv_obj_send_event(back_btn, LV_EVENT_CLICKED, nullptr);
        consume(e);
        return true;
    }
    return false;
}

static bool send_reply(ChatConversationScreen* screen, lv_event_t* e)
{
    if (!screen || !screen->isAlive())
    {
        return false;
    }
    if (lv_obj_t* reply_btn = screen->getReplyBtn())
    {
        lv_obj_send_event(reply_btn, LV_EVENT_CLICKED, nullptr);
        consume(e);
        return true;
    }
    if (screen->requestAction(ChatConversationScreen::ActionIntent::Reply))
    {
        consume(e);
        return true;
    }
    return false;
}

static bool handle_map_key(ChatConversationScreen* screen,
                           lv_event_t* e,
                           uint32_t key)
{
    if (!screen || !screen->isAlive())
    {
        return false;
    }

    if (key == 'm' || key == 'M')
    {
        screen->toggleLocationMap();
        consume(e);
        return true;
    }
    if (key == 'l' || key == 'L')
    {
        screen->cycleLocationMapLayer();
        consume(e);
        return true;
    }
    return false;
}

static bool handle_conversation_shortcut(ChatConversationScreen* screen,
                                         lv_event_t* e,
                                         uint32_t key,
                                         bool allow_message_selection)
{
    if (!screen || !screen->isAlive())
    {
        return false;
    }

    if (key == LV_KEY_BACKSPACE || key == LV_KEY_ESC)
    {
        return send_back(screen, e);
    }
    if (key == 'h' || key == 'H')
    {
        screen->toggleShortcutHelp();
        consume(e);
        return true;
    }
    if (key == 's' || key == 'S')
    {
        return send_reply(screen, e);
    }
    if (allow_message_selection)
    {
        if (key == 'a' || key == 'A')
        {
            if (screen->selectPreviousMessage())
            {
                consume(e);
            }
            return true;
        }
        if (key == 'd' || key == 'D')
        {
            if (screen->selectNextMessage())
            {
                consume(e);
            }
            return true;
        }
        if (key == LV_KEY_ENTER && screen->activateSelectedMessage())
        {
            consume(e);
            return true;
        }
    }
    if (handle_map_key(screen, e, key))
    {
        return true;
    }

    // The message list owns rotary scrolling only while its group is in
    // editing mode. Once Enter leaves that mode, return rotation to LVGL so
    // it can move focus between the list, Send, and the top-bar Back button.
    const lv_group_t* const group = lv_group_get_default();
    const bool list_scrolling_enabled = !group || lv_group_get_editing(group);
    if (list_scrolling_enabled &&
        (key == LV_KEY_UP || key == kEncoderKeyRotateUp))
    {
        return scroll_messages(screen, e, -kScrollStep);
    }
    if (list_scrolling_enabled &&
        (key == LV_KEY_DOWN || key == kEncoderKeyRotateDown))
    {
        return scroll_messages(screen, e, kScrollStep);
    }
    if (key == LV_KEY_PREV)
    {
        if (screen->requestAction(ChatConversationScreen::ActionIntent::LoadOlder))
        {
            consume(e);
            return true;
        }
        return scroll_messages(screen, e, -page_scroll_step(screen));
    }
    if (key == LV_KEY_NEXT)
    {
        if (screen->requestAction(ChatConversationScreen::ActionIntent::LoadNewer))
        {
            consume(e);
            return true;
        }
        return scroll_messages(screen, e, page_scroll_step(screen));
    }
    if (key == LV_KEY_HOME)
    {
        return jump_messages(screen, e, false);
    }
    if (key == LV_KEY_END)
    {
        return jump_messages(screen, e, true);
    }

    return false;
}

static void on_msg_list_key(lv_event_t* e)
{
    auto* screen = static_cast<ChatConversationScreen*>(lv_event_get_user_data(e));
    if (!screen || !screen->isAlive()) return;

    uint32_t key = lv_event_get_key(e);
    if (handle_conversation_shortcut(screen, e, key, true))
    {
        return;
    }

    if (lv_group_t* g = lv_group_get_default())
    {
        if (key == LV_KEY_ENTER)
        {
            lv_group_set_editing(g, !lv_group_get_editing(g));
            lv_event_stop_processing(e);
            return;
        }

        if (!lv_group_get_editing(g))
        {
            return;
        }

        lv_coord_t delta = 0;
        if (key == LV_KEY_UP || key == kEncoderKeyRotateUp)
        {
            delta = -kScrollStep;
        }
        else if (key == LV_KEY_DOWN || key == kEncoderKeyRotateDown)
        {
            delta = kScrollStep;
        }
        if (delta != 0)
        {
            scroll_messages(screen, e, delta);
        }
    }
}

static void on_control_key(lv_event_t* e)
{
    auto* screen = static_cast<ChatConversationScreen*>(lv_event_get_user_data(e));
    if (!screen || !screen->isAlive()) return;
    uint32_t key = lv_event_get_key(e);
    (void)handle_conversation_shortcut(screen, e, key, false);
}

void init(ChatConversationScreen* screen, Binding* binding)
{
    if (!binding)
    {
        return;
    }
    binding->bound = false;
    binding->msg_list = screen ? screen->getMsgList() : nullptr;
    binding->reply_btn = screen ? screen->getReplyBtn() : nullptr;
    binding->back_btn = screen ? screen->getBackBtn() : nullptr;
    binding->group = lv_group_get_default();

    if (!screen)
    {
        CHAT_CONV_INPUT_LOG("[ChatConversationInput] init (no screen)\n");
        return;
    }
    if (!binding->group)
    {
        CHAT_CONV_INPUT_LOG("[ChatConversationInput] init (no group)\n");
        return;
    }

    if (binding->msg_list)
    {
        lv_group_add_obj(binding->group, binding->msg_list);
        lv_group_focus_obj(binding->msg_list);
#if defined(ARDUINO_T_LORA_PAGER)
        // The wheel can reach Send and Back; press to scroll, hold to leave editing.
        lv_group_set_editing(binding->group, false);
#else
        lv_group_set_editing(binding->group, true);
#endif
        lv_obj_add_event_cb(binding->msg_list, on_msg_list_key, LV_EVENT_KEY, screen);
    }
    if (binding->reply_btn)
    {
        lv_group_add_obj(binding->group, binding->reply_btn);
        lv_obj_add_event_cb(binding->reply_btn, on_control_key, LV_EVENT_KEY, screen);
    }
    if (binding->back_btn)
    {
        lv_group_add_obj(binding->group, binding->back_btn);
        lv_obj_add_event_cb(binding->back_btn, on_control_key, LV_EVENT_KEY, screen);
    }
    binding->bound = true;
    CHAT_CONV_INPUT_LOG("[ChatConversationInput] init (group focus msg list)\n");
}

void cleanup(Binding* binding)
{
    if (!binding || !binding->bound)
    {
        return;
    }
    if (binding->group)
    {
        if (binding->msg_list)
        {
            lv_group_remove_obj(binding->msg_list);
        }
        if (binding->reply_btn)
        {
            lv_group_remove_obj(binding->reply_btn);
        }
        if (binding->back_btn)
        {
            lv_group_remove_obj(binding->back_btn);
        }
    }
    binding->msg_list = nullptr;
    binding->reply_btn = nullptr;
    binding->back_btn = nullptr;
    binding->group = nullptr;
    binding->bound = false;
    CHAT_CONV_INPUT_LOG("[ChatConversationInput] cleanup\n");
}

} // namespace chat::ui::conversation::input

#endif
