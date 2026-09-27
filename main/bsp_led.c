/*
 * 板载 LED 驱动：ESP32 → I2C0（SCL=15 SDA=21）→ GD32（0x40）寄存器 0xA0/0xA1。
 * 只在状态变化时写总线；写失败（GD32 不在线）进入离线，10 秒后自动重试。
 * 禁用永久阻塞超时（节拍路径安全）。
 */

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "bsp_led.h"

#define TAG "bsp_led"

#define GD32_I2C_ADDR      0x40
#define GD32_REG_LED1      0xA0
#define GD32_REG_LED2      0xA1
#define XFER_TIMEOUT_MS    5
#define OFFLINE_RETRY_MS   10000

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static bool s_online;
static bool s_state[2];        /* 缓存：LED1/LED2 当前亮灭 */
static int64_t s_offline_us;   /* 进入离线时刻 */
static bool s_warned;          /* 离线只告警一次 */

static bool led_write(int led, bool on)
{
    const uint8_t buf[2] = {
        (uint8_t)((led == 1) ? GD32_REG_LED1 : GD32_REG_LED2),
        (uint8_t)(on ? 1 : 0),
    };
    const esp_err_t err = i2c_master_transmit(s_dev, buf, sizeof(buf), XFER_TIMEOUT_MS);
    return err == ESP_OK;
}

void bsp_led_init(void)
{
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = 0,
        .sda_io_num = GPIO_NUM_21,
        .scl_io_num = GPIO_NUM_15,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = true,
        },
    };
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c bus init failed: %s", esp_err_to_name(err));
        return;
    }

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = GD32_I2C_ADDR,
        .scl_speed_hz = 100000,
    };
    err = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c device add failed: %s", esp_err_to_name(err));
        return;
    }

    /* 探测：尝试熄灭两灯，成功即认为 GD32 在线 */
    s_online = led_write(1, false);
    s_online = led_write(2, false) || s_online;
    s_state[0] = false;
    s_state[1] = false;
    s_offline_us = 0;
    s_warned = false;
    ESP_LOGI(TAG, "GD32 0x40 LED probe: %s", s_online ? "online" : "offline");
}

void bsp_led_set(int led, bool on)
{
    if (s_dev == NULL) {
        return;
    }
    const int idx = (led == 1) ? 0 : 1;

    if (!s_online) {
        const int64_t now_us = esp_timer_get_time();
        if (s_offline_us != 0 && now_us - s_offline_us < (int64_t)OFFLINE_RETRY_MS * 1000) {
            return; /* 离线冷却中 */
        }
        s_offline_us = now_us;
    }

    if (s_online && s_state[idx] == on) {
        return; /* 状态未变，不写总线 */
    }

    if (led_write(led, on)) {
        s_state[idx] = on;
        if (!s_online) {
            s_online = true;
            ESP_LOGI(TAG, "GD32 0x40 back online");
        }
    }
    else {
        if (s_online && !s_warned) {
            ESP_LOGW(TAG, "GD32 0x40 no ack, LEDs disabled (retry every %ds)", OFFLINE_RETRY_MS / 1000);
            s_warned = true;
        }
        s_online = false;
        if (s_offline_us == 0) {
            s_offline_us = esp_timer_get_time();
        }
    }
}
