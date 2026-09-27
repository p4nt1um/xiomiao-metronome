#pragma once

#include <stdbool.h>

/*
 * 板载 LED1/LED2（经 GD32 协处理器 I2C 从机 0x40 控制）：
 * LED1 = 寄存器 0xA0，LED2 = 寄存器 0xA1，值 0=灭 1=亮。
 * 内部缓存状态，仅变化时发 I2C 写；GD32 无响应时离线并定期重试。
 */
void bsp_led_init(void);

/* led: 1 或 2；仅 LVGL 任务调用 */
void bsp_led_set(int led, bool on);
