#ifndef TRACKER_ADAPTER_H
#define TRACKER_ADAPTER_H

#include "post_processor.h"

#ifdef __cplusplus
extern "C" {
#endif

void tracker_init(int frame_rate, int track_buffer);
void tracker_update(const DetectResultGroup* detections, DetectResultGroup* tracks);
void tracker_release(void);

#ifdef __cplusplus
}
#endif

#endif