/*
 * 超时自动关机（省电）。
 * 判定：引擎非 RUNNING 且 5 分钟无按键活动，且当前 6 键全部松开
 * （按住的键不算空闲——否则入睡瞬间 ext1 电平唤醒会造成开机循环）。
 * 睡眠：深度睡眠，ext1(GPIO34=A键, ALL_LOW) 唤醒 = 重新开机，NVS 设置保留。
 * 已知边界：背光直连电源、GD32 协处理器常供电，二者底耗固件侧无法消除。
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "app_settings.h"
#include "bsp_buttons.h"
#include "bsp_buzzer.h"
#include "bsp_lcd.h"
#include "bsp_led.h"
#include "metro_engine.h"
#include "power_ctl.h"

#define POWER_CTL_TIMEOUT_MS    (5 * 60 * 1000)
/* 告别音固定占空比：音量 0 档时按音量映射 duty=0 会完全无声 */
#define FAREWELL_DUTY           96
#define FAREWELL_TONE_MS        120
#define FAREWELL_WAIT_MS        300

static const char *TAG = "power_ctl";

static uint32_t s_idle_since_ms;

void power_ctl_init(void)
{
    s_idle_since_ms = lv_tick_get();
}

void power_ctl_reset(void)
{
    s_idle_since_ms = lv_tick_get();
}

void power_ctl_poll(bool engine_running)
{
    if (engine_running || bsp_buttons_any_pressed()) {
        s_idle_since_ms = lv_tick_get();
        return;
    }
    if (app_settings_get_auto_off() > 0 &&
        lv_tick_elaps(s_idle_since_ms) >= POWER_CTL_TIMEOUT_MS) {
        power_ctl_sleep_now();
    }
}

void power_ctl_sleep_now(void)
{
    ESP_LOGI(TAG, "idle %d ms, entering deep sleep (wake: A key)", POWER_CTL_TIMEOUT_MS);

    metro_engine_stop();      /* PAUSED 态安全；STOPPED 态内部早退 */
    bsp_buzzer_stop();
    bsp_led_set(1, false);    /* GD32 侧 LED 需显式熄灭，深睡后 I2C 不可达 */
    bsp_led_set(2, false);
    app_settings_flush();     /* 防抖中的设置改动立即落盘 */

    bsp_buzzer_tone(660, FAREWELL_DUTY, FAREWELL_TONE_MS); /* 低音告别 */
    vTaskDelay(pdMS_TO_TICKS(FAREWELL_WAIT_MS));           /* 等音播完再睡 */

    bsp_lcd_display_off();
    bsp_lcd_sleep_in();

    /* A 键 = GPIO34：RTC 域输入脚，外部上拉，低电平按下 */
    esp_sleep_enable_ext1_wakeup(BIT64(GPIO_NUM_34), ESP_EXT1_WAKEUP_ALL_LOW);
    esp_deep_sleep_start();   /* 不返回 */
}

void power_ctl_boot_wake_check(void)
{
    const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    if (cause == ESP_SLEEP_WAKEUP_EXT1) {
        ESP_LOGI(TAG, "woken from deep sleep by A key");
    }
    else if (cause != ESP_SLEEP_WAKEUP_UNDEFINED) {
        ESP_LOGI(TAG, "wakeup cause: %d", cause);
    }
}
