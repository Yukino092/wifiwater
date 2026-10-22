#ifndef _WATER_TEMP_H_
#define _WATER_TEMP_H_

// 初始化水温传感器
void water_temp_init(void);

// 获取真实的温度值 (单位: 摄氏度)
// 因为温度有小数(如 25.5度)，所以返回值为 float 浮点型
float water_temp_get_celsius(void);

#endif // _WATER_TEMP_H_