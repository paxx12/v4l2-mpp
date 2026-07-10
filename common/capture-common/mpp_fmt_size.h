#ifndef MPP_FMT_SIZE_H
#define MPP_FMT_SIZE_H

#include <rockchip/mpp_frame.h>

__attribute__((unused)) static size_t mpp_fmt_frame_size(unsigned int hor_stride, unsigned int ver_stride, MppFrameFormat fmt)
{
    switch (fmt) {
    case MPP_FMT_YUV420SP:
    case MPP_FMT_YUV420SP_VU:
    case MPP_FMT_YUV420P:
        return (size_t)hor_stride * ver_stride * 3 / 2;
    case MPP_FMT_YUV422_YUYV:
    case MPP_FMT_YUV422_UYVY:
    case MPP_FMT_YUV422SP:
    case MPP_FMT_YUV422P:
        return (size_t)hor_stride * ver_stride * 2;
    case MPP_FMT_RGB888:
    case MPP_FMT_BGR888:
        return (size_t)hor_stride * ver_stride * 3;
    default:
        return (size_t)hor_stride * ver_stride * 3;
    }
}

#endif
