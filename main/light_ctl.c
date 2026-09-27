/*
 * 光照突变检测：遮一下切换（手势）。
 *
 * V1.2 修订：两级判定，解决「拿起误触发」与「手指遮不严漏检」。
 *  - 拿起/身体阴影：中等幅度变暗（~40-50%），到不了深暗线 → 观察窗超时放弃并重锚基线
 *  - 真实遮挡（手指/手掌贴上）：深暗（变暗 ≥65%）→ 保持 250ms 即触发
 *
 * 算法（50ms 采样节拍）：
 * - fast：快速 EMA（约 150ms 时间常数）去噪；光敏电阻响应慢，EMA 不宜再快
 * - baseline：慢速 EMA（约 2s）跟踪环境光
 * - IDLE → CAND：fast < baseline×CAND_TH（变暗 ≥20%，含一切可能手势）
 * - CAND → 触发：fast < baseline×DEEP_TH（变暗 ≥65%）持续 ≥DEEP_HOLD_MS
 *   → TOGGLE；随后 REFRACTORY_MS 不应期，baseline 重锚到当前 fast
 * - CAND 超过 CAND_WINDOW_MS 未达深暗 → 环境渐变（拿起/云影），baseline=fast 重锚放弃
 * - 变亮：fast > baseline×UP_TH 持续 UP_CONFIRM_MS → 仅向上重锚（拿开手/开灯不触发，
 *   且保证随后快速再遮不漏检）
 * - 启用后先标定 CALIB_MS 不产生事件
 * - 非 IDLE 态逐采样日志（标定/排障），IDLE 态 1Hz 心跳日志
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_log.h"
#include "lvgl.h"

#include "bsp_light.h"
#include "light_ctl.h"

#define TAG "light_ctl"

#define SAMPLE_PERIOD_MS    50
#define FAST_ALPHA          0.25f   /* 20Hz 下约 150ms 时间常数 */
#define SLOW_ALPHA          0.025f  /* 20Hz 下约 2s 时间常数 */
#define CAND_TH             0.80f   /* 相对基线变暗 20%：进入观察 */
#define DEEP_TH             0.35f   /* 相对基线变暗 65%：判定真实遮挡 */
#define DEEP_HOLD_MS        250
#define CAND_WINDOW_MS      1200    /* 观察窗：未达深暗视为环境渐变，放弃 */
#define REFRACTORY_MS       1500
#define UP_TH               1.30f   /* 高于基线 30% 视为变亮 */
#define UP_CONFIRM_MS       300
#define CALIB_MS            2000
#define BASELINE_FLOOR      150     /* 基线低于此值不判变暗（太暗） */

enum {
    ST_CALIB = 0,
    ST_IDLE,
    ST_CAND,
    ST_DEEP,
    ST_REFRACT,
    ST_UP_CAND,
};

static struct {
    uint8_t state;
    float fast;
    float baseline;
    uint32_t last_sample_ms;
    uint32_t state_since_ms;    /* 当前状态进入时刻 */
    uint32_t cand_since_ms;     /* 观察窗起点（CAND/DEEP 共用） */
    uint32_t last_log_ms;
    bool valid;
} s;

void light_ctl_init(void)
{
    s.state = ST_CALIB;
    s.valid = false;
    s.last_sample_ms = 0;
}

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

    /* 日志：IDLE/CALIB 1Hz 心跳；手势过程逐采样 */
    if (s.state == ST_IDLE || s.state == ST_CALIB) {
        if (now - s.last_log_ms >= 1000) {
            s.last_log_ms = now;
            ESP_LOGI(TAG, "raw=%d fast=%d base=%d st=%d", raw,
                     (int)s.fast, (int)s.baseline, s.state);
        }
    }
    else {
        ESP_LOGI(TAG, "[g] raw=%d fast=%d base=%d st=%d", raw,
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
        if (s.baseline > BASELINE_FLOOR && s.fast < s.baseline * CAND_TH) {
            s.cand_since_ms = now;
            ESP_LOGI(TAG, "cand enter: fast=%d base=%d", (int)s.fast, (int)s.baseline);
            enter_state(ST_CAND);
        }
        else if (s.fast > s.baseline * UP_TH) {
            enter_state(ST_UP_CAND);
        }
        break;

    case ST_CAND:
        if (s.fast >= s.baseline * CAND_TH) {
            enter_state(ST_IDLE); /* 快速回升，抖动 */
        }
        else if (s.fast < s.baseline * DEEP_TH) {
            enter_state(ST_DEEP); /* 到达深暗 */
        }
        else if (now - s.cand_since_ms >= CAND_WINDOW_MS) {
            /* 观察窗耗尽仍只是中等变暗：环境渐变（拿起/云影），重锚放弃 */
            ESP_LOGI(TAG, "cand abort (gradual): fast=%d base=%d", (int)s.fast, (int)s.baseline);
            s.baseline = s.fast;
            enter_state(ST_IDLE);
        }
        break;

    case ST_DEEP:
        if (s.fast >= s.baseline * DEEP_TH) {
            enter_state(ST_CAND); /* 深暗中回升，退回观察（窗口继续） */
        }
        else if (in_state_ms() >= DEEP_HOLD_MS) {
            ESP_LOGI(TAG, "DARK gesture: base=%d fast=%d", (int)s.baseline, (int)s.fast);
            s.baseline = s.fast; /* 暗电平成为新基线（仍处于遮住中） */
            enter_state(ST_REFRACT);
            return LIGHT_CTL_TOGGLE;
        }
        else if (now - s.cand_since_ms >= CAND_WINDOW_MS + 600) {
            /* 兜底：长期深暗却不满足保持（抖动）也放弃 */
            ESP_LOGI(TAG, "deep abort: fast=%d base=%d", (int)s.fast, (int)s.baseline);
            s.baseline = s.fast;
            enter_state(ST_IDLE);
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
