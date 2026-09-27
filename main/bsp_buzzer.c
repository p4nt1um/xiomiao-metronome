/*
 * GPIO14 无源蜂鸣器 LEDC 驱动，tone(freq, duty, duration) 语义。
 * 基于参考工程蜂鸣器代码改造：停声由单次 esp_timer 完成，
 * 不依赖 LVGL 循环，可在 esp_timer 回调上下文安全调用。
 */

#include <stdbool.h>
#include <stdint.h>

#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#include "bsp_buzzer.h"

#define PIN_NUM_BUZZER             GPIO_NUM_14

#define BUZZER_LEDC_MODE           LEDC_LOW_SPEED_MODE
#define BUZZER_LEDC_TIMER          LEDC_TIMER_0
#define BUZZER_LEDC_CHANNEL        LEDC_CHANNEL_0

static const char *s_tag = "bsp_buzzer";

static esp_timer_handle_t s_tone_stop_timer;

static void tone_stop_cb(void *arg)
{
    (void)arg;
    ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, 0);
    ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL);
}

void bsp_buzzer_init(void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode = BUZZER_LEDC_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = BUZZER_LEDC_TIMER,
        .freq_hz = 880,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    ledc_channel_config_t channel_cfg = {
        .gpio_num = PIN_NUM_BUZZER,
        .speed_mode = BUZZER_LEDC_MODE,
        .channel = BUZZER_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BUZZER_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel_cfg));

    const esp_timer_create_args_t stop_timer_args = {
        .callback = tone_stop_cb,
        .name = "tone_stop",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&stop_timer_args, &s_tone_stop_timer));
}

void bsp_buzzer_tone(uint32_t freq_hz, uint8_t duty, uint32_t duration_ms)
{
    if (duty == 0 || duration_ms == 0) {
        return;
    }

    esp_err_t err = ledc_set_freq(BUZZER_LEDC_MODE, BUZZER_LEDC_TIMER, freq_hz);
    if (err != ESP_OK) {
        ESP_LOGW(s_tag, "set freq %lu failed: %s", (unsigned long)freq_hz, esp_err_to_name(err));
        return;
    }
    err = ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, duty);
    if (err == ESP_OK) {
        err = ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL);
    }
    if (err != ESP_OK) {
        ESP_LOGW(s_tag, "set duty failed: %s", esp_err_to_name(err));
        return;
    }

    /* 重设停声时刻：tone 回调与停声回调都在 esp_timer 任务上下文，天然串行 */
    esp_timer_stop(s_tone_stop_timer);
    esp_timer_start_once(s_tone_stop_timer, (uint64_t)duration_ms * 1000);
}

void bsp_buzzer_stop(void)
{
    esp_timer_stop(s_tone_stop_timer);
    tone_stop_cb(NULL);
}
