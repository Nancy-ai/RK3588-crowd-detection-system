#ifndef STREAMER_H
#define STREAMER_H

#include "post_processor.h"
#include "sys_monitor.h"

#ifdef __cplusplus
extern "C" {
#endif

bool streamer_init(const char* host_ip, int port, int width, int height, int fps);

void streamer_push(unsigned char* rgb_buffer,
                   int width,
                   int height,
                   DetectResultGroup* results,
                   SystemStats* stats);

void streamer_release(void);

#ifdef __cplusplus
}
#endif

#endif