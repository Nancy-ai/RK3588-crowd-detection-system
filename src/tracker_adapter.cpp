#include "tracker_adapter.h"

#include "ByteTrack/BYTETracker.h"
#include "ByteTrack/Object.h"
#include "ByteTrack/Rect.h"
#include "ByteTrack/STrack.h"

#include <memory>
#include <vector>

static std::unique_ptr<byte_track::BYTETracker> g_tracker;

static float calc_iou(float ax1, float ay1, float ax2, float ay2,
                      float bx1, float by1, float bx2, float by2)
{
    float x1 = ax1 > bx1 ? ax1 : bx1;
    float y1 = ay1 > by1 ? ay1 : by1;
    float x2 = ax2 < bx2 ? ax2 : bx2;
    float y2 = ay2 < by2 ? ay2 : by2;

    float w = x2 - x1;
    float h = y2 - y1;

    if (w <= 0.0f || h <= 0.0f) {
        return 0.0f;
    }

    float inter = w * h;
    float area_a = (ax2 - ax1) * (ay2 - ay1);
    float area_b = (bx2 - bx1) * (by2 - by1);
    float denom = area_a + area_b - inter;

    if (denom <= 0.0f) {
        return 0.0f;
    }

    return inter / denom;
}

static int clamp_int(int v, int min_v, int max_v)
{
    if (v < min_v) return min_v;
    if (v > max_v) return max_v;
    return v;
}

void tracker_init(int frame_rate, int track_buffer)
{
    // 参数含义：
    // frame_rate: 视频帧率，比如 30
    // track_buffer: 允许目标丢失多少帧后删除，比如 30
    // track_thresh: 进入跟踪的分数阈值
    // high_thresh: 高置信度检测阈值
    // match_thresh: 匹配阈值
    g_tracker.reset(new byte_track::BYTETracker(
        frame_rate,
        track_buffer,
        0.35f,
        0.45f,
        0.80f
    ));
}

void tracker_update(const DetectResultGroup* detections, DetectResultGroup* tracks)
{
    if (!tracks) {
        return;
    }

    tracks->count = 0;

    if (!g_tracker || !detections) {
        return;
    }

    std::vector<byte_track::Object> objects;
    objects.reserve(detections->count);

    for (int i = 0; i < detections->count; i++) {
        const DetectResult* r = &detections->results[i];

        int w = r->x2 - r->x1;
        int h = r->y2 - r->y1;

        if (w <= 0 || h <= 0) {
            continue;
        }

        byte_track::Rect<float> rect(
            (float)r->x1,
            (float)r->y1,
            (float)w,
            (float)h
        );

        objects.emplace_back(rect, r->id, r->prob);
    }

    std::vector<byte_track::BYTETracker::STrackPtr> active_tracks = g_tracker->update(objects);

    for (const auto& track : active_tracks) {
        if (!track) {
            continue;
        }

        if (!track->isActivated()) {
            continue;
        }

        if (tracks->count >= 128) {
            break;
        }

        const byte_track::Rect<float>& rect = track->getRect();

        int x1 = clamp_int((int)rect.x(), 0, MODEL_INPUT_SIZE - 1);
        int y1 = clamp_int((int)rect.y(), 0, MODEL_INPUT_SIZE - 1);
        int x2 = clamp_int((int)(rect.x() + rect.width()), 0, MODEL_INPUT_SIZE - 1);
        int y2 = clamp_int((int)(rect.y() + rect.height()), 0, MODEL_INPUT_SIZE - 1);

        if (x2 <= x1 || y2 <= y1) {
            continue;
        }

        DetectResult* out = &tracks->results[tracks->count];

        out->id = PERSON_CLASS_ID;
        out->track_id = (int)track->getTrackId();
        out->prob = track->getScore();
        out->x1 = x1;
        out->y1 = y1;
        out->x2 = x2;
        out->y2 = y2;

        tracks->count++;
    }
}


void tracker_release(void)
{
    g_tracker.reset();
}