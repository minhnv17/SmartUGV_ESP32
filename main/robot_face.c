#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "robot_face.h"
#include "sdkconfig.h"
#include "display_text.h"
#include "music.h"
#include "touch_input.h"
#include "speaker.h"

#define WIDTH 320
#define HEIGHT 240
#define STRIPE_ROWS 16
#define LCD_BL GPIO_NUM_45
#define EMOTION_MS 15000
// ILI9341 MADCTL: MV swaps axes, BGR preserves the panel color order.
#ifdef CONFIG_ROBOT_FACE_FLIP_LANDSCAPE
#define LCD_MADCTL 0xa8  // MY | MV | BGR: opposite landscape mounting.
#else
#define LCD_MADCTL 0x68  // MX | MV | BGR: default 90-degree landscape.
#endif

typedef enum { NEUTRAL, HAPPY, SAD, SURPRISED, SLEEPY } emotion_t;
static const char *const s_emotions[] = {"neutral", "happy", "sad", "surprised", "sleepy"};
static const char *TAG = "ROBOT_FACE";
static esp_lcd_panel_io_handle_t s_io;
static SemaphoreHandle_t s_transfer_done;
static uint16_t *s_pixels;
static uint16_t *s_stripe;
static atomic_bool s_available;
static atomic_bool s_wifi;
static atomic_int s_status = ROBOT_BOOTING;
static portMUX_TYPE s_emotion_lock = portMUX_INITIALIZER_UNLOCKED;
static emotion_t s_emotion;
static TickType_t s_emotion_since;
static char s_render_title[52], s_render_artist[52];
static unsigned s_render_duration;
static speaker_music_snapshot_t s_render_audio;
static touch_view_t s_render_view;
static bool s_render_touch_available;
static atomic_int s_touch_view = TOUCH_VIEW_NONE;

// Store RGB565 in wire byte order. MADCTL keeps the BGR bit set.
static uint16_t rgb(unsigned r, unsigned g, unsigned b)
{
    uint16_t color = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    return (color << 8) | (color >> 8);
}

static void rect(int x, int y, int w, int h, uint16_t color)
{
    int end_x = x + w < WIDTH ? x + w : WIDTH;
    int end_y = y + h < HEIGHT ? y + h : HEIGHT;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    for (int row = y; row < end_y; ++row)
        for (int col = x; col < end_x; ++col) s_pixels[row * WIDTH + col] = color;
}

static void ellipse(int cx, int cy, int rx, int ry, uint16_t color)
{
    if (rx <= 0 || ry <= 0) return;
    for (int y = -ry; y <= ry; ++y) {
        for (int x = -rx; x <= rx; ++x) {
            if (x * x * ry * ry + y * y * rx * rx <= rx * rx * ry * ry)
                rect(cx + x, cy + y, 1, 1, color);
        }
    }
}

// Small built-in 5x7 alphabet; on-screen labels use Vietnamese without accents.
static const uint8_t s_font[26][5] = {
    {0x7e,0x11,0x11,0x11,0x7e}, {0x7f,0x49,0x49,0x49,0x36},
    {0x3e,0x41,0x41,0x41,0x22}, {0x7f,0x41,0x41,0x22,0x1c},
    {0x7f,0x49,0x49,0x49,0x41}, {0x7f,0x09,0x09,0x09,0x01},
    {0x3e,0x41,0x49,0x49,0x7a}, {0x7f,0x08,0x08,0x08,0x7f},
    {0x00,0x41,0x7f,0x41,0x00}, {0x20,0x40,0x41,0x3f,0x01},
    {0x7f,0x08,0x14,0x22,0x41}, {0x7f,0x40,0x40,0x40,0x40},
    {0x7f,0x02,0x0c,0x02,0x7f}, {0x7f,0x04,0x08,0x10,0x7f},
    {0x3e,0x41,0x41,0x41,0x3e}, {0x7f,0x09,0x09,0x09,0x06},
    {0x3e,0x41,0x51,0x21,0x5e}, {0x7f,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7f,0x01,0x01},
    {0x3f,0x40,0x40,0x40,0x3f}, {0x1f,0x20,0x40,0x20,0x1f},
    {0x3f,0x40,0x38,0x40,0x3f}, {0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43},
};
static const uint8_t s_digits[10][5] = {
    {0x3e,0x51,0x49,0x45,0x3e}, {0x00,0x42,0x7f,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4b,0x31},
    {0x18,0x14,0x12,0x7f,0x10}, {0x27,0x45,0x45,0x45,0x39},
    {0x3c,0x4a,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1e},
};

static void text_at_center(const char *text, int center_x, int y, int scale, uint16_t color)
{
    int x = center_x - ((int)strlen(text) * 6 * scale - scale) / 2;
    for (; *text; ++text, x += 6 * scale) {
        const uint8_t *glyph;
        if (*text >= 'A' && *text <= 'Z') glyph = s_font[*text - 'A'];
        else if (*text >= '0' && *text <= '9') glyph = s_digits[*text - '0'];
        else {
            static const uint8_t minus[5] = {8,8,8,8,8};
            static const uint8_t plus[5] = {8,8,0x3e,8,8};
            static const uint8_t colon[5] = {0,0x36,0x36,0,0};
            static const uint8_t percent[5] = {0x63,0x13,8,0x64,0x63};
            if (*text == '-') glyph = minus;
            else if (*text == '+') glyph = plus;
            else if (*text == ':') glyph = colon;
            else if (*text == '%') glyph = percent;
            else continue;
        }
        for (int col = 0; col < 5; ++col)
            for (int row = 0; row < 7; ++row)
                if (glyph[col] & (1 << row)) rect(x + col * scale, y + row * scale, scale, scale, color);
    }
}

static void text_center(const char *text, int y, int scale, uint16_t color)
{
    text_at_center(text, WIDTH / 2, y, scale, color);
}

static void curve(int cx, int cy, int width, int depth, uint16_t color)
{
    for (int x = -width; x <= width; ++x) {
        int y = cy + depth * (width * width - x * x) / (width * width);
        rect(cx + x - 1, y - 2, 3, 5, color);
    }
}

static void format_clock(unsigned seconds, char output[9])
{
    unsigned hours = seconds / 3600, minutes = seconds / 60 % 60, remainder = seconds % 60;
    unsigned i = 0;
    if (hours) {
        output[i++] = '0' + hours / 10 % 10;
        output[i++] = '0' + hours % 10;
        output[i++] = ':';
    }
    output[i++] = '0' + minutes / 10;
    output[i++] = '0' + minutes % 10;
    output[i++] = ':';
    output[i++] = '0' + remainder / 10;
    output[i++] = '0' + remainder % 10;
    output[i] = 0;
}

static void player_button(int x, int y, int width, int height, const char *label, uint16_t color)
{
    rect(x, y, width, height, color);
    rect(x + 2, y + 2, width - 4, height - 4, rgb(5, 10, 22));
    text_at_center(label, x + width / 2, y + (height - 14) / 2, 2, color);
}

static void draw_music_player(robot_status_t status, unsigned frame)
{
    uint16_t bg = rgb(5, 10, 22), muted = rgb(101, 125, 150), accent = rgb(58, 226, 222);
    uint16_t green = rgb(100, 230, 150), red = rgb(255, 100, 110);
    for (int i = 0; i < WIDTH * HEIGHT; ++i) s_pixels[i] = bg;
    bool paused = s_render_view == TOUCH_VIEW_PAUSED;
    const char *state = paused ? "TAM DUNG" : status == ROBOT_MUSIC_BUFFERING ? "DANG TAI NHAC" : "DANG PHAT NHAC";
    if (status == ROBOT_SPEAKING) state = "CA DANG NOI";
    if (status == ROBOT_RECONNECTING || status == ROBOT_CONNECTING_WIFI) state = "DANG NOI LAI MANG";
    if (!s_render_touch_available) state = "CAM UNG CHUA SAN SANG";
    text_center(state, 8, 1, paused ? muted : green);

    // Scroll long titles through a 25-character viewport, without hiding buttons.
    char title[26];
    size_t length = strlen(s_render_title);
    if (length > 25) {
        unsigned start = (frame / 3) % (length + 5);
        for (unsigned i = 0; i < 25; ++i) {
            unsigned position = (start + i) % (length + 5);
            title[i] = position < length ? s_render_title[position] : ' ';
        }
        title[25] = 0;
    } else {
        for (size_t i = 0; i <= length; ++i) title[i] = s_render_title[i];
    }
    text_center(length ? title : "DANG LAY TEN BAI", 27, 2, accent);
    text_center(s_render_artist, 48, 1, muted);
    for (unsigned i = 0; i < SPEAKER_MUSIC_LEVELS; ++i) {
        unsigned level = paused ? 0 : s_render_audio.levels[i];
        if (level > 100) level = 100;
        int height = 2 + level * 52 / 100;
        rect(32 + i * 16, 119 - height, 10, height, i % 2 ? green : accent);
    }

    unsigned elapsed = s_render_audio.played_samples / 16000;
    if (s_render_duration && elapsed > s_render_duration) elapsed = s_render_duration;
    unsigned filled = s_render_duration ? (uint64_t)elapsed * 288 / s_render_duration : 0;
    rect(16, 127, 288, 4, rgb(30, 48, 66));
    rect(16, 127, filled, 4, green);
    char elapsed_text[9], duration_text[9];
    format_clock(elapsed, elapsed_text);
    format_clock(s_render_duration, duration_text);
    text_at_center(elapsed_text, 16 + strlen(elapsed_text) * 3, 136, 1, muted);
    text_at_center(duration_text, 304 - strlen(duration_text) * 3, 136, 1, muted);

    unsigned volume = s_render_audio.volume_percent;
    if (volume > 100) volume = 100;
    text_center("AM LUONG", 151, 1, muted);
    uint16_t control = s_render_touch_available ? accent : muted;
    player_button(TOUCH_VOLUME_MINUS_X, TOUCH_VOLUME_Y, TOUCH_VOLUME_BUTTON_WIDTH, TOUCH_VOLUME_HEIGHT, "-", control);
    player_button(TOUCH_VOLUME_PLUS_X, TOUCH_VOLUME_Y, TOUCH_VOLUME_BUTTON_WIDTH, TOUCH_VOLUME_HEIGHT, "+", control);
    rect(TOUCH_SLIDER_X, 170, TOUCH_SLIDER_WIDTH, 4, rgb(30, 48, 66));
    unsigned slider = volume * (TOUCH_SLIDER_WIDTH - 1) / 100;
    rect(TOUCH_SLIDER_X, 170, slider, 4, control);
    ellipse(TOUCH_SLIDER_X + slider, 172, 6, 6, control);
    char percent[5];
    unsigned n = 0;
    if (volume == 100) percent[n++] = '1';
    if (volume >= 10) percent[n++] = '0' + volume / 10 % 10;
    percent[n++] = '0' + volume % 10;
    percent[n++] = '%';
    percent[n] = 0;
    text_center(percent, 184, 1, control);
    player_button(TOUCH_LEFT_X, TOUCH_BUTTON_Y, TOUCH_BUTTON_WIDTH, TOUCH_BUTTON_HEIGHT,
                  paused ? "TIEP TUC" : "TAM DUNG", control);
    player_button(TOUCH_RIGHT_X, TOUCH_BUTTON_Y, TOUCH_BUTTON_WIDTH, TOUCH_BUTTON_HEIGHT,
                  "DUNG", s_render_touch_available ? red : muted);
}

static void draw_face(robot_status_t status, emotion_t emotion, unsigned frame)
{
    if (s_render_view != TOUCH_VIEW_NONE) { draw_music_player(status, frame); return; }
    uint16_t bg = rgb(5, 10, 22), muted = rgb(101, 125, 150);
    uint16_t ink = rgb(58, 226, 222);
    const char *label;
    switch (status) {
    case ROBOT_BOOTING: label = "KHOI DONG"; break;
    case ROBOT_CONNECTING_WIFI: label = "DANG NOI WIFI"; ink = rgb(255, 190, 64); break;
    case ROBOT_CONNECTING_LIVE: label = "DANG NOI GEMINI"; ink = rgb(255, 190, 64); break;
    case ROBOT_LISTENING: label = "DANG NGHE"; break;
    case ROBOT_THINKING: label = "DANG XU LY"; ink = rgb(160, 140, 255); break;
    case ROBOT_SPEAKING: label = "DANG TRA LOI"; break;
    case ROBOT_RECONNECTING: label = "DANG NOI LAI"; ink = rgb(255, 190, 64); break;
    case ROBOT_MUSIC_SEARCHING: label = "DANG TIM NHAC"; break;
    case ROBOT_MUSIC_BUFFERING: label = "DANG TAI NHAC"; ink = rgb(255, 190, 64); break;
    case ROBOT_MUSIC_PLAYING: label = "DANG PHAT NHAC"; ink = rgb(100, 230, 150); break;
    case ROBOT_MUSIC_PAUSED: label = "TAM DUNG NHAC"; break;
    case ROBOT_MUSIC_ERROR: label = "LOI PHAT NHAC"; ink = rgb(255, 100, 110); break;
    default: label = "LOI HE THONG"; ink = rgb(255, 100, 110); break;
    }
    for (int i = 0; i < WIDTH * HEIGHT; ++i) s_pixels[i] = bg;
    text_center("CA", 12, 2, ink);
    const int face_x = WIDTH / 2;
    const int mouth_y = 154;
    bool fault = status == ROBOT_ERROR || status == ROBOT_RECONNECTING || status == ROBOT_MUSIC_ERROR;
    if (fault) emotion = SAD;
    bool blink = frame % 43 == 0 || frame % 43 == 1;
    int offset = status == ROBOT_THINKING ? 5 : (int)((frame / 18) % 3) * 2 - 2;
    for (int eye = 0; eye < 2; ++eye) {
        int cx = face_x + (eye ? 66 : -66) + offset, cy = 98;
        if (blink || emotion == SLEEPY) {
            rect(cx - 32, cy, 64, 5, ink);
        } else if (emotion == HAPPY) {
            curve(cx, cy + 6, 34, -23, ink);
        } else if (emotion == SAD) {
            ellipse(cx, cy + 4, 30, 18, ink);
            // Sloping upper lids make the inner corners look raised.
            for (int x = -33; x <= 33; ++x) {
                int lid = cy - 6 + (eye ? x : -x) / 3;
                rect(cx + x, cy - 25, 1, lid - (cy - 25), bg);
            }
        } else if (emotion == SURPRISED) {
            ellipse(cx, cy, 32, 34, ink);
            ellipse(cx, cy, 18, 22, bg);
        } else {
            ellipse(cx, cy, 34, 30, ink);
            ellipse(cx - 9, cy - 10, 8, 8, rgb(200, 255, 255));
        }
    }
    if (status == ROBOT_SPEAKING) {
        // Decorative motion follows playback state, not measured audio volume.
        ellipse(face_x, mouth_y, 14 + (frame % 3) * 3, 5 + (frame % 4) * 3, ink);
    } else if (emotion == SURPRISED) {
        ellipse(face_x, mouth_y, 10, 15, ink);
        ellipse(face_x, mouth_y, 5, 9, bg);
    } else if (emotion == SAD) {
        curve(face_x, mouth_y + 10, 29, -9, ink);
    } else if (emotion == SLEEPY) {
        rect(face_x - 18, mouth_y, 36, 4, ink);
    } else {
        curve(face_x, mouth_y - 8, 32, emotion == HAPPY ? 16 : 8, ink);
    }
    text_center(label, 192, 2, ink);
    const char *mood[] = {"BINH THUONG", "VUI VE", "BUON", "NGAC NHIEN", "BUON NGU"};
    bool music = status >= ROBOT_MUSIC_BUFFERING && status <= ROBOT_MUSIC_ERROR;
    text_center(music ? s_render_title : mood[emotion], 216, 1, muted);
    for (int i = 0; i < 3; ++i)
        ellipse(face_x - 12 + i * 12, 233, 2, 2, i == (frame / 3) % 3 ? ink : muted);
}

static bool color_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *event, void *context)
{
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR(s_transfer_done, &wake);
    return wake == pdTRUE;
}

static esp_err_t flush_frame(void)
{
    const uint8_t columns[] = {0, 0, (WIDTH - 1) >> 8, (WIDTH - 1) & 255};
    esp_err_t ret = esp_lcd_panel_io_tx_param(s_io, 0x2a, columns, sizeof(columns));
    if (ret != ESP_OK) return ret;
    for (int y = 0; y < HEIGHT; y += STRIPE_ROWS) {
        uint8_t rows[] = {y >> 8, y & 255, (y + STRIPE_ROWS - 1) >> 8, (y + STRIPE_ROWS - 1) & 255};
        ret = esp_lcd_panel_io_tx_param(s_io, 0x2b, rows, sizeof(rows));
        if (ret != ESP_OK) return ret;
        memcpy(s_stripe, s_pixels + y * WIDTH, WIDTH * STRIPE_ROWS * sizeof(uint16_t));
        ret = esp_lcd_panel_io_tx_color(s_io, 0x2c, s_stripe, WIDTH * STRIPE_ROWS * sizeof(uint16_t));
        if (ret != ESP_OK) return ret;
        // Never reuse DMA memory until its completion callback has fired.
        if (xSemaphoreTake(s_transfer_done, pdMS_TO_TICKS(1000)) != pdTRUE) return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static void display_task(void *arg)
{
    unsigned frame = 0;
    for (;;) {
        nct_track_t track;
        music_get_track(&track);
        display_ascii(track.title, s_render_title, sizeof(s_render_title));
        display_ascii(track.artist, s_render_artist, sizeof(s_render_artist));
        s_render_duration = track.duration;
        speaker_music_snapshot(&s_render_audio);
        music_state_t music_state_now = music_state();
        s_render_view = music_state_now == MUSIC_PAUSED ? TOUCH_VIEW_PAUSED :
                        music_state_now == MUSIC_BUFFERING || music_state_now == MUSIC_PLAYING ? TOUCH_VIEW_ACTIVE : TOUCH_VIEW_NONE;
        s_render_touch_available = touch_input_available();
        TickType_t now = xTaskGetTickCount();
        portENTER_CRITICAL(&s_emotion_lock);
        emotion_t emotion = now - s_emotion_since < pdMS_TO_TICKS(EMOTION_MS) ? s_emotion : NEUTRAL;
        portEXIT_CRITICAL(&s_emotion_lock);
        robot_status_t status = atomic_load(&s_status);
        if (!atomic_load(&s_wifi) && status != ROBOT_BOOTING && status != ROBOT_ERROR)
            status = ROBOT_CONNECTING_WIFI;
        draw_face(status, emotion, frame++);
        esp_err_t ret = flush_frame();
        if (ret != ESP_OK) {
            atomic_store(&s_touch_view, TOUCH_VIEW_NONE);
            atomic_store(&s_available, false);
            gpio_set_level(LCD_BL, 0);
            ESP_LOGE(TAG, "LCD transfer failed: %s; voice chat continues", esp_err_to_name(ret));
            // Keep DMA-owned resources alive on timeout; the task ends safely.
            vTaskDelete(NULL);
        }
        atomic_store(&s_touch_view, s_render_touch_available ? s_render_view : TOUCH_VIEW_NONE);
        gpio_set_level(LCD_BL, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

esp_err_t robot_face_start(void)
{
    if (s_io) return ESP_ERR_INVALID_STATE;
    bool bus_ready = false;
    esp_err_t ret;
    gpio_config_t backlight = {.pin_bit_mask = 1ULL << LCD_BL, .mode = GPIO_MODE_OUTPUT};
    ret = gpio_config(&backlight);
    if (ret != ESP_OK) return ret;
    gpio_set_level(LCD_BL, 0);
    s_transfer_done = xSemaphoreCreateBinary();
    s_pixels = heap_caps_malloc(WIDTH * HEIGHT * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_stripe = heap_caps_malloc(WIDTH * STRIPE_ROWS * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!s_transfer_done || !s_pixels || !s_stripe) { ret = ESP_ERR_NO_MEM; goto fail; }
    spi_bus_config_t bus = {
        .mosi_io_num = GPIO_NUM_11, .miso_io_num = -1, .sclk_io_num = GPIO_NUM_12,
        .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = WIDTH * STRIPE_ROWS * sizeof(uint16_t),
    };
    ret = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) goto fail;
    bus_ready = true;
    esp_lcd_panel_io_spi_config_t io = {
        .cs_gpio_num = GPIO_NUM_10, .dc_gpio_num = GPIO_NUM_46,
        .spi_mode = 0, .pclk_hz = 40 * 1000 * 1000, .trans_queue_depth = 1,
        .lcd_cmd_bits = 8, .lcd_param_bits = 8, .on_color_trans_done = color_done,
    };
    ret = esp_lcd_new_panel_io_spi(SPI2_HOST, &io, &s_io);
    if (ret != ESP_OK) goto fail;
    // LCD reset is wired to ESP32 EN. Use a software reset, never toggle EN.
    ret = esp_lcd_panel_io_tx_param(s_io, 0x01, NULL, 0);
    if (ret != ESP_OK) goto fail;
    vTaskDelay(pdMS_TO_TICKS(150));
    // LCDWIKI ES3C28P ILI9341V_Init.txt (IPS, RGB565), rotated to landscape.
    static const struct { uint8_t command, count, data[15]; } init[] = {
        {0xcf,3,{0x00,0xc1,0x30}}, {0xed,4,{0x64,0x03,0x12,0x81}},
        {0xe8,3,{0x85,0x00,0x78}}, {0xcb,5,{0x39,0x2c,0x00,0x34,0x02}},
        {0xf7,1,{0x20}}, {0xea,2,{0x00,0x00}}, {0xc0,1,{0x13}}, {0xc1,1,{0x13}},
        {0xc5,2,{0x22,0x35}}, {0xc7,1,{0xbd}}, {0x21,0,{0}}, {0x36,1,{LCD_MADCTL}},
        {0xb6,2,{0x0a,0xa2}}, {0x3a,1,{0x55}}, {0xf6,2,{0x01,0x30}},
        {0xb1,2,{0x00,0x1b}}, {0xf2,1,{0x00}}, {0x26,1,{0x01}},
        {0xe0,15,{0x0f,0x35,0x31,0x0b,0x0e,0x06,0x49,0xa7,0x33,0x07,0x0f,0x03,0x0c,0x0a,0x00}},
        {0xe1,15,{0x00,0x0a,0x0f,0x04,0x11,0x08,0x36,0x58,0x4d,0x07,0x10,0x0c,0x32,0x34,0x0f}},
        {0x11,0,{0}},
    };
    for (size_t i = 0; i < sizeof(init) / sizeof(init[0]); ++i) {
        ret = esp_lcd_panel_io_tx_param(s_io, init[i].command, init[i].data, init[i].count);
        if (ret != ESP_OK) goto fail;
    }
    vTaskDelay(pdMS_TO_TICKS(120));
    ret = esp_lcd_panel_io_tx_param(s_io, 0x29, NULL, 0);
    if (ret != ESP_OK) goto fail;
    atomic_store(&s_available, true);
    if (xTaskCreate(display_task, "robot_face", 4096, NULL, 1, NULL) != pdPASS) {
        ret = ESP_ERR_NO_MEM;
        goto fail;
    }
    ESP_LOGI(TAG, "ILI9341 %dx%d landscape ready, MADCTL=0x%02x", WIDTH, HEIGHT, LCD_MADCTL);
    return ESP_OK;
fail:
    atomic_store(&s_available, false);
    if (s_io) { esp_lcd_panel_io_del(s_io); s_io = NULL; }
    if (bus_ready) spi_bus_free(SPI2_HOST);
    if (s_transfer_done) { vSemaphoreDelete(s_transfer_done); s_transfer_done = NULL; }
    free(s_pixels); s_pixels = NULL;
    free(s_stripe); s_stripe = NULL;
    return ret;
}

void robot_face_set_status(robot_status_t status) { atomic_store(&s_status, status); }
void robot_face_set_wifi(bool connected) { atomic_store(&s_wifi, connected); }
bool robot_face_is_available(void) { return atomic_load(&s_available); }
touch_view_t robot_face_touch_view(void) { return atomic_load(&s_touch_view); }

bool robot_face_set_emotion(const char *emotion)
{
    if (!emotion || !robot_face_is_available()) return false;
    for (size_t i = 0; i < sizeof(s_emotions) / sizeof(s_emotions[0]); ++i) {
        if (strcmp(emotion, s_emotions[i]) != 0) continue;
        portENTER_CRITICAL(&s_emotion_lock);
        s_emotion = (emotion_t)i;
        s_emotion_since = xTaskGetTickCount();
        portEXIT_CRITICAL(&s_emotion_lock);
        return true;
    }
    return false;
}
