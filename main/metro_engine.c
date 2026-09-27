/*
 * 节拍引擎：esp_timer 周期定时驱动。
 *
 * 精度设计：esp_timer 周期任务按绝对时间对齐，无累积漂移；
 * 改 BPM/细分用 esp_timer_restart 平滑切换周期。
 * 定时周期 = 60s / (BPM × 细分)，细分拍只发弱短音不发 UI 事件。
 * 静音训练：按小节计数循环「播 N 小节 → 静 M 小节」，
 * 静音段不发声音，UI 事件带 muted 标记供拍点变暗。
 * 回调在 esp_timer 任务上下文执行，只做 LEDC 寄存器操作和入队。
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "bsp_buzzer.h"
#include "metro_engine.h"

#define TAG "metro_eng"

#define EVT_QUEUE_LEN       8

/* 声音方案（M0 实机确认，后续可再调） */
#define TONE_ACCENT_HZ      1568
#define TONE_WEAK_HZ        880
#define TONE_SUB_HZ         660
#define TONE_ACCENT_MS      50
#define TONE_WEAK_MS        40
#define TONE_SUB_MS         25

static metro_cfg_t s_cfg;
static portMUX_TYPE s_cfg_mux = portMUX_INITIALIZER_UNLOCKED;
static QueueHandle_t s_evt_q;
static esp_timer_handle_t s_beat_timer;
static volatile bool s_running;
static volatile bool s_paused;
static int s_tick;    /* 小节内位置 0..beats*subdiv-1 */
static int s_bar;     /* 启动以来小节计数 */

static uint64_t period_us(const metro_cfg_t *cfg)
{
    return 60000000ULL / ((uint64_t)cfg->bpm * (uint64_t)cfg->subdiv);
}

static uint8_t volume_duty(int volume)
{
    if (volume <= METRO_VOL_MIN) {
        return 0;
    }
    if (volume >= METRO_VOL_MAX) {
        return 255;
    }
    return (uint8_t)((volume * 255) / METRO_VOL_MAX);
}

static void cfg_copy(metro_cfg_t *out)
{
    portENTER_CRITICAL(&s_cfg_mux);
    *out = s_cfg;
    portEXIT_CRITICAL(&s_cfg_mux);
}

void metro_engine_clamp_cfg(metro_cfg_t *cfg)
{
    if (cfg->bpm < METRO_BPM_MIN) {
        cfg->bpm = METRO_BPM_MIN;
    }
    if (cfg->bpm > METRO_BPM_MAX) {
        cfg->bpm = METRO_BPM_MAX;
    }
    if (cfg->beats_per_bar < METRO_BEATS_MIN) {
        cfg->beats_per_bar = METRO_BEATS_MIN;
    }
    if (cfg->beats_per_bar > METRO_BEATS_MAX) {
        cfg->beats_per_bar = METRO_BEATS_MAX;
    }
    if (cfg->subdiv < METRO_SUBDIV_MIN) {
        cfg->subdiv = METRO_SUBDIV_MIN;
    }
    if (cfg->subdiv > METRO_SUBDIV_MAX) {
        cfg->subdiv = METRO_SUBDIV_MAX;
    }
    if (cfg->volume < METRO_VOL_MIN) {
        cfg->volume = METRO_VOL_MIN;
    }
    if (cfg->volume > METRO_VOL_MAX) {
        cfg->volume = METRO_VOL_MAX;
    }
    if (cfg->mute_play < METRO_MUTE_PLAY_MIN) {
        cfg->mute_play = METRO_MUTE_PLAY_MIN;
    }
    if (cfg->mute_play > METRO_MUTE_PLAY_MAX) {
        cfg->mute_play = METRO_MUTE_PLAY_MAX;
    }
    if (cfg->mute_rest < METRO_MUTE_REST_MIN) {
        cfg->mute_rest = METRO_MUTE_REST_MIN;
    }
    if (cfg->mute_rest > METRO_MUTE_REST_MAX) {
        cfg->mute_rest = METRO_MUTE_REST_MAX;
    }
}

static bool bar_is_muted(const metro_cfg_t *cfg, int bar)
{
    if (cfg->mute_rest <= 0) {
        return false;
    }
    const int cycle = cfg->mute_play + cfg->mute_rest;
    return (bar % cycle) >= cfg->mute_play;
}

static void beat_emit_main(const metro_cfg_t *cfg, uint8_t beat, bool muted)
{
    const bool accented = cfg->accent && beat == 0;
    const uint8_t duty = volume_duty(cfg->volume);
    const uint64_t now_us = esp_timer_get_time();

    if (!muted && duty > 0) {
        if (accented) {
            bsp_buzzer_tone(TONE_ACCENT_HZ, duty, TONE_ACCENT_MS);
        }
        else {
            bsp_buzzer_tone(TONE_WEAK_HZ, duty, TONE_WEAK_MS);
        }
    }

    const metro_beat_event_t ev = {
        .beat = beat,
        .accented = accented,
        .muted = muted,
        .ts_us = now_us,
    };
    xQueueSend(s_evt_q, &ev, 0);
    ESP_LOGI(TAG, "beat=%u acc=%d mute=%d ts=%llu", beat, accented, muted,
             (unsigned long long)now_us);
}

static void beat_timer_cb(void *arg)
{
    (void)arg;
    if (!s_running) {
        return;
    }

    metro_cfg_t cfg;
    cfg_copy(&cfg);
    const uint8_t duty = volume_duty(cfg.volume);
    const int tick = s_tick;

    if (tick % cfg.subdiv == 0) { /* 主拍 */
        beat_emit_main(&cfg, (uint8_t)(tick / cfg.subdiv), bar_is_muted(&cfg, s_bar));
    }
    else if (duty > 0 && !bar_is_muted(&cfg, s_bar)) { /* 细分拍：只发弱短音 */
        bsp_buzzer_tone(TONE_SUB_HZ, (uint8_t)((duty * 3) / 4), TONE_SUB_MS);
    }

    s_tick = tick + 1;
    if (s_tick >= cfg.beats_per_bar * cfg.subdiv) {
        s_tick = 0;
        s_bar++;
    }
}

void metro_engine_init(const metro_cfg_t *cfg)
{
    s_cfg = *cfg;
    metro_engine_clamp_cfg(&s_cfg);
    s_running = false;
    s_paused = false;
    s_tick = 0;
    s_bar = 0;

    s_evt_q = xQueueCreate(EVT_QUEUE_LEN, sizeof(metro_beat_event_t));
    assert(s_evt_q);

    const esp_timer_create_args_t timer_args = {
        .callback = beat_timer_cb,
        .name = "beat",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_beat_timer));

    ESP_LOGI(TAG, "engine init: bpm=%d beats=%d sub=%d accent=%d vol=%d mute=%d+%d",
             s_cfg.bpm, s_cfg.beats_per_bar, s_cfg.subdiv, s_cfg.accent, s_cfg.volume,
             s_cfg.mute_play, s_cfg.mute_rest);
}

/* 从第 1 拍启动定时（start 与 resume 共用；start 前已重置 s_bar，resume 保留） */
static void run_from_beat0(const metro_cfg_t *cfg)
{
    s_tick = 0;
    s_running = true;
    beat_emit_main(cfg, 0, bar_is_muted(cfg, s_bar));
    s_tick = 1;
    if (s_tick >= cfg->beats_per_bar * cfg->subdiv) {
        s_tick = 0;
        s_bar = 1;
    }
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_beat_timer, period_us(cfg)));
}

void metro_engine_start(void)
{
    if (s_running || s_paused) {
        return;
    }

    metro_cfg_t cfg;
    cfg_copy(&cfg);
    s_bar = 0;
    run_from_beat0(&cfg);
    ESP_LOGI(TAG, "start @%dbpm sub=%d period=%lluus", cfg.bpm, cfg.subdiv,
             (unsigned long long)period_us(&cfg));
}

void metro_engine_pause(void)
{
    if (!s_running) {
        return;
    }

    s_running = false;
    s_paused = true;
    esp_timer_stop(s_beat_timer);
    bsp_buzzer_stop();
    ESP_LOGI(TAG, "pause (tick=%d bar=%d)", s_tick, s_bar);
}

void metro_engine_resume(void)
{
    if (!s_paused) {
        return;
    }

    s_paused = false;
    metro_cfg_t cfg;
    cfg_copy(&cfg);
    run_from_beat0(&cfg);
    ESP_LOGI(TAG, "resume @%dbpm bar=%d", cfg.bpm, s_bar);
}

metro_state_t metro_engine_state(void)
{
    if (s_running) {
        return METRO_STATE_RUNNING;
    }
    return s_paused ? METRO_STATE_PAUSED : METRO_STATE_STOPPED;
}

void metro_engine_stop(void)
{
    if (!s_running && !s_paused) {
        return;
    }

    s_running = false;
    s_paused = false;
    esp_timer_stop(s_beat_timer);
    bsp_buzzer_stop();
    ESP_LOGI(TAG, "stop");
}

bool metro_engine_running(void)
{
    return s_running;
}

QueueHandle_t metro_engine_event_queue(void)
{
    return s_evt_q;
}

void metro_engine_set_cfg(const metro_cfg_t *cfg)
{
    metro_cfg_t c = *cfg;
    metro_engine_clamp_cfg(&c);

    uint64_t old_period;
    bool mute_changed;
    portENTER_CRITICAL(&s_cfg_mux);
    old_period = period_us(&s_cfg);
    mute_changed = (s_cfg.mute_play != c.mute_play) || (s_cfg.mute_rest != c.mute_rest);
    s_cfg = c;
    portEXIT_CRITICAL(&s_cfg_mux);

    if (s_tick >= c.beats_per_bar * c.subdiv) {
        s_tick = 0;
    }
    if (mute_changed) {
        s_bar = 0; /* 静音参数变化后从播段重新开始 */
    }

    if (s_running) {
        const uint64_t new_period = period_us(&c);
        if (new_period != old_period) {
            esp_timer_restart(s_beat_timer, new_period);
        }
    }
    ESP_LOGI(TAG, "cfg: bpm=%d beats=%d sub=%d accent=%d vol=%d mute=%d+%d",
             c.bpm, c.beats_per_bar, c.subdiv, c.accent, c.volume, c.mute_play, c.mute_rest);
}
