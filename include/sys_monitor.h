#ifndef SYS_MONITOR_H
#define SYS_MONITOR_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float temp;          // CPU 温度
    float cpu_usage;     // CPU 占用率
    int gpu_load;        // GPU 占用率
    char npu_load[128];   // NPU 占用率字符串
    float fps;           // 帧率
} SystemStats;

// 打印当前系统状态 (CPU温度/占用, GPU占用, NPU占用)
void print_system_status(void);
void get_system_status(SystemStats* stats);

#ifdef __cplusplus
}
#endif

#endif // SYS_MONITOR_H