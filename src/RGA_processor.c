#include "RGA_processor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int align_down_even(int value)
{
    return value & ~1;
}

void rga_init(RgaContext* ctx, int src_w, int src_h, int dst_w, int dst_h)
{
    memset(ctx, 0, sizeof(RgaContext));

    ctx->src_w = src_w;
    ctx->src_h = src_h;
    ctx->dst_w = dst_w;
    ctx->dst_h = dst_h;

    float scale_w = (float)dst_w / (float)src_w;
    float scale_h = (float)dst_h / (float)src_h;

    ctx->scale = scale_w < scale_h ? scale_w : scale_h;

    ctx->resized_w = align_down_even((int)((float)src_w * ctx->scale));
    ctx->resized_h = align_down_even((int)((float)src_h * ctx->scale));

    ctx->pad_x = (dst_w - ctx->resized_w) / 2;
    ctx->pad_y = (dst_h - ctx->resized_h) / 2;

    ctx->resized_rgb = (unsigned char*)malloc(ctx->resized_w * ctx->resized_h * 3);
    if (!ctx->resized_rgb) {
        fprintf(stderr, "RGA resized_rgb malloc failed\n");
    }

    printf("RGA safe letterbox: src=%dx%d dst=%dx%d resized=%dx%d pad=(%d,%d) scale=%.4f\n",
           ctx->src_w,
           ctx->src_h,
           ctx->dst_w,
           ctx->dst_h,
           ctx->resized_w,
           ctx->resized_h,
           ctx->pad_x,
           ctx->pad_y,
           ctx->scale);
}

bool rga_process(RgaContext* ctx, unsigned char* src_nv12, unsigned char* dst_rgb)
{
    if (!ctx || !src_nv12 || !dst_rgb || !ctx->resized_rgb) {
        fprintf(stderr, "RGA letterbox invalid input\n");
        return false;
    }

    rga_buffer_t src = wrapbuffer_virtualaddr(src_nv12,
                                              ctx->src_w,
                                              ctx->src_h,
                                              RK_FORMAT_YCbCr_420_SP);

    rga_buffer_t resized = wrapbuffer_virtualaddr(ctx->resized_rgb,
                                                  ctx->resized_w,
                                                  ctx->resized_h,
                                                  RK_FORMAT_RGB_888);

    im_rect src_rect = {0, 0, ctx->src_w, ctx->src_h};
    im_rect resized_rect = {0, 0, ctx->resized_w, ctx->resized_h};

    IM_STATUS status = imcheck(src, resized, src_rect, resized_rect);
    if (status != IM_STATUS_NOERROR) {
        fprintf(stderr, "RGA resize check failed: %d\n", status);
        return false;
    }

    // RGA 只写完整临时 buffer，避免写 dst_rgb 的局部区域。
    status = imresize(src, resized);
    if (status != IM_STATUS_SUCCESS) {
        fprintf(stderr, "RGA resize failed: %d\n", status);
        return false;
    }

    // YOLO letterbox 常用灰色背景：RGB = 114,114,114。
    memset(dst_rgb, 114, ctx->dst_w * ctx->dst_h * 3);

    // CPU 把 resized_rgb 贴到 dst_rgb 中间。
    for (int y = 0; y < ctx->resized_h; y++) {
        unsigned char* dst_row = dst_rgb + ((ctx->pad_y + y) * ctx->dst_w + ctx->pad_x) * 3;
        unsigned char* src_row = ctx->resized_rgb + y * ctx->resized_w * 3;
        memcpy(dst_row, src_row, ctx->resized_w * 3);
    }

    return true;
}

void rga_release(RgaContext* ctx)
{
    if (!ctx) return;

    if (ctx->resized_rgb) {
        free(ctx->resized_rgb);
        ctx->resized_rgb = NULL;
    }
}