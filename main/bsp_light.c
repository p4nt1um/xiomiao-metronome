/*
 * 光照传感器 ADC 驱动：GPIO36（ADC1_CH0）。
 * 只提供原始值，突变检测逻辑在 light_ctl。
 */

#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "esp_log.h"

#include "bsp_light.h"

static adc_oneshot_unit_handle_t s_adc;

void bsp_light_init(void)
{
    const adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &s_adc));

    const adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, ADC_CHANNEL_0, &chan_cfg));
    ESP_LOGI("bsp_light", "ADC1_CH0 (GPIO36) init done");
}

int bsp_light_read_raw(void)
{
    int val = 0;
    if (adc_oneshot_read(s_adc, ADC_CHANNEL_0, &val) != ESP_OK) {
        return -1;
    }
    return val;
}
