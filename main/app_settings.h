#pragma once

#include "metro_engine.h"

/* 练习计时的候选档位（分钟，0 = 关） */
#define APP_TIMER_CHOICES        {0, 5, 10, 15, 20, 30, 45, 60}
#define APP_TIMER_CHOICE_COUNT   8

/* 光控暂停开关（独立持久化，1 开 0 关，默认开） */
int app_settings_get_light_ctrl(void);
void app_settings_set_light_ctrl(int on);

/* 上电从 NVS 读取（首次启动用默认值：120BPM 4/4 四分音符 重音开 音量6 计时关） */
void app_settings_init(void);

void app_settings_get(metro_cfg_t *out);

/* 更新内存快照并防抖落盘（800ms 内多次修改只写一次） */
void app_settings_set(const metro_cfg_t *cfg);

/* 练习计时档位独立持久化（不属于引擎配置） */
int app_settings_get_timer_min(void);
void app_settings_set_timer_min(int minutes);
