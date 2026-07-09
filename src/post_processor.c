#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "detector.h"
#include "post_processor.h"

#define MAX_CANDIDATES 2048
#define MAX_RESULTS 128
#define YOLOV8_REG_MAX 16

static float sigmoid(float x)
{
    return 1.0f / (1.0f + expf(-x));
}

static float normalize_score(float score)
{
    if (score < 0.0f || score > 1.0f) {
        return sigmoid(score);
    }
    return score;
}

static int clamp_int(int v, int min_v, int max_v)
{
    if (v < min_v) return min_v;
    if (v > max_v) return max_v;
    return v;
}

static float get_nchw_value(const float* data, int channel, int y, int x, int grid_h, int grid_w)
{
    int grid_size = grid_h * grid_w;
    int pos = y * grid_w + x;
    return data[channel * grid_size + pos];
}

static float compute_dfl_nchw(const float* box_data,
                              int side,
                              int y,
                              int x,
                              int grid_h,
                              int grid_w)
{
    float values[YOLOV8_REG_MAX];
    int base_channel = side * YOLOV8_REG_MAX;

    float max_value = -1.0e30f;
    for (int i = 0; i < YOLOV8_REG_MAX; i++) {
        values[i] = get_nchw_value(box_data, base_channel + i, y, x, grid_h, grid_w);
        if (values[i] > max_value) {
            max_value = values[i];
        }
    }

    float sum_exp = 0.0f;
    float weighted = 0.0f;

    for (int i = 0; i < YOLOV8_REG_MAX; i++) {
        float e = expf(values[i] - max_value);
        sum_exp += e;
        weighted += (float)i * e;
    }

    if (sum_exp <= 0.0f) return 0.0f;
    return weighted / sum_exp;
}

static float calculate_iou(const DetectResult* a, const DetectResult* b)
{
    float x1 = fmaxf((float)a->x1, (float)b->x1);
    float y1 = fmaxf((float)a->y1, (float)b->y1);
    float x2 = fminf((float)a->x2, (float)b->x2);
    float y2 = fminf((float)a->y2, (float)b->y2);

    float w = fmaxf(0.0f, x2 - x1);
    float h = fmaxf(0.0f, y2 - y1);
    float inter = w * h;

    float area_a = (float)(a->x2 - a->x1) * (float)(a->y2 - a->y1);
    float area_b = (float)(b->x2 - b->x1) * (float)(b->y2 - b->y1);
    float denom = area_a + area_b - inter;

    if (denom <= 0.0f) return 0.0f;
    return inter / denom;
}

static int compare_prob_desc(const void* a, const void* b)
{
    const DetectResult* ra = (const DetectResult*)a;
    const DetectResult* rb = (const DetectResult*)b;

    if (ra->prob < rb->prob) return 1;
    if (ra->prob > rb->prob) return -1;
    return 0;
}

static void add_candidate(DetectResult* candidates,
                          int* count,
                          float prob,
                          float x1,
                          float y1,
                          float x2,
                          float y2,
                          int img_w,
                          int img_h)
{
    if (*count >= MAX_CANDIDATES) return;

    int ix1 = clamp_int((int)x1, 0, img_w - 1);
    int iy1 = clamp_int((int)y1, 0, img_h - 1);
    int ix2 = clamp_int((int)x2, 0, img_w - 1);
    int iy2 = clamp_int((int)y2, 0, img_h - 1);

    if (ix2 <= ix1 || iy2 <= iy1) return;

    candidates[*count].id = PERSON_CLASS_ID;
    candidates[*count].prob = prob;
    candidates[*count].x1 = ix1;
    candidates[*count].y1 = iy1;
    candidates[*count].x2 = ix2;
    candidates[*count].y2 = iy2;

    (*count)++;
}

static void run_nms(DetectResult* candidates, int total, DetectResultGroup* group)
{
    int suppressed[MAX_CANDIDATES] = {0};

    qsort(candidates, total, sizeof(DetectResult), compare_prob_desc);

    group->count = 0;

    for (int i = 0; i < total; i++) {
        if (suppressed[i]) continue;
        if (candidates[i].prob < OBJ_THRESH) continue;

        if (group->count < MAX_RESULTS) {
            group->results[group->count++] = candidates[i];
        }

        for (int j = i + 1; j < total; j++) {
            if (suppressed[j]) continue;

            if (calculate_iou(&candidates[i], &candidates[j]) > NMS_THRESH) {
                suppressed[j] = 1;
            }
        }
    }
}

static int decode_stride_output(rknn_output* outputs,
                                int box_index,
                                int cls_index,
                                int stride,
                                int img_w,
                                int img_h,
                                DetectResult* candidates,
                                int* total_candidates)
{
    const float* box_data = (const float*)outputs[box_index].buf;
    const float* cls_data = (const float*)outputs[cls_index].buf;

    if (!box_data || !cls_data) return -1;

    int grid_w = img_w / stride;
    int grid_h = img_h / stride;

    for (int y = 0; y < grid_h; y++) {
        for (int x = 0; x < grid_w; x++) {
            float score = get_nchw_value(cls_data, PERSON_CLASS_ID, y, x, grid_h, grid_w);
            score = normalize_score(score);

            if (score < OBJ_THRESH) continue;

            float dl = compute_dfl_nchw(box_data, 0, y, x, grid_h, grid_w);
            float dt = compute_dfl_nchw(box_data, 1, y, x, grid_h, grid_w);
            float dr = compute_dfl_nchw(box_data, 2, y, x, grid_h, grid_w);
            float db = compute_dfl_nchw(box_data, 3, y, x, grid_h, grid_w);

            float cx = ((float)x + 0.5f) * (float)stride;
            float cy = ((float)y + 0.5f) * (float)stride;

            float x1 = cx - dl * (float)stride;
            float y1 = cy - dt * (float)stride;
            float x2 = cx + dr * (float)stride;
            float y2 = cy + db * (float)stride;

            add_candidate(candidates,
                          total_candidates,
                          score,
                          x1,
                          y1,
                          x2,
                          y2,
                          img_w,
                          img_h);
        }
    }

    return 0;
}

int post_process(rknn_output* outputs, RknnContext* ctx, DetectResultGroup* group)
{
    if (!outputs || !ctx || !group) return -1;

    group->count = 0;

    if (ctx->io_num.n_output < 8) {
        return -1;
    }

    int img_w = ctx->model_width > 0 ? ctx->model_width : MODEL_INPUT_SIZE;
    int img_h = ctx->model_height > 0 ? ctx->model_height : MODEL_INPUT_SIZE;

    DetectResult candidates[MAX_CANDIDATES];
    memset(candidates, 0, sizeof(candidates));

    int total_candidates = 0;

    decode_stride_output(outputs, 0, 1, 8,  img_w, img_h, candidates, &total_candidates);
    decode_stride_output(outputs, 3, 4, 16, img_w, img_h, candidates, &total_candidates);
    decode_stride_output(outputs, 6, 7, 32, img_w, img_h, candidates, &total_candidates);

    if (total_candidates <= 0) {
        return 0;
    }

    run_nms(candidates, total_candidates, group);

    return 0;
}