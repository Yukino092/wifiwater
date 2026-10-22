#ifndef _REFILL_PUMP_H_
#define _REFILL_PUMP_H_

// 初始化补水泵控制引脚
void refill_pump_init(void);

// 控制补水泵状态 (1: 开启补水, 0: 关闭补水)
void refill_pump_set_state(int state);

#endif // _REFILL_PUMP_H_
