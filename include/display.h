#ifndef DISPLAY_BUS_H
#define DISPLAY_BUS_H

#include "post_processor.h" // 拿到 DetectResultGroup
#include "sys_monitor.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif



// 1. 初始化窗口
void display_init(const char* window_name);

// 2. 核心函数：把图像、AI 结果、系统状态一起刷到屏幕上
// 输入：RGB 图像缓冲区，宽，高，AI 结果，系统状态
int display_show(const char* window_name, unsigned char* rgb_data, int w, int h, 
                 DetectResultGroup* group, SystemStats* stats);

// 3. 关闭窗口
void display_exit(void);

#ifdef __cplusplus
}
#endif

#endif