/*
 * 光照突变检测：遮一下切换（手势）。
 *
 * 算法（50ms 采样节拍）：
 * - fast：快速 EMA（约 200ms 时间常数）去噪
 * - baseline：慢速 EMA（约 2s）跟踪环境光
 * - 变暗检测：fast 相对 baseline 下降 >DARK_TH 且持续 >=DARK_CONFIRM_MS
 *   → 产生一次 TOGGLE 事件；随后 REFRACTORY_MS 不应期，并把 baseline 重锚到当前
 *   fast（此时仍处于遮住状态，暗电平成为新基线）
 * - 变亮检测：fast 高于 baseline×UP_TH 持续 >=UP_CONFIRM_MS → 仅把 baseline
 *   向上重锚（拿开手 / 开灯不触发，且保证随后快速再遮不漏检）
 * - 启用后先标定 CALIB_MS 不产生事件
 * 基线下限保护：极暗环境下不参与相对比较（避免除小数抖动）。
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_log.h"
#include "lvgl.h"

#include "bsp_light.h"
#include "light_ctl.h"

#define TAG "light_ctl"

#define SAMPLE_PERIOD_MS    50
#define FAST_ALPHA          0.20f   /* 20Hz 下约 200ms 时间常数 */
#define SLOW_ALPHA          0.025f  /* 20Hz 下约 2s 时间常数 */
#define DARK_TH             0.35f   /* 相对基线变暗 35% */
#define DARK_CONFIRM_MS     600
#define REFRACTORY_MS       1500
#define UP_TH               1.30f   /* 高于基线 30% 视为变亮 */
#define UP_CONFIRM_MS       300
#define CALIB_MS            2000
#define BASELINE_FLOOR      200     /* 基线低于此值不判变暗（太暗） */

enum {
    ST_CALIB = 0,
    ST_IDLE,
    ST_DARK_CAND,
    ST_REFRACT,
    ST_UP_CAND,
};

static struct {
    uint8_t state;
    float fast;
    float baseline;
    uint32_t last_sample_ms;
    uint32_t state_since_ms;
    uint32_t last_log_ms;
    bool valid;
} s;

void light_ctl_init(void)
{
    s.state = ST_CALIB;
    s.valid = false;
    s.last_sample_ms = 0;
}

/* 进入/退出候选态的小工具 */
static void enter_state(uint8_t st)
{
    s.state = st;
    s.state_since_ms = lv_tick_get();
}

static uint32_t in_state_ms(void)
{
    return lv_tick_elaps(s.state_since_ms);
}

light_ctl_event_t light_ctl_poll(bool enabled)
{
    const uint32_t now = lv_tick_get();

    if (!enabled) {
        s.valid = false;
        s.state = ST_CALIB;
        return LIGHT_CTL_NONE;
    }
    if (s.last_sample_ms != 0 && lv_tick_elaps(s.last_sample_ms) < SAMPLE_PERIOD_MS) {
        return LIGHT_CTL_NONE;
    }
    s.last_sample_ms = now;

    const int raw = bsp_light_read_raw();
    if (raw < 0) {
        return LIGHT_CTL_NONE;
    }

    if (!s.valid) {
        s.valid = true;
        s.fast = (float)raw;
        s.baseline = (float)raw;
        enter_state(ST_CALIB);
    }
    else {
        s.fast += ((float)raw - s.fast) * FAST_ALPHA;
        s.baseline += (s.fast - s.baseline) * SLOW_ALPHA;
    }

    /* 1Hz 心跳日志：观察原始值与基线（标定/排障用） */
    if (now - s.last_log_ms >= 1000) {
        s.last_log_ms = now;
        ESP_LOGI(TAG, "raw=%d fast=%d base=%d st=%d", raw,
                 (int)s.fast, (int)s.baseline, s.state);
    }

    switch (s.state) {
    case ST_CALIB:
        if (in_state_ms() >= CALIB_MS) {
            enter_state(ST_IDLE);
        }
        break;

    case ST_IDLE:
    case ST_REFRACT:
        if (s.state == ST_REFRACT && in_state_ms() < REFRACTORY_MS) {
            break;
        }
        if (s.state == ST_REFRACT) {
            enter_state(ST_IDLE);
        }
        if (s.baseline > BASELINE_FLOOR && s.fast < s.baseline * (1.0f - DARK_TH)) {
            enter_state(ST_DARK_CAND);
        }
        else if (s.fast > s.baseline * UP_TH) {
            enter_state(ST_UP_CAND);
        }
        break;

    case ST_DARK_CAND:
        if (s.fast >= s.baseline * (1.0f - DARK_TH)) {
            enter_state(ST_IDLE); /* 抖动回落，放弃 */
        }
        else if (in_state_ms() >= DARK_CONFIRM_MS) {
            ESP_LOGI(TAG, "DARK gesture: base=%d fast=%d", (int)s.baseline, (int)s.fast);
            s.baseline = s.fast; /* 暗电平成为新基线（仍处于遮住中） */
            enter_state(ST_REFRACT);
            return LIGHT_CTL_TOGGLE;
        }
        break;

    case ST_UP_CAND:
        if (s.fast <= s.baseline * UP_TH) {
            enter_state(ST_IDLE);
        }
        else if (in_state_ms() >= UP_CONFIRM_MS) {
            ESP_LOGI(TAG, "UP re-anchor: base=%d fast=%d", (int)s.baseline, (int)s.fast);
            s.baseline = s.fast;
            enter_state(ST_IDLE);
        }
        break;

    default:
        enter_state(ST_IDLE);
        break;
    }

    return LIGHT_CTL_NONE;
}
