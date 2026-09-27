#pragma once

typedef enum {
    LIGHT_CTL_NONE = 0,
    LIGHT_CTL_TOGGLE,   /* 遮一下手势：切换 暂停/恢复 */
} light_ctl_event_t;

void light_ctl_init(void);

/*
 * 由主循环高频调用（内部按 50ms 节拍采样）。
 * enabled=false 时不采样并请求重新标定（重新启用后从新基线开始）。
 * 返回本周期检测到的手势事件（至多一个）。
 */
light_ctl_event_t light_ctl_poll(bool enabled);
