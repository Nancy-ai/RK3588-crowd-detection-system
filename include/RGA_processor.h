#ifndef RGA_PROCESSOR_H
#define RGA_PROCESSOR_H

#include <stdbool.h>
#include "im2d.h"
#include "rga.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int src_w;
    int src_h;
    int dst_w;
    int dst_h;

    float scale;
    int resized_w;
    int resized_h;
    int pad_x;
    int pad_y;

    unsigned char* resized_rgb;
} RgaContext;

void rga_init(RgaContext* ctx, int src_w, int src_h, int dst_w, int dst_h);
bool rga_process(RgaContext* ctx, unsigned char* src_nv12, unsigned char* dst_rgb);
void rga_release(RgaContext* ctx);

#ifdef __cplusplus
}
#endif

#endif