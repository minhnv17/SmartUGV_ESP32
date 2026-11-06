#pragma once
#include <stdbool.h>
#include <stdint.h>

#define TOUCH_BUTTON_Y 198
#define TOUCH_BUTTON_HEIGHT 38
#define TOUCH_BUTTON_WIDTH 146
#define TOUCH_LEFT_X 8
#define TOUCH_RIGHT_X 166
#define TOUCH_VOLUME_Y 150
#define TOUCH_VOLUME_HEIGHT 38
#define TOUCH_VOLUME_BUTTON_WIDTH 38
#define TOUCH_VOLUME_MINUS_X 8
#define TOUCH_VOLUME_PLUS_X 274
#define TOUCH_SLIDER_X 54
#define TOUCH_SLIDER_WIDTH 212

typedef enum { TOUCH_VIEW_NONE, TOUCH_VIEW_ACTIVE, TOUCH_VIEW_PAUSED } touch_view_t;
typedef enum {
    TOUCH_ACTION_NONE, TOUCH_ACTION_PAUSE, TOUCH_ACTION_RESUME, TOUCH_ACTION_STOP,
    TOUCH_ACTION_VOLUME_DOWN, TOUCH_ACTION_VOLUME_UP, TOUCH_ACTION_VOLUME_SET,
} touch_action_t;
typedef struct {
    bool ready, pressed, cancelled;
    unsigned down_samples, up_samples;
    touch_view_t view;
    touch_action_t action;
    unsigned volume_percent; // Last slider value; retained with a SET release.
} touch_gesture_t;

// FT6336 registers 0x02..0x06, native portrait coordinates -> LCD landscape.
bool touch_decode(const uint8_t data[5], bool flip_landscape, unsigned *contacts, int *x, int *y);
touch_action_t touch_hit(touch_view_t view, int x, int y);
// Poll every 20ms. A stable press/release fires once; errors/multitouch cancel.
touch_action_t touch_gesture_step(touch_gesture_t *gesture, touch_view_t view,
                                 bool valid, unsigned contacts, int x, int y);
