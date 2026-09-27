#pragma once

/* 光照传感器原始值读取（GPIO36 = ADC1_CH0，12bit，12dB 衰减全量程） */
void bsp_light_init(void);

/* 原始值 0..4095，数值越大越亮（具体分压方向以实测为准，算法只用相对变化） */
int bsp_light_read_raw(void);
