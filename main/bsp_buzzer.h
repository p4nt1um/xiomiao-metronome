#pragma once

#include <stdint.h>

/* LEDC 初始化：GPIO14 无源蜂鸣器，8bit 分辨率，初始静音 */
void bsp_buzzer_init(void);

/*
 * 发一声 tone：设定频率/占空比，duration_ms 后自动停声。
 * 可在 esp_timer 回调上下文中调用（内部用单次 esp_timer 停声）。
 * duty: 0~255（8bit 满量程），音量映射由此控制。
 */
void bsp_buzzer_tone(uint32_t freq_hz, uint8_t duty, uint32_t duration_ms);

/* 立即停声 */
void bsp_buzzer_stop(void);
