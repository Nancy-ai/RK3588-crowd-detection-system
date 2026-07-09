#ifndef EVENT_ANALYZER_H
#define EVENT_ANALYZER_H

#include "post_processor.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EVENT_TYPE_LONG_STAY 1
#define EVENT_MAX_RESULTS 32
#define TRACK_INFO_MAX 128

typedef struct {
    int type;
    int track_id;
    float duration;
    int x1;
    int y1;
    int x2;
    int y2;
    char roi_name[32];
} EventResult;

typedef struct {
    int count;
    EventResult results[EVENT_MAX_RESULTS];
} EventResultGroup;

typedef struct {
    int track_id;
    int in_roi;
    int alarmed;
    float dwell_time;
} TrackRuntimeInfo;

typedef struct {
    int count;
    TrackRuntimeInfo infos[TRACK_INFO_MAX];
} TrackRuntimeInfoGroup;

void event_analyzer_init(int image_w, int image_h, float long_stay_threshold_sec);

void event_analyzer_set_roi(int x1, int y1, int x2, int y2, const char* roi_name);

void event_analyzer_update(const DetectResultGroup* tracks,
                           EventResultGroup* events,
                           TrackRuntimeInfoGroup* runtime_info);

void event_analyzer_draw_overlay(unsigned char* rgb_buffer,
                                 int width,
                                 int height,
                                 const DetectResultGroup* tracks,
                                 const TrackRuntimeInfoGroup* runtime_info,
                                 const EventResultGroup* events);

void event_analyzer_release(void);

#ifdef __cplusplus
}
#endif

#endif