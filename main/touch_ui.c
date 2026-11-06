#include "touch_ui.h"

bool touch_decode(const uint8_t data[5], bool flip, unsigned *contacts, int *x, int *y)
{
    if (!data || !contacts || !x || !y) return false;
    *contacts = data[0] & 0x0f;
    *x = *y = 0;
    if (*contacts > 2) return false;
    if (*contacts != 1) return true;
    unsigned event = data[1] >> 6;
    if (event == 1) { *contacts = 0; return true; }
    if (event == 3) return false;
    unsigned raw_x = ((data[1] & 0x0f) << 8) | data[2];
    unsigned raw_y = ((data[3] & 0x0f) << 8) | data[4];
    if (raw_x >= 240 || raw_y >= 320) return false;
    // Same MV/MX/MY orientation as LCD MADCTL 0x68 / 0xa8.
    *x = flip ? 319 - (int)raw_y : (int)raw_y;
    *y = flip ? (int)raw_x : 239 - (int)raw_x;
    return true;
}

touch_action_t touch_hit(touch_view_t view, int x, int y)
{
    if (view != TOUCH_VIEW_ACTIVE && view != TOUCH_VIEW_PAUSED) return TOUCH_ACTION_NONE;
    if (y >= TOUCH_VOLUME_Y && y < TOUCH_VOLUME_Y + TOUCH_VOLUME_HEIGHT) {
        if (x >= TOUCH_VOLUME_MINUS_X && x < TOUCH_VOLUME_MINUS_X + TOUCH_VOLUME_BUTTON_WIDTH) return TOUCH_ACTION_VOLUME_DOWN;
        if (x >= TOUCH_VOLUME_PLUS_X && x < TOUCH_VOLUME_PLUS_X + TOUCH_VOLUME_BUTTON_WIDTH) return TOUCH_ACTION_VOLUME_UP;
        if (x >= TOUCH_SLIDER_X - 6 && x < TOUCH_SLIDER_X + TOUCH_SLIDER_WIDTH + 6) return TOUCH_ACTION_VOLUME_SET;
    }
    if (y < TOUCH_BUTTON_Y || y >= TOUCH_BUTTON_Y + TOUCH_BUTTON_HEIGHT) return TOUCH_ACTION_NONE;
    if (x >= TOUCH_RIGHT_X && x < TOUCH_RIGHT_X + TOUCH_BUTTON_WIDTH) return TOUCH_ACTION_STOP;
    if (x >= TOUCH_LEFT_X && x < TOUCH_LEFT_X + TOUCH_BUTTON_WIDTH)
        return view == TOUCH_VIEW_PAUSED ? TOUCH_ACTION_RESUME : TOUCH_ACTION_PAUSE;
    return TOUCH_ACTION_NONE;
}

touch_action_t touch_gesture_step(touch_gesture_t *g, touch_view_t view,
                                 bool valid, unsigned contacts, int x, int y)
{
    if (!g) return TOUCH_ACTION_NONE;
    if (!valid || contacts > 1) { *g = (touch_gesture_t){0}; return TOUCH_ACTION_NONE; }
    if (!contacts) {
        if (g->up_samples < 2) ++g->up_samples;
        if (g->up_samples < 2) return TOUCH_ACTION_NONE;
        touch_action_t action = g->pressed && !g->cancelled && g->down_samples >= 2 && g->view == view ?
            g->action : TOUCH_ACTION_NONE;
        unsigned volume = g->volume_percent;
        *g = (touch_gesture_t){.ready = true, .up_samples = 2, .volume_percent = volume};
        return action;
    }
    g->up_samples = 0;
    if (!g->ready) return TOUCH_ACTION_NONE; // Require a clean release after startup/error.
    touch_action_t hit = touch_hit(view, x, y);
    // Once a slider drag starts, allow movement beyond its ends and clamp it.
    if (g->pressed && g->action == TOUCH_ACTION_VOLUME_SET &&
        y >= TOUCH_VOLUME_Y && y < TOUCH_VOLUME_Y + TOUCH_VOLUME_HEIGHT && x >= 0 && x < 320)
        hit = TOUCH_ACTION_VOLUME_SET;
    if (hit == TOUCH_ACTION_VOLUME_SET) {
        int value = (x - TOUCH_SLIDER_X) * 100 / (TOUCH_SLIDER_WIDTH - 1);
        g->volume_percent = value < 0 ? 0 : value > 100 ? 100 : (unsigned)value;
    }
    if (!g->pressed) {
        g->pressed = true;
        g->view = view;
        g->action = hit;
        g->down_samples = 1;
    } else {
        if (g->view != view || hit != g->action) g->cancelled = true;
        if (g->down_samples < 2) ++g->down_samples;
    }
    return TOUCH_ACTION_NONE;
}
