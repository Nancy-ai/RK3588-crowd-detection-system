#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <opencv2/opencv.hpp> // 依然用它保存图片，但不用它转换，RGA代劳
#include <chrono>

//引入头文件
#include "camera.h"
#include "RGA_processor.h"
#include "sys_monitor.h"
#include "detector.h"
#include "post_processor.h"
#include "display.h"
#include "reporter.h"
#include "tracker_adapter.h"
// #include "streamer.h"
#include "rtsp_streamer.h"
#include "event_analysis.h"


int main() {

    // 初始化 Camera 结构体
    Camera cam = { .fd = -1 ,.dev_name = "/dev/video11", .width = 1920, .height = 1080};
    if (!camera_init(&cam)) {
        fprintf(stderr, "摄像头初始化失败\n");
        return -1;
    }
    // 初始化 RGA 结构体 (从 1920x1080 转为 640x640)
    RgaContext rga_ctx;
    rga_init(&rga_ctx, 1920, 1080, 640, 640);

    // 分配 NPU/后续模型需要的 RGB 内存 (注意：RGB888 格式大小为 W*H*3)
    unsigned char* rgb_buffer = (unsigned char*)malloc(640 * 640 * 3 + 1024);
    if (!rgb_buffer) {
        fprintf(stderr, "RGB 内存分配失败\n");
        return -1;
    }
    memset(rgb_buffer, 0, 640 * 640 * 3 + 1024);

    //初始化NPU并加载模型
    //RknnContext rknn_ctx;
    RknnContext* rknn_ctx = (RknnContext*)malloc(sizeof(RknnContext));
    memset(rknn_ctx, 0, sizeof(RknnContext));
    const char* model_path = "../model/yolov8.rknn"; // 模型相对位置
    if (!detector_init(rknn_ctx, model_path)) {
        fprintf(stderr, "NPU 初始化失败，退出。\n");
        return -1;
    }
    
    // display_init("RK3588_AI_STATION");
    //实时检测帧率
    auto start_time = std::chrono::steady_clock::now();
    int fps_counter = 0;
    float current_fps = 0;

    tracker_init(30, 90);
    event_analyzer_init(640, 640, 30.0f);

    // 这里先随便给一个测试 ROI，坐标是 640x640 模型画面坐标。
    // 你后面根据实际画面调整。
    event_analyzer_set_roi(320, 150, 620, 390, "watch_area");

    // 采集循环
    printf("开始实时采集 (按 Ctrl+C 停止)...\n");
    unsigned char* nv12_ptr = NULL;
    int frame_idx = 0;

    //初始化上报：ubuntu的Qt 程序收
    reporter_init("192.168.1.23", 8000);
    // 这里填电脑 IP
    rtsp_streamer_init(8554, 640, 640, 30);
    // streamer_init("192.168.1.23", 5000, 640, 640, 30);

    auto last_report_time = std::chrono::steady_clock::now();

     // ---------------------------------------------------------
    //  实时处理循环
    // ---------------------------------------------------------
    while (true) { 

        // 采集原始 NV12 数据
        auto t0 = std::chrono::steady_clock::now();
        if (camera_capture(&cam, &nv12_ptr)) {
           // printf("[DEBUG] camera_capture ok\n");
            
            auto t1 = std::chrono::steady_clock::now();
            //  调用 RGA 硬件转换 NV12 -> RGB888
            if (rga_process(&rga_ctx, nv12_ptr, rgb_buffer)) {
                // printf("[DEBUG] rga_process ok\n");

                auto t2 = std::chrono::steady_clock::now();
                DetectResultGroup detect_results = {0};
                DetectResultGroup track_results = {0};
                camera_release(&cam);

                //C.调用NPU进行硬件推理
                if (detector_run(rknn_ctx, rgb_buffer,&detect_results)) {
                    // printf("[DEBUG] detector_run ok\n");

                    tracker_update(&detect_results, &track_results);
                    EventResultGroup events = {0};
                    TrackRuntimeInfoGroup runtime_info = {0};

                    event_analyzer_update(&track_results, &events, &runtime_info);

                    auto t3 = std::chrono::steady_clock::now();
                    fps_counter++;
                    // 结果实时反馈，每 30 帧计算并打印一次，终端显示，后续可删除
                    // if (frame_idx % 30 == 0 && frame_idx != 0) {
                    //     auto end_time = std::chrono::steady_clock::now();
                    //     // 计算时间差（秒）
                    //     double elapsed = std::chrono::duration<double>(end_time - start_time).count();
                    //     current_fps = fps_counter / elapsed; // 算出 FPS
                        
                    //     //  终端打印
                    //     printf("\n[帧 %d] FPS: %.2f | 目标数: %d\n", frame_idx, current_fps, detect_results.count);
                    //     print_system_status(); 

                    // // --- 3. 极其重要：打印完后要重置计数器和时间 ---
                    //     fps_counter = 0;
                    //     start_time = std::chrono::steady_clock::now();
                    // }

                    //获取系统状态
                    SystemStats stats = {0};
                    get_system_status(&stats); // 修改后的监控模块
                    stats.fps = current_fps;

                    auto now_report_time = std::chrono::steady_clock::now();
                    double report_elapsed =
                    std::chrono::duration<double>(now_report_time - last_report_time).count();

                    if (report_elapsed >= 1.0) {
                        reporter_send(&track_results, &runtime_info, &events, &stats);
                        last_report_time = now_report_time;
                    }

                    rtsp_draw_overlay(rgb_buffer, 640, 640, &track_results, &stats);
                    event_analyzer_draw_overlay(rgb_buffer, 640, 640, &track_results, &runtime_info, &events);
                    rtsp_streamer_push(rgb_buffer, 640, 640);
                    // streamer_push(rgb_buffer, 640, 640, &detect_results, &stats);
                    // printf("[DEBUG] streamer_push ok\n");

                    // --- 【一键显示】 ---
                    // int key = display_show("RK3588_AI_STATION", rgb_buffer, 640, 640, &detect_results, &stats);
                    // printf("[DEBUG] display_show ok\n");

                    auto t4 = std::chrono::steady_clock::now();

                    if (frame_idx % 30 == 0 && frame_idx != 0) {
                        auto end_time = std::chrono::steady_clock::now();

                        double elapsed = std::chrono::duration<double>(end_time - start_time).count();
                        current_fps = fps_counter / elapsed;

                        double camera_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
                        double rga_ms    = std::chrono::duration<double, std::milli>(t2 - t1).count();
                        double npu_ms    = std::chrono::duration<double, std::milli>(t3 - t2).count();
                        double disp_ms   = std::chrono::duration<double, std::milli>(t4 - t3).count();
                        double total_ms  = std::chrono::duration<double, std::milli>(t4 - t0).count();

                        printf("\n[帧 %d] FPS: %.2f | 跟踪人数: %d | 事件数: %d\n",
                                frame_idx,
                                current_fps,
                                track_results.count,
                                events.count);

                        printf("[耗时] total=%.2f ms | camera=%.2f | rga=%.2f | npu+post=%.2f | display=%.2f\n",
                            total_ms,
                            camera_ms,
                            rga_ms,
                            npu_ms,
                            disp_ms);

                        print_system_status();

                        fps_counter = 0;
                        start_time = std::chrono::steady_clock::now();
                }

                    // if (key == 'q' || key == 27) break;



                } else {
                    fprintf(stderr, "[Frame %3d/100] NPU推理失败！\n", frame_idx);
                }
            }

            
        }
        frame_idx++;

        if (frame_idx > 10000) break; //demo演示结束条件
    }

    // 5. 资源回收
    printf("[+] 推理结束，释放资源...\n");
    rtsp_streamer_release();
    // streamer_release();
    detector_release(rknn_ctx);
    free(rknn_ctx);
    rknn_ctx = NULL;
    rga_release(&rga_ctx);
    free(rgb_buffer);
    tracker_release();
    event_analyzer_release();
    reporter_close();
    camera_close(&cam);
    printf("程序退出\n");

    return 0;
}