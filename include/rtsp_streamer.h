#ifndef RTSP_STREAMER_H
#define RTSP_STREAMER_H

#include <stdbool.h>
#include "post_processor.h"
#include "sys_monitor.h"

#ifdef __cplusplus
extern "C" {
#endif

bool rtsp_streamer_init(int port, int width, int height, int fps);

void rtsp_draw_overlay(unsigned char* rgb_buffer,
                       int width,
                       int height,
                       DetectResultGroup* results,
                       SystemStats* stats);

void rtsp_streamer_push(unsigned char* rgb_buffer, int width, int height);

void rtsp_streamer_release(void);

#ifdef __cplusplus
}
#endif

#endif