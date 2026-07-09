#include "display.h"
#include <opencv2/opencv.hpp>
#include <stdio.h>

using namespace cv;
/*
显示模块：
将摄像头采集到的视频流在开发板的屏幕显示出来
将系统状态显示出来
*/
void display_init(const char* window_name) {
    namedWindow(window_name, WINDOW_AUTOSIZE);
    // 如果你在板子上接了屏幕，可以取消下面这行的注释来全屏显示
    // setWindowProperty(window_name, WND_PROP_FULLSCREEN, WINDOW_FULLSCREEN);
}

int display_show(const char* window_name, unsigned char* rgb_data, int w, int h, 
                 DetectResultGroup* group, SystemStats* stats) {
    
    // 1. 包装数据：RGB -> BGR (OpenCV 默认色彩空间)
    Mat rgbMat(h, w, CV_8UC3, rgb_data);
    Mat bgrMat;
    cvtColor(rgbMat, bgrMat, COLOR_RGB2BGR);

    // 2. 绘制 AI 检测框
    for (int i = 0; i < group->count; i++) {
        DetectResult *res = &(group->results[i]);
        rectangle(bgrMat, Point(res->x1, res->y1), Point(res->x2, res->y2), Scalar(0, 255, 0), 2);
        
        char label[64];
        // sprintf(label, "ID:%d %.2f", res->id, res->prob);
        sprintf(label, "T:%d C:%d %.2f", res->track_id, res->id, res->prob);
        putText(bgrMat, label, Point(res->x1, res->y1 - 5), FONT_HERSHEY_SIMPLEX, 0.5, Scalar(0, 255, 0), 1);
    }

    // 3. 绘制 HUD (平视显示器) - 系统监控数据
    // 先画一个黑色半透明背景条，让文字更清晰
    rectangle(bgrMat, Rect(0, 0, 240, 100), Scalar(0, 0, 0), -1);
    
    char info[256];
    Scalar textColor(255, 255, 255); // 白色

    sprintf(info, "FPS:  %.2f", stats->fps);
    putText(bgrMat, info, Point(10, 20), FONT_HERSHEY_SIMPLEX, 0.5, textColor, 1);

    sprintf(info, "TEMP: %.1f C", stats->temp);
    putText(bgrMat, info, Point(10, 40), FONT_HERSHEY_SIMPLEX, 0.5, textColor, 1);

    sprintf(info, "CPU:  %.1f %%", stats->cpu_usage);
    putText(bgrMat, info, Point(10, 60), FONT_HERSHEY_SIMPLEX, 0.5, textColor, 1);

    sprintf(info, "NPU:  %s", stats->npu_load);
    putText(bgrMat, info, Point(10, 80), FONT_HERSHEY_SIMPLEX, 0.5, textColor, 1);

     Mat screenMat;
    // 使用 RGA 缩放最快，如果追求简单先用 cv::resize
    cv::resize(bgrMat, screenMat, Size(1920, 1080)); 

    // 4. 正式推送到屏幕显示
    imshow(window_name, bgrMat);

    // 5. 必须调用 waitKey，否则窗口会卡死不刷新
    // 返回按键值，如果返回 27 (ESC) 或 'q'，可以通知主程序退出
    return waitKey(1);
}

void display_exit(void) {
    destroyAllWindows();
}