#pragma once

#include <stdbool.h>

/*
 * 超时自动关机（省电）：引擎非运行且长时间无按键 → 深度睡眠；
 * 仅 A 键（GPIO34，RTC 脚，低电平）可唤醒，唤醒 = 重新开机回主界面停止态。
 */

/* 空闲时间戳归零起点（须在 lv_init 之后调用一次） */
void power_ctl_init(void);

/* 任意按键活动时刷新空闲计时 */
void power_ctl_reset(void);

/*
 * 由 ui_poll() 每周期调用（LVGL 任务上下文）。
 * engine_running 为真或当前有键按住 → 视为活动；
 * 否则空闲超过 POWER_CTL_TIMEOUT_MS 且设置开关开 → 进入深睡（不返回）。
 */
void power_ctl_poll(bool engine_running);

/* 关机序列：停引擎/蜂鸣/LED → 落盘设置 → 告别音 → 屏幕关闭 → 深睡（不返回） */
void power_ctl_sleep_now(void);

/* app_main 顶部调用：打日志说明本次开机的唤醒来源（深睡唤醒/普通上电） */
void power_ctl_boot_wake_check(void);
