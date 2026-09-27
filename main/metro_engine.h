#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define METRO_BPM_MIN      10
#define METRO_BPM_MAX      250
#define METRO_BEATS_MIN    1
#define METRO_BEATS_MAX    9
#define METRO_VOL_MIN      0
#define METRO_VOL_MAX      8
#define METRO_SUBDIV_MIN   1
#define METRO_SUBDIV_MAX   4
#define METRO_MUTE_PLAY_MIN 1
#define METRO_MUTE_PLAY_MAX 8
#define METRO_MUTE_REST_MIN 0
#define METRO_MUTE_REST_MAX 8

typedef struct {
    int bpm;            /* 10..250 */
    int beats_per_bar;  /* 1..9 */
    int subdiv;         /* 每拍细分：1 四分 / 2 八分 / 3 三连音 / 4 十六分 */
    bool accent;        /* 第一拍重音 */
    int volume;         /* 0..8，0 = 静音 */
    int mute_play;      /* 静音训练：播 N 小节 1..8 */
    int mute_rest;      /* 静音训练：静 M 小节 0..8（0 = 关闭） */
} metro_cfg_t;

typedef struct {
    uint8_t beat;       /* 0..beats_per_bar-1（细分拍不产生事件） */
    bool accented;
    bool muted;         /* 当前小节处于静音训练的静音段 */
    uint64_t ts_us;     /* esp_timer_get_time() 拍点时间戳（精度验证用） */
} metro_beat_event_t;

/* 将配置夹到合法范围 */
void metro_engine_clamp_cfg(metro_cfg_t *cfg);

/* 初始化引擎（app_main 中、buzzer 初始化之后调用一次） */
void metro_engine_init(const metro_cfg_t *cfg);

/* 原子更新配置；运行中改 BPM/细分会平滑重启定时器 */
void metro_engine_set_cfg(const metro_cfg_t *cfg);

/* 从第 1 拍起步（立即响第一拍） */
void metro_engine_start(void);
void metro_engine_stop(void);

/* 三态：STOPPED / RUNNING / PAUSED。
 * 暂停 = 立即静音、拍点冻结（静音小节循环位置保留）；
 * 恢复 = 从第 1 拍强拍起步，静音循环继续。 */
typedef enum {
    METRO_STATE_STOPPED = 0,
    METRO_STATE_RUNNING,
    METRO_STATE_PAUSED,
} metro_state_t;

metro_state_t metro_engine_state(void);
void metro_engine_pause(void);
void metro_engine_resume(void);

bool metro_engine_running(void);

/* UI 任务从此队列取拍点事件（ metro_beat_event_t ） */
QueueHandle_t metro_engine_event_queue(void);
