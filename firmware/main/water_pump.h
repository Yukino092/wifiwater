#ifndef _WATER_PUMP_H_
#define _WATER_PUMP_H_

// 初始化水泵控制引脚
void water_pump_init(void);

// 控制水泵状态 (1: 开启抽水, 0: 关闭抽水)
void water_pump_set_state(int state);

#endif // _WATER_PUMP_H_
