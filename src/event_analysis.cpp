#include "event_analysis.h"

#include <opencv2/opencv.hpp>
#include <chrono>
#include <string.h>
#include <stdio.h>

#define MAX_TRACK_STATES 256

typedef struct {
    int used;
    int track_id;
    int in_roi;
    int alarmed;
    double enter_time;
    double last_seen_time;
    float dwell_time;
} TrackState;

static TrackState g_states[MAX_TRACK_STATES];

static int g_img_w = 640;
static int g_img_h = 640;

static int g_roi_x1 = 80;
static int g_roi_y1 = 80;
static int g_roi_x2 = 560;
static int g_roi_y2 = 560;
static char g_roi_name[32] = "watch_area";

static float g_long_stay_threshold = 30.0f;
static float g_track_timeout_sec = 3.0f;

static double now_sec()
{
    using clock = std::chrono::steady_clock;
    static const auto start_time = clock::now();
    auto now = clock::now();
    return std::chrono::duration<double>(now - start_time).count();
}

static int clamp_int(int v, int low, int high)
{
    if (v < low) return low;
    if (v > high) return high;
    return v;
}

static int point_in_roi(int x, int y)
{
    return x >= g_roi_x1 && x <= g_roi_x2 && y >= g_roi_y1 && y <= g_roi_y2;
}

static TrackState* find_state(int track_id)
{
    for (int i = 0; i < MAX_TRACK_STATES; i++) {
        if (g_states[i].used && g_states[i].track_id == track_id) {
            return &g_states[i];
        }
    }
    return nullptr;
}

static TrackState* get_or_create_state(int track_id)
{
    TrackState* state = find_state(track_id);
    if (state) {
        return state;
    }

    for (int i = 0; i < MAX_TRACK_STATES; i++) {
        if (!g_states[i].used) {
            memset(&g_states[i], 0, sizeof(TrackState));
            g_states[i].used = 1;
            g_states[i].track_id = track_id;
            return &g_states[i];
        }
    }

    return nullptr;
}

static void cleanup_lost_states(double now)
{
    for (int i = 0; i < MAX_TRACK_STATES; i++) {
        if (!g_states[i].used) {
            continue;
        }

        if (now - g_states[i].last_seen_time > g_track_timeout_sec) {
            memset(&g_states[i], 0, sizeof(TrackState));
        }
    }
}

static void add_event(EventResultGroup* events,
                      int track_id,
                      float duration,
                      const DetectResult* track)
{
    if (!events || !track) {
        return;
    }

    if (events->count >= EVENT_MAX_RESULTS) {
        return;
    }

    EventResult* event = &events->results[events->count];

    event->type = EVENT_TYPE_LONG_STAY;
    event->track_id = track_id;
    event->duration = duration;
    event->x1 = track->x1;
    event->y1 = track->y1;
    event->x2 = track->x2;
    event->y2 = track->y2;

    strncpy(event->roi_name, g_roi_name, sizeof(event->roi_name) - 1);
    event->roi_name[sizeof(event->roi_name) - 1] = '\0';

    events->count++;
}

static void add_runtime_info(TrackRuntimeInfoGroup* runtime_info,
                             const TrackState* state)
{
    if (!runtime_info || !state) {
        return;
    }

    if (runtime_info->count >= TRACK_INFO_MAX) {
        return;
    }

    TrackRuntimeInfo* info = &runtime_info->infos[runtime_info->count];

    info->track_id = state->track_id;
    info->in_roi = state->in_roi;
    info->alarmed = state->alarmed;
    info->dwell_time = state->dwell_time;

    runtime_info->count++;
}

static const TrackRuntimeInfo* find_runtime_info(const TrackRuntimeInfoGroup* runtime_info,
                                                 int track_id)
{
    if (!runtime_info) {
        return nullptr;
    }

    for (int i = 0; i < runtime_info->count; i++) {
        if (runtime_info->infos[i].track_id == track_id) {
            return &runtime_info->infos[i];
        }
    }

    return nullptr;
}

void event_analyzer_init(int image_w, int image_h, float long_stay_threshold_sec)
{
    memset(g_states, 0, sizeof(g_states));

    g_img_w = image_w;
    g_img_h = image_h;

    if (long_stay_threshold_sec > 0.0f) {
        g_long_stay_threshold = long_stay_threshold_sec;
    }

    g_roi_x1 = 0;
    g_roi_y1 = 0;
    g_roi_x2 = image_w - 1;
    g_roi_y2 = image_h - 1;

    strncpy(g_roi_name, "watch_area", sizeof(g_roi_name) - 1);
    g_roi_name[sizeof(g_roi_name) - 1] = '\0';
}

void event_analyzer_set_roi(int x1, int y1, int x2, int y2, const char* roi_name)
{
    g_roi_x1 = clamp_int(x1, 0, g_img_w - 1);
    g_roi_y1 = clamp_int(y1, 0, g_img_h - 1);
    g_roi_x2 = clamp_int(x2, 0, g_img_w - 1);
    g_roi_y2 = clamp_int(y2, 0, g_img_h - 1);

    if (g_roi_x2 < g_roi_x1) {
        int tmp = g_roi_x1;
        g_roi_x1 = g_roi_x2;
        g_roi_x2 = tmp;
    }

    if (g_roi_y2 < g_roi_y1) {
        int tmp = g_roi_y1;
        g_roi_y1 = g_roi_y2;
        g_roi_y2 = tmp;
    }

    if (roi_name) {
        strncpy(g_roi_name, roi_name, sizeof(g_roi_name) - 1);
        g_roi_name[sizeof(g_roi_name) - 1] = '\0';
    }
}

void event_analyzer_update(const DetectResultGroup* tracks,
                           EventResultGroup* events,
                           TrackRuntimeInfoGroup* runtime_info)
{
    if (events) {
        events->count = 0;
    }

    if (runtime_info) {
        runtime_info->count = 0;
    }

    if (!tracks) {
        return;
    }

    double now = now_sec();
    cleanup_lost_states(now);

    for (int i = 0; i < tracks->count; i++) {
        const DetectResult* track = &tracks->results[i];

        if (track->track_id < 0) {
            continue;
        }

        TrackState* state = get_or_create_state(track->track_id);
        if (!state) {
            continue;
        }

        int foot_x = (track->x1 + track->x2) / 2;
        int foot_y = track->y2;

        int inside = point_in_roi(foot_x, foot_y);

        state->last_seen_time = now;

        if (inside) {
            if (!state->in_roi) {
                state->in_roi = 1;
                state->alarmed = 0;
                state->enter_time = now;
                state->dwell_time = 0.0f;
            } else {
                state->dwell_time = (float)(now - state->enter_time);
            }

            if (state->dwell_time >= g_long_stay_threshold && !state->alarmed) {
                add_event(events, state->track_id, state->dwell_time, track);
                state->alarmed = 1;
            }
        } else {
            state->in_roi = 0;
            state->alarmed = 0;
            state->enter_time = 0.0;
            state->dwell_time = 0.0f;
        }

        add_runtime_info(runtime_info, state);
    }
}

void event_analyzer_draw_overlay(unsigned char* rgb_buffer,
                                 int width,
                                 int height,
                                 const DetectResultGroup* tracks,
                                 const TrackRuntimeInfoGroup* runtime_info,
                                 const EventResultGroup* events)
{
    if (!rgb_buffer || width <= 0 || height <= 0) {
        return;
    }

    cv::Mat img(height, width, CV_8UC3, rgb_buffer);

    cv::rectangle(img,
                  cv::Point(g_roi_x1, g_roi_y1),
                  cv::Point(g_roi_x2, g_roi_y2),
                  cv::Scalar(255, 255, 0),
                  2);

    char roi_label[128];
    snprintf(roi_label, sizeof(roi_label), "ROI:%s stay>%.0fs",
             g_roi_name, g_long_stay_threshold);

    cv::putText(img,
                roi_label,
                cv::Point(g_roi_x1 + 5, g_roi_y1 + 20),
                cv::FONT_HERSHEY_SIMPLEX,
                0.55,
                cv::Scalar(255, 255, 0),
                2);

    if (tracks && runtime_info) {
        for (int i = 0; i < tracks->count; i++) {
            const DetectResult* track = &tracks->results[i];
            const TrackRuntimeInfo* info = find_runtime_info(runtime_info, track->track_id);

            if (!info || !info->in_roi) {
                continue;
            }

            cv::Scalar color = info->alarmed ? cv::Scalar(255, 0, 0) : cv::Scalar(0, 255, 0);

            cv::rectangle(img,
                          cv::Point(track->x1, track->y1),
                          cv::Point(track->x2, track->y2),
                          color,
                          2);

            char label[128];

            if (info->alarmed) {
                snprintf(label, sizeof(label), "ID:%d LONG STAY %.1fs",
                         track->track_id, info->dwell_time);
            } else {
                snprintf(label, sizeof(label), "ID:%d stay %.1fs",
                         track->track_id, info->dwell_time);
            }

            int label_y = track->y2 + 18;
            if (label_y > height - 5) {
                label_y = track->y1 - 8;
            }

            cv::putText(img,
                        label,
                        cv::Point(track->x1, label_y),
                        cv::FONT_HERSHEY_SIMPLEX,
                        0.5,
                        color,
                        2);
        }
    }

    if (events && events->count > 0) {
        cv::rectangle(img,
                      cv::Rect(0, height - 45, width, 45),
                      cv::Scalar(255, 0, 0),
                      -1);

        char alert[256];
        snprintf(alert, sizeof(alert), "ALERT: long stay detected, events=%d",
                 events->count);

        cv::putText(img,
                    alert,
                    cv::Point(10, height - 15),
                    cv::FONT_HERSHEY_SIMPLEX,
                    0.75,
                    cv::Scalar(255, 255, 255),
                    2);
    }
}

void event_analyzer_release(void)
{
    memset(g_states, 0, sizeof(g_states));
}