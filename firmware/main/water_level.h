#ifndef _WATER_LEVEL_H_
#define _WATER_LEVEL_H_

// 两个数字水位传感器（浮球/接触式开关，输出高低电平）
void water_level_init(void);

// 读水位传感器1（GPIO9）：1=高电平 0=低电平
int water_level_get_level1(void);

// 读水位传感器2（GPIO38）：1=高电平 0=低电平
int water_level_get_level2(void);

#endif // _WATER_LEVEL_H_
