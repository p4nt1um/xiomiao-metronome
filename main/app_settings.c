/*
 * 设置持久化：NVS 读写 + 防抖落盘。
 * 写入方只有 LVGL 任务（改设置），读取方为 app_main 初始化，无并发问题。
 */

#include <stdbool.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "app_settings.h"

#define TAG "app_settings"

#define NVS_NAMESPACE       "metro"
#define SAVE_DEBOUNCE_US    (800 * 1000)

static metro_cfg_t s_cfg = {
    .bpm = 120,
    .beats_per_bar = 4,
    .subdiv = 1,
    .accent = true,
    .volume = 6,
    .mute_play = 3,
    .mute_rest = 0,
};
static int s_timer_min;
static int s_light_ctrl = 1;
static int s_led_follow = 1;
static esp_timer_handle_t s_save_timer;

static void load_from_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGW(TAG, "no saved settings, using defaults");
        return;
    }

    int32_t v32 = 0;
    uint8_t u8 = 0;
    if (nvs_get_i32(h, "bpm", &v32) == ESP_OK) {
        s_cfg.bpm = (int)v32;
    }
    if (nvs_get_i32(h, "beats", &v32) == ESP_OK) {
        s_cfg.beats_per_bar = (int)v32;
    }
    if (nvs_get_i32(h, "subdiv", &v32) == ESP_OK) {
        s_cfg.subdiv = (int)v32;
    }
    if (nvs_get_i32(h, "vol", &v32) == ESP_OK) {
        s_cfg.volume = (int)v32;
    }
    if (nvs_get_i32(h, "mplay", &v32) == ESP_OK) {
        s_cfg.mute_play = (int)v32;
    }
    if (nvs_get_i32(h, "mrest", &v32) == ESP_OK) {
        s_cfg.mute_rest = (int)v32;
    }
    if (nvs_get_i32(h, "timer", &v32) == ESP_OK) {
        s_timer_min = (int)v32;
    }
    uint8_t lctrl = 0;
    if (nvs_get_u8(h, "lctrl", &lctrl) == ESP_OK) {
        s_light_ctrl = (lctrl != 0);
    }
    uint8_t ledf = 0;
    if (nvs_get_u8(h, "ledf", &ledf) == ESP_OK) {
        s_led_follow = (ledf != 0);
    }
    if (nvs_get_u8(h, "accent", &u8) == ESP_OK) {
        s_cfg.accent = (u8 != 0);
    }
    nvs_close(h);
    metro_engine_clamp_cfg(&s_cfg);
    ESP_LOGI(TAG, "loaded: bpm=%d beats=%d sub=%d accent=%d vol=%d mute=%d+%d timer=%d",
             s_cfg.bpm, s_cfg.beats_per_bar, s_cfg.subdiv, s_cfg.accent, s_cfg.volume,
             s_cfg.mute_play, s_cfg.mute_rest, s_timer_min);
}

static void save_now(void *arg)
{
    (void)arg;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return;
    }
    nvs_set_i32(h, "bpm", s_cfg.bpm);
    nvs_set_i32(h, "beats", s_cfg.beats_per_bar);
    nvs_set_i32(h, "subdiv", s_cfg.subdiv);
    nvs_set_i32(h, "vol", s_cfg.volume);
    nvs_set_i32(h, "mplay", s_cfg.mute_play);
    nvs_set_i32(h, "mrest", s_cfg.mute_rest);
    nvs_set_i32(h, "timer", s_timer_min);
    nvs_set_u8(h, "lctrl", s_light_ctrl ? 1 : 0);
    nvs_set_u8(h, "ledf", s_led_follow ? 1 : 0);
    nvs_set_u8(h, "accent", s_cfg.accent ? 1 : 0);
    err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit failed: %s", esp_err_to_name(err));
    }
    else {
        ESP_LOGI(TAG, "saved: bpm=%d beats=%d sub=%d accent=%d vol=%d mute=%d+%d timer=%d",
                 s_cfg.bpm, s_cfg.beats_per_bar, s_cfg.subdiv, s_cfg.accent, s_cfg.volume,
                 s_cfg.mute_play, s_cfg.mute_rest, s_timer_min);
    }
}

void app_settings_init(void)
{
    load_from_nvs();

    const esp_timer_create_args_t args = {
        .callback = save_now,
        .name = "settings_save",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&args, &s_save_timer));
}

void app_settings_get(metro_cfg_t *out)
{
    *out = s_cfg;
}

void app_settings_set(const metro_cfg_t *cfg)
{
    s_cfg = *cfg;
    metro_engine_clamp_cfg(&s_cfg);
    esp_timer_stop(s_save_timer);
    esp_timer_start_once(s_save_timer, SAVE_DEBOUNCE_US);
}

int app_settings_get_light_ctrl(void)
{
    return s_light_ctrl;
}

void app_settings_set_light_ctrl(int on)
{
    s_light_ctrl = on ? 1 : 0;
    esp_timer_stop(s_save_timer);
    esp_timer_start_once(s_save_timer, SAVE_DEBOUNCE_US);
}

int app_settings_get_led_follow(void)
{
    return s_led_follow;
}

void app_settings_set_led_follow(int on)
{
    s_led_follow = on ? 1 : 0;
    esp_timer_stop(s_save_timer);
    esp_timer_start_once(s_save_timer, SAVE_DEBOUNCE_US);
}

int app_settings_get_timer_min(void)
{
    return s_timer_min;
}

void app_settings_set_timer_min(int minutes)
{
    s_timer_min = minutes;
    esp_timer_stop(s_save_timer);
    esp_timer_start_once(s_save_timer, SAVE_DEBOUNCE_US);
}
