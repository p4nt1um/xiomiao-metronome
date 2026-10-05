#pragma once

#include "esp_lcd_panel_io.h"
#include "lvgl.h"

/* SPI + ST7735 上电初始化（横屏 rot90、Black Tab 序列） */
void bsp_lcd_init(void);

/* 创建 LVGL 显示：160x128、RGB565SWAP、双 20 行部分缓冲 */
lv_display_t *bsp_lcd_lvgl_display_init(void);

/* 注册 SPI 传输完成回调（异步 flush 完成通知 LVGL） */
void bsp_lcd_register_flush_ready_cb(lv_display_t *display);

/* 启动 1ms LVGL tick（esp_timer） */
void bsp_lcd_start_lvgl_tick(void);

/* 首帧已刷出后打开屏幕显示（防开机白屏） */
void bsp_lcd_display_on(void);

/* 面板停止显示（DISPOFF），进深睡前使用 */
void bsp_lcd_display_off(void);

/* 面板进入睡眠（SLPIN）；唤醒后由开机初始化序列恢复 */
void bsp_lcd_sleep_in(void);

/* 首帧 flush 是否已完成 */
bool bsp_lcd_first_flush_done(void);
