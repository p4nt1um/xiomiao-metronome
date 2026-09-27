/*
 * xiomiao-metronome — 音乐节拍器（小喵掌机）
 *
 * app_main：初始化 bsp / 设置 / 引擎，启动 LVGL 任务（事件泵 + 渲染循环）。
 * 页面与按键交互见 ui.c，节拍定时与发声见 metro_engine.c。
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/param.h>
#include <unistd.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs_flash.h"

#include "app_settings.h"
#include "bsp_buttons.h"
#include "bsp_buzzer.h"
#include "bsp_lcd.h"
#include "bsp_light.h"
#include "light_ctl.h"
#include "metro_engine.h"
#include "ui.h"

#define LVGL_TASK_STACK_SIZE    (10 * 1024)
#define LVGL_TASK_PRIORITY      5
#define LVGL_TASK_MIN_DELAY_MS  1
#define LVGL_TASK_MAX_DELAY_MS  16

/* 精度自测模式：开机 2 秒后以 120BPM 静音自动启动，串口输出拍点时间戳。
 * 仅用于长时间精度测量，正式固件保持 0。 */
#define METRO_PRECISION_TEST    0

static const char *TAG = "metronome";

static void lvgl_task(void *arg)
{
    lv_group_t *group = (lv_group_t *)arg;

    ui_init(group);
    lv_refr_now(NULL);
    for (int i = 0; i < 100 && !bsp_lcd_first_flush_done(); ++i) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    bsp_lcd_display_on();
    ESP_LOGI(TAG, "UI ready");

    /* 开机提示音（音量跟随设置） */
    metro_cfg_t cfg;
    app_settings_get(&cfg);
    const uint8_t duty = (uint8_t)(cfg.volume * 255 / METRO_VOL_MAX);
    bsp_buzzer_tone(1046, duty, 80);
    vTaskDelay(pdMS_TO_TICKS(120));
    bsp_buzzer_tone(1568, duty, 80);

#if METRO_PRECISION_TEST
    ESP_LOGI(TAG, "PRECISION TEST: auto start 120bpm silent");
    const metro_cfg_t test_cfg = {
        .bpm = 120,
        .beats_per_bar = 4,
        .subdiv = 1,
        .accent = true,
        .volume = 0, /* 静音测试，只记录时间戳 */
        .mute_play = 8,
        .mute_rest = 0,
    };
    metro_engine_set_cfg(&test_cfg);
    vTaskDelay(pdMS_TO_TICKS(2000));
    metro_engine_start();
#endif

    QueueHandle_t q = metro_engine_event_queue();
    metro_beat_event_t ev;
    while (true) {
        ui_poll();
        while (xQueueReceive(q, &ev, 0) == pdTRUE) {
            ui_on_beat(&ev);
        }
        uint32_t delay_ms = lv_timer_handler();
        delay_ms = MAX(delay_ms, LVGL_TASK_MIN_DELAY_MS);
        delay_ms = MIN(delay_ms, LVGL_TASK_MAX_DELAY_MS);
        usleep(delay_ms * 1000);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "xiomiao-metronome boot (M1)");

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    app_settings_init();
    bsp_buttons_init();
    bsp_buzzer_init();
    bsp_light_init();
    light_ctl_init();
    bsp_lcd_init();

    lv_init();
    lv_display_t *display = bsp_lcd_lvgl_display_init();
    bsp_lcd_register_flush_ready_cb(display);
    lv_group_t *group = bsp_buttons_lvgl_indev_init(display);
    bsp_lcd_start_lvgl_tick();

    metro_cfg_t cfg;
    app_settings_get(&cfg);
    metro_engine_init(&cfg);

    BaseType_t ret = xTaskCreate(lvgl_task, "lvgl", LVGL_TASK_STACK_SIZE, group,
                                 LVGL_TASK_PRIORITY, NULL);
    ESP_ERROR_CHECK(ret == pdPASS ? ESP_OK : ESP_FAIL);
}
