#ifndef POST_PROCESSOR_H
#define POST_PROCESSOR_H

#include "rknn_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OBJ_THRESH 0.40f
#define NMS_THRESH 0.55f
#define MODEL_INPUT_SIZE 640
#define PERSON_CLASS_ID 0

struct RknnContext;

typedef struct {
    int id;
    int track_id;
    float prob;
    int x1,y1,x2,y2;
} DetectResult;

typedef struct {
    int count;
    DetectResult results[128];
} DetectResultGroup;

int post_process(rknn_output* outputs, struct RknnContext* ctx, DetectResultGroup* out_results);

#ifdef __cplusplus
}
#endif

#endif