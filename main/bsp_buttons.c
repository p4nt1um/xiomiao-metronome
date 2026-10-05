/*
 * 6 按键轮询驱动 + LVGL keypad indev。
 * 复用自 xueersi-idf main/main.c（按键表 / 初始化 / read 回调 / indev 注册）。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "bsp_buttons.h"

#define BUTTON_ACTIVE_LEVEL         0
#define BUTTON_DEBOUNCE_MS          25

typedef struct {
    gpio_num_t gpio;
    uint32_t key;
    const char *name;
} board_button_t;

static const board_button_t s_buttons[] = {
    {GPIO_NUM_2, LV_KEY_UP, "UP"},
    {GPIO_NUM_13, LV_KEY_DOWN, "DOWN"},
    {GPIO_NUM_27, LV_KEY_LEFT, "LEFT"},
    {GPIO_NUM_35, LV_KEY_RIGHT, "RIGHT"},
    {GPIO_NUM_34, LV_KEY_ENTER, "A"},
    {GPIO_NUM_12, LV_KEY_ESC, "B"},
};

void bsp_buttons_init(void)
{
    uint64_t pin_mask = 0;
    uint64_t pullup_mask = 0;

    for (size_t i = 0; i < sizeof(s_buttons) / sizeof(s_buttons[0]); ++i) {
        pin_mask |= 1ULL << s_buttons[i].gpio;
        if (s_buttons[i].gpio != GPIO_NUM_34 && s_buttons[i].gpio != GPIO_NUM_35) {
            pullup_mask |= 1ULL << s_buttons[i].gpio;
        }
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = pin_mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    /* GPIO34/35 输入专用脚无内部上拉，外部已有上拉 */
    gpio_config_t pullup_conf = {
        .pin_bit_mask = pullup_mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&pullup_conf));
}

bool bsp_buttons_is_pressed(bsp_button_id_t id)
{
    if (id < 0 || id >= BSP_BTN_COUNT) {
        return false;
    }
    return gpio_get_level(s_buttons[id].gpio) == BUTTON_ACTIVE_LEVEL;
}

bool bsp_buttons_any_pressed(void)
{
    for (size_t i = 0; i < sizeof(s_buttons) / sizeof(s_buttons[0]); ++i) {
        if (gpio_get_level(s_buttons[i].gpio) == BUTTON_ACTIVE_LEVEL) {
            return true;
        }
    }
    return false;
}

static void keypad_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    static int last_raw_index = -1;
    static int stable_index = -1;
    static uint32_t raw_changed_ms = 0;
    static uint32_t last_key = LV_KEY_ENTER;
    /* 开机时已按住的键（如 A 键唤醒后未松开）不算按键：
     * 吞掉全部事件直到 6 键都松开，避免唤醒即触发启停等误操作 */
    static bool boot_guard = true;
    int raw_index = -1;
    const uint32_t now_ms = lv_tick_get();

    for (size_t i = 0; i < sizeof(s_buttons) / sizeof(s_buttons[0]); ++i) {
        if (gpio_get_level(s_buttons[i].gpio) == BUTTON_ACTIVE_LEVEL) {
            raw_index = (int)i;
            break;
        }
    }

    if (boot_guard) {
        if (raw_index >= 0) {
            data->state = LV_INDEV_STATE_RELEASED;
            data->key = last_key;
            return;
        }
        boot_guard = false;
    }

    if (raw_index != last_raw_index) {
        last_raw_index = raw_index;
        raw_changed_ms = now_ms;
        if (raw_index < 0) {
            stable_index = -1;
        }
    }
    if (lv_tick_elaps(raw_changed_ms) >= BUTTON_DEBOUNCE_MS) {
        stable_index = last_raw_index;
    }

    if (stable_index >= 0) {
        last_key = s_buttons[stable_index].key;
        data->state = LV_INDEV_STATE_PRESSED;
        data->key = last_key;
    }
    else {
        data->state = LV_INDEV_STATE_RELEASED;
        data->key = last_key;
    }
}

lv_group_t *bsp_buttons_lvgl_indev_init(lv_display_t *display)
{
    lv_group_t *group = lv_group_create();
    assert(group);
    lv_group_set_default(group);

    lv_indev_t *indev = lv_indev_create();
    assert(indev);
    lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_display(indev, display);
    lv_indev_set_group(indev, group);
    lv_indev_set_read_cb(indev, keypad_read_cb);
    lv_indev_set_long_press_time(indev, 360);
    lv_indev_set_long_press_repeat_time(indev, 130);

    return group;
}
