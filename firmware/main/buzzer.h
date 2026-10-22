#ifndef _BUZZER_H_
#define _BUZZER_H_

// 初始化蜂鸣器引脚
void buzzer_init(void);

// 控制蜂鸣器状态 (1: 开启报警, 0: 关闭报警)
void buzzer_set_state(int state);

#endif // _BUZZER_H_