#pragma once

#include "lvgl.h"
#include "metro_engine.h"

/* 创建根容器（组焦点 + 按键分发）并显示主界面 */
void ui_init(lv_group_t *group);

/* 节拍事件可视化（仅在主界面生效） */
void ui_on_beat(const metro_beat_event_t *ev);

/* 主循环高频调用：提示音序列、A 长按检测、测速超时、练习倒计时 */
void ui_poll(void);
