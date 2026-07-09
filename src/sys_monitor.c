#include "sys_monitor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/*
获取系统状态模块，包括温度，CPU占有率，GPU占有率，NPU使用情况
*/
void print_system_status(void) {
    // 1. 获取 CPU 温度 (单位：毫摄氏度)
    float cpu_temp = 0.0;
    FILE* fd_temp = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (fd_temp) {
        int temp = 0;
        if (fscanf(fd_temp, "%d", &temp) == 1) {
            cpu_temp = temp / 1000.0;
        }
        fclose(fd_temp);
    }

    // 2. 获取 CPU 占用率 (解析 /proc/stat)
    static unsigned long long last_total = 0, last_idle = 0;
    float cpu_usage = 0.0;
    FILE* fd_stat = fopen("/proc/stat", "r");
    if (fd_stat) {
        unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
        // %*s 用来跳过第一列的 "cpu" 字符串
        if (fscanf(fd_stat, "%*s %llu %llu %llu %llu %llu %llu %llu %llu", 
                   &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal) == 8) {
            
            unsigned long long total_idle = idle + iowait;
            unsigned long long total_non_idle = user + nice + system + irq + softirq + steal;
            unsigned long long total = total_idle + total_non_idle;
            
            if (last_total != 0) { // 计算两次调用之间的差值
                unsigned long long total_diff = total - last_total;
                unsigned long long idle_diff = total_idle - last_idle;
                if (total_diff > 0) {
                    cpu_usage = (float)(total_diff - idle_diff) / total_diff * 100.0;
                }
            }
            last_total = total;
            last_idle = total_idle;
        }
        fclose(fd_stat);
    }

    // 3. 获取 GPU 占用率 (注: 瑞芯微不同芯片 GPU 节点名字可能不同，如不匹配会显示 N/A)
    int gpu_load = -1;
    // 尝试 RK3588 常见节点
    FILE* fd_gpu = fopen("/sys/class/devfreq/fb000000.gpu/load", "r");
    if (!fd_gpu) {
        // 尝试 RK3568 常见节点
        fd_gpu = fopen("/sys/class/devfreq/fde60000.gpu/load", "r");
    }
    if (fd_gpu) {
        fscanf(fd_gpu, "%d@", &gpu_load); // 格式通常是 "20@800000000"，我们只取 @ 前面的数字
        fclose(fd_gpu);
    }

    // 4. 获取 NPU 占用率 (必须用 sudo 运行才能读到 debug 节点)
    char npu_load[128] = "N/A";
    FILE* fd_npu = fopen("/sys/kernel/debug/rknpu/load", "r");
    if (fd_npu) {
        if (fgets(npu_load, sizeof(npu_load), fd_npu) != NULL) {
            npu_load[strcspn(npu_load, "\n")] = 0; // 去除末尾的换行符
        }
        fclose(fd_npu);
    }

    // --- 终端彩色打印输出 ---
    // \033[1;33m 是黄色，\033[1;36m 是青色，\033[0m 是恢复默认颜色
    printf("\033[1;33m[系统监控] \033[0m");
    printf("CPU温度: \033[1;36m%.1f°C\033[0m | ", cpu_temp);
    printf("CPU占用: \033[1;36m%5.1f%%\033[0m | ", cpu_usage);
    
    if (gpu_load >= 0) {
        printf("GPU占用: \033[1;36m%3d%%\033[0m | ", gpu_load);
    } else {
        printf("GPU占用: \033[1;36mN/A\033[0m | ");
    }
    
    printf("\033[1;32m%s\033[0m\n", npu_load);
}

void get_system_status(SystemStats* stats) {
    if (!stats) return;

    // 1. 获取 CPU 温度
    stats->temp = 0.0f;
    FILE* fd_temp = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (fd_temp) {
        int temp_raw = 0;
        if (fscanf(fd_temp, "%d", &temp_raw) == 1) {
            stats->temp = temp_raw / 1000.0f;
        }
        fclose(fd_temp);
    }

    // 2. 获取 CPU 占用率 (计算两次快照之间的差值)
    static unsigned long long last_total = 0, last_idle = 0;
    stats->cpu_usage = 0.0f;
    FILE* fd_stat = fopen("/proc/stat", "r");
    if (fd_stat) {
        unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
        if (fscanf(fd_stat, "%*s %llu %llu %llu %llu %llu %llu %llu %llu", 
                   &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal) == 8) {
            
            unsigned long long total_idle = idle + iowait;
            unsigned long long total_non_idle = user + nice + system + irq + softirq + steal;
            unsigned long long total = total_idle + total_non_idle;
            
            if (last_total != 0) {
                unsigned long long total_diff = total - last_total;
                unsigned long long idle_diff = total_idle - last_idle;
                if (total_diff > 0) {
                    stats->cpu_usage = (float)(total_diff - idle_diff) / total_diff * 100.0f;
                }
            }
            last_total = total;
            last_idle = total_idle;
        }
        fclose(fd_stat);
    }

    // 3. 获取 GPU 占用率
    stats->gpu_load = 0;
    // 尝试 RK3588 路径
    FILE* fd_gpu = fopen("/sys/class/devfreq/fb000000.gpu/load", "r");
    if (!fd_gpu) {
        // 尝试 RK356x 路径
        fd_gpu = fopen("/sys/class/devfreq/fde60000.gpu/load", "r");
    }
    if (fd_gpu) {
        // 格式通常为 "25@800000000"，只读取前面的数字
        fscanf(fd_gpu, "%d", &stats->gpu_load);
        fclose(fd_gpu);
    }

    // 4. 获取 NPU 占用率 (需要 sudo 权限)
    strncpy(stats->npu_load, "N/A", sizeof(stats->npu_load));
    FILE* fd_npu = fopen("/sys/kernel/debug/rknpu/load", "r");
    if (fd_npu) {
        if (fgets(stats->npu_load, sizeof(stats->npu_load), fd_npu) != NULL) {
            // 去掉末尾换行符
            stats->npu_load[strcspn(stats->npu_load, "\n")] = 0;
        }
        fclose(fd_npu);
    }
}