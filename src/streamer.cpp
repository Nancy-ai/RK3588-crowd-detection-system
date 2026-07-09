#include "streamer.h"

#include <opencv2/opencv.hpp>
#include <stdio.h>

static cv::VideoWriter g_writer;
static int g_stream_width = 0;
static int g_stream_height = 0;

bool streamer_init(const char* host_ip, int port, int width, int height, int fps)
{
    g_stream_width = width;
    g_stream_height = height;

    char pipeline[1024];

    snprintf(pipeline, sizeof(pipeline),
             "appsrc ! "
             "videoconvert ! "
             "mpph264enc ! "
             "h264parse ! "
             "rtph264pay config-interval=1 pt=96 ! "
             "udpsink host=%s port=%d sync=false",
             host_ip, port);

    g_writer.open(
        pipeline,
        cv::CAP_GSTREAMER,
        0,
        fps,
        cv::Size(width, height),
        true
    );

    if (!g_writer.isOpened()) {
        printf("[Streamer] UDP推流初始化失败\n");
        printf("[Streamer] pipeline: %s\n", pipeline);
        return false;
    }

    printf("[Streamer] UDP推流初始化成功: %s:%d\n", host_ip, port);
    return true;
}

void streamer_push(unsigned char* rgb_buffer,
                   int width,
                   int height,
                   DetectResultGroup* results,
                   SystemStats* stats)
{
    if (!g_writer.isOpened()) {
        return;
    }

    cv::Mat rgb(height, width, CV_8UC3, rgb_buffer);
    cv::Mat bgr;
    cv::cvtColor(rgb, bgr, cv::COLOR_RGB2BGR);

    for (int i = 0; i < results->count; i++) {
        DetectResult* r = &results->results[i];

        cv::rectangle(
            bgr,
            cv::Point(r->x1, r->y1),
            cv::Point(r->x2, r->y2),
            cv::Scalar(0, 255, 0),
            2
        );

        char label[128];

        /*
           如果你的 DetectResult 里面已经加了 track_id，
           可以改成：
           snprintf(label, sizeof(label), "ID:%d %.2f", r->track_id, r->prob);
        */
        snprintf(label, sizeof(label), "person %.2f", r->prob);

        int text_y = r->y1 - 5;
        if (text_y < 15) text_y = r->y1 + 15;

        cv::putText(
            bgr,
            label,
            cv::Point(r->x1, text_y),
            cv::FONT_HERSHEY_SIMPLEX,
            0.5,
            cv::Scalar(0, 255, 0),
            1
        );
    }

    char info[128];
    snprintf(info, sizeof(info),
             "FPS: %.2f  Count: %d",
             stats ? stats->fps : 0.0f,
             results ? results->count : 0);

    cv::putText(
        bgr,
        info,
        cv::Point(20, 35),
        cv::FONT_HERSHEY_SIMPLEX,
        0.8,
        cv::Scalar(0, 255, 255),
        2
    );

    g_writer.write(bgr);
}

void streamer_release(void)
{
    if (g_writer.isOpened()) {
        g_writer.release();
    }
}