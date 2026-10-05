idf_component_register(
    SRCS "main.c"
         "bsp_lcd.c"
         "bsp_buttons.c"
         "bsp_buzzer.c"
         "bsp_led.c"
         "bsp_light.c"
         "light_ctl.c"
         "power_ctl.c"
         "metro_engine.c"
         "app_settings.c"
         "ui.c"
         "fonts/ui_font_cn12.c"
         "fonts/ui_font_cn16.c"
    INCLUDE_DIRS "." "fonts"
    PRIV_REQUIRES esp_lcd esp_timer esp_driver_gpio esp_driver_spi esp_driver_ledc esp_driver_i2c esp_hw_support esp_adc lvgl nvs_flash
)
