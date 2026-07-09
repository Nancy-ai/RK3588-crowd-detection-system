#include "camera.h"
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

/*
    摄像头获取模块，主要实现获取到NV12格式的数据
*/
bool camera_init(Camera* cam) {
    cam->fd = open(cam->dev_name, O_RDWR);
    if (cam->fd < 0) {
        perror("打开摄像头失败");
        return false;
    }

    // 1. 设置格式
    struct v4l2_format fmt = {0};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt.fmt.pix_mp.width = cam->width;
    fmt.fmt.pix_mp.height = cam->height;
    fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
    fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
    fmt.fmt.pix_mp.num_planes = 1;

    if (ioctl(cam->fd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("设置格式失败");
        return false;
    }

    // 2. 申请缓冲区
    struct v4l2_requestbuffers reqbuff = {0};
    reqbuff.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    reqbuff.count = BUFF_COUNT;
    reqbuff.memory = V4L2_MEMORY_MMAP;
    if (ioctl(cam->fd, VIDIOC_REQBUFS, &reqbuff) < 0) {
        perror("申请队列失败");
        return false;
    }

    // 3. 内存映射
    for (int i = 0; i < BUFF_COUNT; i++) {
        struct v4l2_buffer mapbuff = {0};
        struct v4l2_plane planes[1] = {0};
        mapbuff.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        mapbuff.memory = V4L2_MEMORY_MMAP;
        mapbuff.index = i;
        mapbuff.m.planes = planes;
        mapbuff.length = 1;

        if (ioctl(cam->fd, VIDIOC_QUERYBUF, &mapbuff) < 0) return false;
        
        cam->mptr[i] = (unsigned char *)mmap(NULL, planes[0].length, PROT_READ | PROT_WRITE, MAP_SHARED, cam->fd, planes[0].m.mem_offset);
        cam->buf_len[i] = planes[0].length;
        
        if (ioctl(cam->fd, VIDIOC_QBUF, &mapbuff) < 0) return false;
    }

    // 4. 开启流
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(cam->fd, VIDIOC_STREAMON, &type) < 0) return false;

    return true;
}

bool camera_capture(Camera* cam, unsigned char** nv12_ptr) {
    memset(&cam->buf, 0, sizeof(cam->buf));
    struct v4l2_plane planes[1] = {0};

    cam->buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    cam->buf.memory = V4L2_MEMORY_MMAP;
    cam->buf.m.planes = cam->planes;
    cam->buf.length = 1;

    // 1. 出队
    if (ioctl(cam->fd, VIDIOC_DQBUF, &cam->buf) < 0) {
        perror("DQBUF失败");
        return false;
    }

    // 2. 返回数据指针
    *nv12_ptr = cam->mptr[cam->buf.index];
    return true;
}

void camera_release(Camera* cam)
{
    if (ioctl(cam->fd, VIDIOC_QBUF, &cam->buf) < 0) {
        perror("QBUF 失败");
    }
}
void camera_close(Camera* cam) {
    
    if (cam->fd != -1) {
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        ioctl(cam->fd, VIDIOC_STREAMOFF, &type);
        for (int i = 0; i < BUFF_COUNT; i++) munmap(cam->mptr[i], cam->buf_len[i]);
        close(cam->fd);
        cam->fd = -1;
    }
}