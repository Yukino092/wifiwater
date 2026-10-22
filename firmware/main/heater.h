#ifndef _HEATER_H_
#define _HEATER_H_

// 初始化加热棒控制引脚
void heater_init(void);

// 控制加热棒状态 (1: 开启加热, 0: 关闭加热)
void heater_set_state(int state);

#endif // _HEATER_H_