#ifndef CAMERA_H
#define CAMERA_H

#include <linux/videodev2.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BUFF_COUNT 4

typedef struct {
    int fd;
    const char* dev_name;
    int width;
    int height;
    unsigned char *mptr[BUFF_COUNT];
    int buf_len[BUFF_COUNT];
    struct v4l2_buffer buf;
    struct v4l2_plane planes[1];
} Camera;

// 函数声明
bool camera_init(Camera* cam);
bool camera_capture(Camera* cam, unsigned char** nv12_ptr);
void camera_release(Camera* cam);
void camera_close(Camera* cam);

#ifdef __cplusplus
}
#endif

#endif