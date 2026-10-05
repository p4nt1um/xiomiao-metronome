#pragma once

#include <stdbool.h>

#include "lvgl.h"

/* 6 键 GPIO 初始化（上2/下13/左27/右35/A34/B12，低电平按下） */
void bsp_buttons_init(void);

/* 创建 LVGL keypad indev（轮询 + 25ms 消抖）并返回默认 group */
lv_group_t *bsp_buttons_lvgl_indev_init(lv_display_t *display);

/* 查询按键原始电平（已消抖由调用方自理），供 UI 自行长按检测等 */
typedef enum {
    BSP_BTN_UP = 0,
    BSP_BTN_DOWN,
    BSP_BTN_LEFT,
    BSP_BTN_RIGHT,
    BSP_BTN_A,
    BSP_BTN_B,
    BSP_BTN_COUNT,
} bsp_button_id_t;

bool bsp_buttons_is_pressed(bsp_button_id_t id);

/* 是否有任意一键当前按下（原始电平，不消抖），供空闲判定等 */
bool bsp_buttons_any_pressed(void);
