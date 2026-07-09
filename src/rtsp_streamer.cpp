#include "rtsp_streamer.h"

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/rtsp-server/rtsp-server.h>

#include <opencv2/opencv.hpp>

#include <stdio.h>
#include <string.h>
#include <thread>
#include <mutex>
#include <atomic>

static GstRTSPServer* g_server = nullptr;
static GstRTSPMediaFactory* g_factory = nullptr;
static GMainLoop* g_loop = nullptr;
static guint g_server_id = 0;

static GstElement* g_appsrc = nullptr;
static std::mutex g_appsrc_mutex;

static std::thread g_rtsp_thread;
static std::atomic<bool> g_running(false);
static std::atomic<guint64> g_frame_id(0);

static int g_width = 640;
static int g_height = 640;
static int g_fps = 30;

static int clamp_int(int v, int low, int high)
{
    if (v < low) return low;
    if (v > high) return high;
    return v;
}

static void drop_appsrc_ref()
{
    std::lock_guard<std::mutex> lock(g_appsrc_mutex);
    if (g_appsrc) {
        gst_object_unref(g_appsrc);
        g_appsrc = nullptr;
    }
}

static void media_configure_cb(GstRTSPMediaFactory* factory,
                               GstRTSPMedia* media,
                               gpointer user_data)
{
    (void)factory;
    (void)user_data;

    GstElement* element = gst_rtsp_media_get_element(media);
    if (!element) {
        printf("[RTSP] get media element failed\n");
        return;
    }

    GstElement* appsrc = gst_bin_get_by_name_recurse_up(GST_BIN(element), "mysrc");
    if (!appsrc) {
        printf("[RTSP] get appsrc failed\n");
        gst_object_unref(element);
        return;
    }

    g_object_set(G_OBJECT(appsrc),
                 "is-live", TRUE,
                 "block", FALSE,
                 "format", GST_FORMAT_TIME,
                 nullptr);

    gst_app_src_set_stream_type(GST_APP_SRC(appsrc), GST_APP_STREAM_TYPE_STREAM);

    {
        std::lock_guard<std::mutex> lock(g_appsrc_mutex);

        if (g_appsrc) {
            gst_object_unref(g_appsrc);
            g_appsrc = nullptr;
        }

        g_appsrc = GST_ELEMENT(gst_object_ref(appsrc));
        g_frame_id = 0;
    }

    gst_object_unref(appsrc);
    gst_object_unref(element);

    printf("[RTSP] client connected, appsrc ready\n");
}

static void rtsp_loop_thread()
{
    if (g_loop) {
        g_main_loop_run(g_loop);
    }
}

bool rtsp_streamer_init(int port, int width, int height, int fps)
{
    if (g_running.load()) {
        return true;
    }

    g_width = width;
    g_height = height;
    g_fps = fps > 0 ? fps : 30;

    gst_init(nullptr, nullptr);

    g_loop = g_main_loop_new(nullptr, FALSE);
    if (!g_loop) {
        printf("[RTSP] create main loop failed\n");
        return false;
    }

    g_server = gst_rtsp_server_new();
    if (!g_server) {
        printf("[RTSP] create server failed\n");
        return false;
    }

    char service[16];
    snprintf(service, sizeof(service), "%d", port);
    gst_rtsp_server_set_service(g_server, service);

    GstRTSPMountPoints* mounts = gst_rtsp_server_get_mount_points(g_server);
    if (!mounts) {
        printf("[RTSP] get mount points failed\n");
        return false;
    }

    g_factory = gst_rtsp_media_factory_new();
    if (!g_factory) {
        printf("[RTSP] create media factory failed\n");
        gst_object_unref(mounts);
        return false;
    }

    char pipeline[1024];

    snprintf(pipeline, sizeof(pipeline),
             "( appsrc name=mysrc is-live=true block=false format=time "
             "caps=video/x-raw,format=RGB,width=%d,height=%d,framerate=%d/1 "
             "! queue leaky=downstream max-size-buffers=2 "
             "! videoconvert "
             "! mpph264enc "
             "! h264parse "
             "! rtph264pay name=pay0 pt=96 config-interval=1 )",
             g_width, g_height, g_fps);

    gst_rtsp_media_factory_set_launch(g_factory, pipeline);
    gst_rtsp_media_factory_set_shared(g_factory, TRUE);

    g_signal_connect(g_factory, "media-configure", G_CALLBACK(media_configure_cb), nullptr);

    gst_rtsp_mount_points_add_factory(mounts, "/live", g_factory);
    gst_object_unref(mounts);

    g_server_id = gst_rtsp_server_attach(g_server, nullptr);
    if (g_server_id == 0) {
        printf("[RTSP] server attach failed\n");
        return false;
    }

    g_running = true;
    g_rtsp_thread = std::thread(rtsp_loop_thread);

    printf("[RTSP] server started\n");
    printf("[RTSP] url: rtsp://<board_ip>:%d/live\n", port);
    printf("[RTSP] pipeline: %s\n", pipeline);

    return true;
}

void rtsp_draw_overlay(unsigned char* rgb_buffer,
                       int width,
                       int height,
                       DetectResultGroup* results,
                       SystemStats* stats)
{
    if (!rgb_buffer || width <= 0 || height <= 0) {
        return;
    }

    cv::Mat img(height, width, CV_8UC3, rgb_buffer);

    if (results) {
        for (int i = 0; i < results->count; i++) {
            DetectResult* res = &results->results[i];

            int x1 = clamp_int(res->x1, 0, width - 1);
            int y1 = clamp_int(res->y1, 0, height - 1);
            int x2 = clamp_int(res->x2, 0, width - 1);
            int y2 = clamp_int(res->y2, 0, height - 1);

            if (x2 <= x1 || y2 <= y1) {
                continue;
            }

            cv::rectangle(img,
                          cv::Point(x1, y1),
                          cv::Point(x2, y2),
                          cv::Scalar(0, 255, 0),
                          2);

            char label[64];
            // snprintf(label, sizeof(label), "person %.2f", res->prob);
            if (res->track_id >= 0) {
                snprintf(label, sizeof(label), "ID:%d %.2f", res->track_id, res->prob);
            } 
            else {
                snprintf(label, sizeof(label), "person %.2f", res->prob);
            }

            int label_y = y1 - 6;
            if (label_y < 15) {
                label_y = y1 + 18;
            }

            cv::putText(img,
                        label,
                        cv::Point(x1, label_y),
                        cv::FONT_HERSHEY_SIMPLEX,
                        0.5,
                        cv::Scalar(255, 255, 0),
                        1);
        }
    }

    // if (stats) {
    //     cv::rectangle(img,
    //                   cv::Rect(0, 0, 330, 115),
    //                   cv::Scalar(0, 0, 0),
    //                   -1);

    //     char info[256];

    //     snprintf(info, sizeof(info), "FPS: %.2f", stats->fps);
    //     cv::putText(img, info, cv::Point(10, 22),
    //                 cv::FONT_HERSHEY_SIMPLEX, 0.55,
    //                 cv::Scalar(255, 255, 255), 1);

    //     snprintf(info, sizeof(info), "TEMP: %.1f C", stats->temp);
    //     cv::putText(img, info, cv::Point(10, 45),
    //                 cv::FONT_HERSHEY_SIMPLEX, 0.55,
    //                 cv::Scalar(255, 255, 255), 1);

    //     snprintf(info, sizeof(info), "CPU: %.1f %%  GPU: %d %%",
    //              stats->cpu_usage, stats->gpu_load);
    //     cv::putText(img, info, cv::Point(10, 68),
    //                 cv::FONT_HERSHEY_SIMPLEX, 0.55,
    //                 cv::Scalar(255, 255, 255), 1);

    //     snprintf(info, sizeof(info), "NPU: %s", stats->npu_load);
    //     cv::putText(img, info, cv::Point(10, 92),
    //                 cv::FONT_HERSHEY_SIMPLEX, 0.45,
    //                 cv::Scalar(0, 255, 255), 1);
    // }
}

void rtsp_streamer_push(unsigned char* rgb_buffer, int width, int height)
{
    if (!rgb_buffer || width <= 0 || height <= 0) {
        return;
    }

    GstElement* appsrc = nullptr;

    {
        std::lock_guard<std::mutex> lock(g_appsrc_mutex);
        if (!g_appsrc) {
            return;
        }

        appsrc = GST_ELEMENT(gst_object_ref(g_appsrc));
    }

    const int size = width * height * 3;

    GstBuffer* buffer = gst_buffer_new_allocate(nullptr, size, nullptr);
    if (!buffer) {
        gst_object_unref(appsrc);
        return;
    }

    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_WRITE)) {
        gst_buffer_unref(buffer);
        gst_object_unref(appsrc);
        return;
    }

    memcpy(map.data, rgb_buffer, size);
    gst_buffer_unmap(buffer, &map);

    guint64 frame_id = g_frame_id.fetch_add(1);
    guint64 duration = gst_util_uint64_scale_int(1, GST_SECOND, g_fps);

    GST_BUFFER_PTS(buffer) = frame_id * duration;
    GST_BUFFER_DTS(buffer) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_DURATION(buffer) = duration;

    GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(appsrc), buffer);

    if (ret != GST_FLOW_OK) {
        if (ret == GST_FLOW_FLUSHING ||
            ret == GST_FLOW_EOS ||
            ret == GST_FLOW_NOT_LINKED) {
            drop_appsrc_ref();
        } else {
            printf("[RTSP] push buffer failed, ret=%d\n", ret);
        }
    }

    gst_object_unref(appsrc);
}

void rtsp_streamer_release(void)
{
    g_running = false;

    drop_appsrc_ref();

    if (g_loop) {
        g_main_loop_quit(g_loop);
    }

    if (g_rtsp_thread.joinable()) {
        g_rtsp_thread.join();
    }

    if (g_server_id != 0) {
        g_source_remove(g_server_id);
        g_server_id = 0;
    }

    if (g_server) {
        gst_object_unref(g_server);
        g_server = nullptr;
    }

    if (g_loop) {
        g_main_loop_unref(g_loop);
        g_loop = nullptr;
    }

    g_factory = nullptr;

    printf("[RTSP] server stopped\n");
}