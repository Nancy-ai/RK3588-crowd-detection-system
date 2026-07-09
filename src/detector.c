#include "detector.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "post_processor.h"

/*
加载训练好的rknn模型，进行推理过程
*/
// 读取模型文件到内存的辅助函数
static unsigned char* load_file_model(const char* filename, int* model_size) {
    FILE* fp = fopen(filename, "rb");
    if (fp == NULL) {
        printf("打开模型文件 %s 失败!\n", filename);
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    int size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    unsigned char* data = (unsigned char*)malloc(size);
    fread(data, 1, size, fp);
    fclose(fp);
    *model_size = size;
    return data;
}

bool detector_init(RknnContext* rknn_ctx, const char* model_path) {
    int model_size = 0;
    unsigned char* model_data = load_file_model(model_path, &model_size);
    if (model_data == NULL) return false;

    // 1. 初始化 RKNN Context
    int ret = rknn_init(&rknn_ctx->ctx, model_data, model_size, 0, NULL);
    free(model_data); // 加载完后释放内存
    if (ret < 0) {
        printf("rknn_init 失败! ret=%d\n", ret);
        return false;
    }

    // 2. 查询输入输出数量
    rknn_query(rknn_ctx->ctx, RKNN_QUERY_IN_OUT_NUM, &rknn_ctx->io_num, sizeof(rknn_ctx->io_num));
    
    // 3. 获取输入张量属性
    rknn_ctx->input_attrs = (rknn_tensor_attr*)malloc(rknn_ctx->io_num.n_input * sizeof(rknn_tensor_attr));
    memset(rknn_ctx->input_attrs, 0, rknn_ctx->io_num.n_input * sizeof(rknn_tensor_attr));
    for (int i = 0; i < rknn_ctx->io_num.n_input; i++) {
        rknn_ctx->input_attrs[i].index = i;
        rknn_query(rknn_ctx->ctx, RKNN_QUERY_INPUT_ATTR, &(rknn_ctx->input_attrs[i]), sizeof(rknn_tensor_attr));
    }

    // 4. 获取输出张量属性
    rknn_ctx->output_attrs = (rknn_tensor_attr*)malloc(rknn_ctx->io_num.n_output * sizeof(rknn_tensor_attr));
    memset(rknn_ctx->output_attrs, 0, rknn_ctx->io_num.n_output * sizeof(rknn_tensor_attr));
    for (int i = 0; i < rknn_ctx->io_num.n_output; i++) {
        rknn_ctx->output_attrs[i].index = i;
        rknn_query(rknn_ctx->ctx, RKNN_QUERY_OUTPUT_ATTR, &(rknn_ctx->output_attrs[i]), sizeof(rknn_tensor_attr));
    }

    // 记录模型要求的分辨率 (通常是 640x640x3)
    rknn_ctx->model_width   = rknn_ctx->input_attrs[0].dims[1];
    rknn_ctx->model_height  = rknn_ctx->input_attrs[0].dims[2];
    rknn_ctx->model_channel = rknn_ctx->input_attrs[0].dims[3];

    printf("NPU 模型加载成功! 期望输入尺寸: %dx%dx%d\n", 
           rknn_ctx->model_width, rknn_ctx->model_height, rknn_ctx->model_channel);
    
    
    //输出信息打印，确定模型相关信息
    printf("output num: %d\n", rknn_ctx->io_num.n_output);
    for (int i = 0; i < rknn_ctx->io_num.n_output; i++) {
        rknn_tensor_attr* attr = &rknn_ctx->output_attrs[i];

        printf("output[%d]: n_dims=%d dims=[", i, attr->n_dims);
        for (int j = 0; j < attr->n_dims; j++) {
            printf("%d%s", attr->dims[j], j == attr->n_dims - 1 ? "" : ", ");
        }
        printf("] n_elems=%d size=%d type=%s fmt=%s scale=%f zp=%d\n",
           attr->n_elems,
           attr->size,
           get_type_string(attr->type),
           get_format_string(attr->fmt),
           attr->scale,
           attr->zp);
    }
    return true;
}

bool detector_run(RknnContext* rknn_ctx, unsigned char* rgb_buffer, DetectResultGroup* out_results) {
    // 1. 设置输入 (将 RGA 处理好的 RGB 数据送入 NPU)
     // 1. 立即打印结构体指针地址和关键成员

    rknn_input inputs[1];
    memset(inputs, 0, sizeof(inputs));
    inputs[0].index = 0;
    inputs[0].type = RKNN_TENSOR_UINT8;
    inputs[0].size = rknn_ctx->model_width * rknn_ctx->model_height * rknn_ctx->model_channel;
    inputs[0].fmt = RKNN_TENSOR_NHWC;
    inputs[0].pass_through = 0;
    inputs[0].buf = rgb_buffer;

    int ret = rknn_inputs_set(rknn_ctx->ctx, rknn_ctx->io_num.n_input, inputs);
    if (ret < 0) return false;

    // 2. 运行 NPU 推理 (极其迅速，通常 20-30 毫秒)
    ret = rknn_run(rknn_ctx->ctx, NULL);
    if (ret < 0) 
    {
        printf("NPU 推理引擎直接崩了，错误码: %d\n", ret);
        return false;
    }

    // 3. 获取 NPU 输出结果
    rknn_output outputs[rknn_ctx->io_num.n_output];
    memset(outputs, 0, sizeof(outputs));
    for (int i = 0; i < rknn_ctx->io_num.n_output; i++) {
        outputs[i].index = i; 
        outputs[i].want_float = 1; // 取决于你的模型量化方式
    }
    ret = rknn_outputs_get(rknn_ctx->ctx, rknn_ctx->io_num.n_output, outputs, NULL);
    if (ret < 0) return false;


    // --- 重新打印验证 ---
    float* logits_ptr = (float*)outputs[0].buf;
    float* boxes_ptr  = (float*)outputs[1].buf;


    // printf("\n=== logits 前10个值 ===\n");
    // for (int i = 0; i < 10; i++) {
    //     printf("%f ", logits_ptr[i]);
    // }
    // printf("\n");

    // printf("\n=== boxes 前10个值 ===\n");
    // for (int i = 0; i < 10; i++) {
    //     printf("%f ", boxes_ptr[i]);
    // }
    // printf("\n");
    
    // printf("\n[DEBUG] NPU 输出了 %d 个张量 (Tensors)\n", rknn_ctx->io_num.n_output);
    // for (int i = 0; i < rknn_ctx->io_num.n_output; i++) {
    //     printf("  -> 输出 [%d]: 大小为 %d 字节\n", i, outputs[i].size);
    // }

    // ===================================================
    // 4. 调用解耦的后处理模块
    // ===================================================
    post_process(outputs, rknn_ctx, out_results);


    rknn_outputs_release(rknn_ctx->ctx, rknn_ctx->io_num.n_output, outputs);
    return true;
}

void detector_release(RknnContext* rknn_ctx) {
    if (rknn_ctx->input_attrs) free(rknn_ctx->input_attrs);
    if (rknn_ctx->output_attrs) free(rknn_ctx->output_attrs);
    if (rknn_ctx->ctx > 0) rknn_destroy(rknn_ctx->ctx);
}