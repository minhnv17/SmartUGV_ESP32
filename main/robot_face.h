#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "touch_ui.h"

typedef enum {
    ROBOT_BOOTING,
    ROBOT_CONNECTING_WIFI,
    ROBOT_CONNECTING_LIVE,
    ROBOT_LISTENING,
    ROBOT_THINKING,
    ROBOT_SPEAKING,
    ROBOT_RECONNECTING,
    ROBOT_ERROR,
    ROBOT_MUSIC_SEARCHING,
    ROBOT_MUSIC_BUFFERING,
    ROBOT_MUSIC_PLAYING,
    ROBOT_MUSIC_PAUSED,
    ROBOT_MUSIC_ERROR,
} robot_status_t;

// Initialize the ES3C28P ILI9341 in 320x240 landscape. Failure must not stop voice chat.
esp_err_t robot_face_start(void);
// These setters are short and thread safe; only the display task touches SPI.
void robot_face_set_status(robot_status_t status);
void robot_face_set_wifi(bool connected);
// neutral / happy / sad / surprised / sleepy. Automatically expires after 15s.
// Returns false for unsupported expressions or if the display is unavailable.
bool robot_face_set_emotion(const char *emotion);
bool robot_face_is_available(void);
// Only hit-test controls from the most recently completed LCD frame.
touch_view_t robot_face_touch_view(void);
