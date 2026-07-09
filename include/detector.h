#ifndef DETECTOR_H
#define DETECTOR_H

#include "rknn_api.h"
#include <stdbool.h>
#include "post_processor.h"

#ifdef __cplusplus
extern "C" {
#endif

// 定义 NPU 上下文结构体
typedef struct RknnContext{
    rknn_context ctx;
    rknn_input_output_num io_num;       //模型的输入和输出
    rknn_tensor_attr* input_attrs;      //输入张量的属性信息
    rknn_tensor_attr* output_attrs;     //模型输出信息
    int model_width;
    int model_height;
    int model_channel;
} RknnContext;

// 1. 初始化 NPU 并加载模型
bool detector_init(RknnContext* rknn_ctx, const char* model_path);

// 2. 执行推理 (传入 RGA 处理好的 640x640 RGB 数据)
bool detector_run(RknnContext* rknn_ctx, unsigned char* rgb_buffer, DetectResultGroup* out_results);

// 3. 释放 NPU 资源
void detector_release(RknnContext* rknn_ctx);

#ifdef __cplusplus
}
#endif

#endif // DETECTOR_H