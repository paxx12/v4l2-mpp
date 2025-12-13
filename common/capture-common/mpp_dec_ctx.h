#ifndef MPP_DEC_CTX_H
#define MPP_DEC_CTX_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <rockchip/rk_mpi.h>
#include <rockchip/mpp_buffer.h>
#include <rockchip/mpp_frame.h>
#include <rockchip/mpp_packet.h>
#include "log.h"

typedef struct {
    MppCtx ctx;
    MppApi *mpi;
    MppBufferGroup frm_grp;
    MppBufferGroup pkt_grp;
    unsigned int width;
    unsigned int height;
    MppFrameFormat format;
} mpp_dec_ctx_t;

__attribute__((unused)) static unsigned int mpp_align_up(unsigned int value, unsigned int align)
{
    return (value + align - 1) & ~(align - 1);
}

__attribute__((unused)) static int mpp_jpeg_decoder_init(mpp_dec_ctx_t *ctx, unsigned int width, unsigned int height, MppFrameFormat fmt)
{
    MPP_RET ret;

    ctx->width = width;
    ctx->height = height;
    ctx->format = fmt;

    ret = mpp_create(&ctx->ctx, &ctx->mpi);
    if (ret != MPP_OK) {
        log_errorf("mpp_create failed: %d\n", ret);
        return -1;
    }

    ret = mpp_init(ctx->ctx, MPP_CTX_DEC, MPP_VIDEO_CodingMJPEG);
    if (ret != MPP_OK) {
        log_errorf("mpp_init decoder failed: %d\n", ret);
        return -1;
    }

    MppDecCfg cfg = NULL;
    mpp_dec_cfg_init(&cfg);
    mpp_dec_cfg_set_u32(cfg, "base:out_fmt", fmt);
    ret = ctx->mpi->control(ctx->ctx, MPP_DEC_SET_CFG, cfg);
    mpp_dec_cfg_deinit(cfg);

    if (ret != MPP_OK) {
        log_errorf("MPP_DEC_SET_CFG failed: %d\n", ret);
        return -1;
    }

    if (MPP_FRAME_FMT_IS_YUV(fmt) || MPP_FRAME_FMT_IS_RGB(fmt)) {
        ret = ctx->mpi->control(ctx->ctx, MPP_DEC_SET_OUTPUT_FORMAT, &fmt);
        if (ret != MPP_OK) {
            log_errorf("Failed to set output format 0x%x\n", fmt);
            return -1;
        }
    }

    ret = mpp_buffer_group_get_internal(&ctx->pkt_grp, MPP_BUFFER_TYPE_ION);
    if (ret != MPP_OK) {
        log_errorf("mpp_buffer_group_get_internal pkt failed: %d\n", ret);
        return -1;
    }

    ret = mpp_buffer_group_get_internal(&ctx->frm_grp, MPP_BUFFER_TYPE_ION);
    if (ret != MPP_OK) {
        log_errorf("mpp_buffer_group_get_internal frm failed: %d\n", ret);
        return -1;
    }

    return 0;
}

__attribute__((unused)) static MppFrame mpp_decode_jpeg(mpp_dec_ctx_t *ctx, void *data, size_t size)
{
    MPP_RET ret;
    MppTask task = NULL;
    MppBuffer pkt_buf = NULL;
    MppBuffer frm_buf = NULL;
    MppPacket packet = NULL;
    MppFrame frame = NULL;
    size_t frame_size;
    unsigned int hor_stride = mpp_align_up(ctx->width, 16);
    unsigned int ver_stride = mpp_align_up(ctx->height, 16);

    frame_size = hor_stride * ver_stride * 2;

    ret = mpp_buffer_get(ctx->pkt_grp, &pkt_buf, size);
    if (ret != MPP_OK) {
        log_errorf("mpp_buffer_get pkt failed: %d\n", ret);
        return NULL;
    }

    memcpy(mpp_buffer_get_ptr(pkt_buf), data, size);

    ret = mpp_packet_init_with_buffer(&packet, pkt_buf);
    if (ret != MPP_OK) {
        log_errorf("mpp_packet_init_with_buffer failed: %d\n", ret);
        mpp_buffer_put(pkt_buf);
        return NULL;
    }
    mpp_packet_set_length(packet, size);

    ret = mpp_buffer_get(ctx->frm_grp, &frm_buf, frame_size);
    if (ret != MPP_OK) {
        log_errorf("mpp_buffer_get frm failed: %d\n", ret);
        mpp_packet_deinit(&packet);
        mpp_buffer_put(pkt_buf);
        return NULL;
    }

    ret = mpp_frame_init(&frame);
    if (ret != MPP_OK) {
        log_errorf("mpp_frame_init failed: %d\n", ret);
        mpp_packet_deinit(&packet);
        mpp_buffer_put(pkt_buf);
        mpp_buffer_put(frm_buf);
        return NULL;
    }
    mpp_frame_set_width(frame, ctx->width);
    mpp_frame_set_height(frame, ctx->height);
    mpp_frame_set_hor_stride(frame, hor_stride);
    mpp_frame_set_ver_stride(frame, ver_stride);
    mpp_frame_set_fmt(frame, ctx->format);
    mpp_frame_set_buffer(frame, frm_buf);

    ret = ctx->mpi->poll(ctx->ctx, MPP_PORT_INPUT, MPP_POLL_BLOCK);
    if (ret != MPP_OK) {
        log_errorf("poll input failed: %d\n", ret);
        goto error;
    }

    ret = ctx->mpi->dequeue(ctx->ctx, MPP_PORT_INPUT, &task);
    if (ret != MPP_OK || !task) {
        log_errorf("dequeue input failed: %d\n", ret);
        goto error;
    }

    mpp_task_meta_set_packet(task, KEY_INPUT_PACKET, packet);
    mpp_task_meta_set_frame(task, KEY_OUTPUT_FRAME, frame);

    ret = ctx->mpi->enqueue(ctx->ctx, MPP_PORT_INPUT, task);
    if (ret != MPP_OK) {
        log_errorf("enqueue input failed: %d\n", ret);
        goto error;
    }

    ret = ctx->mpi->poll(ctx->ctx, MPP_PORT_OUTPUT, MPP_POLL_BLOCK);
    if (ret != MPP_OK) {
        log_errorf("poll output failed: %d\n", ret);
        goto error;
    }

    ret = ctx->mpi->dequeue(ctx->ctx, MPP_PORT_OUTPUT, &task);
    if (ret != MPP_OK || !task) {
        log_errorf("dequeue output failed: %d\n", ret);
        goto error;
    }

    MppFrame output_frame = NULL;
    mpp_task_meta_get_frame(task, KEY_OUTPUT_FRAME, &output_frame);

    ret = ctx->mpi->enqueue(ctx->ctx, MPP_PORT_OUTPUT, task);
    if (ret != MPP_OK) {
        log_errorf("enqueue output failed: %d\n", ret);
    }

    mpp_packet_deinit(&packet);
    mpp_buffer_put(pkt_buf);
    mpp_buffer_put(frm_buf);

    return output_frame;

error:
    if (frame) mpp_frame_deinit(&frame);
    if (packet) mpp_packet_deinit(&packet);
    if (pkt_buf) mpp_buffer_put(pkt_buf);
    if (frm_buf) mpp_buffer_put(frm_buf);
    return NULL;
}

static void mpp_decoder_close(mpp_dec_ctx_t *ctx)
{
    if (ctx->pkt_grp) {
        mpp_buffer_group_put(ctx->pkt_grp);
    }
    if (ctx->frm_grp) {
        mpp_buffer_group_put(ctx->frm_grp);
    }
    if (ctx->ctx) {
        ctx->mpi->reset(ctx->ctx);
        mpp_destroy(ctx->ctx);
    }
}

#endif
