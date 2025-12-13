#ifndef MPP_ENC_CTX_H
#define MPP_ENC_CTX_H

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
    MppBufferGroup buf_grp;
    MppEncCfg cfg;
    unsigned int width;
    unsigned int height;
    MppFrameFormat fmt;
} mpp_enc_ctx_t;

__attribute__((unused)) static int mpp_jpeg_encoder_init(mpp_enc_ctx_t *ctx, unsigned int width, unsigned int height, MppFrameFormat fmt, unsigned int quality)
{
    MPP_RET ret;

    ctx->width = width;
    ctx->height = height;
    ctx->fmt = fmt;

    ret = mpp_create(&ctx->ctx, &ctx->mpi);
    if (ret != MPP_OK) {
        log_errorf("mpp_create failed: %d\n", ret);
        return -1;
    }

    ret = mpp_init(ctx->ctx, MPP_CTX_ENC, MPP_VIDEO_CodingMJPEG);
    if (ret != MPP_OK) {
        log_errorf("mpp_init failed: %d\n", ret);
        return -1;
    }

    ret = mpp_enc_cfg_init(&ctx->cfg);
    if (ret != MPP_OK) {
        log_errorf("mpp_enc_cfg_init failed: %d\n", ret);
        return -1;
    }

    mpp_enc_cfg_set_s32(ctx->cfg, "prep:width", width);
    mpp_enc_cfg_set_s32(ctx->cfg, "prep:height", height);
    mpp_enc_cfg_set_s32(ctx->cfg, "prep:hor_stride", width);
    mpp_enc_cfg_set_s32(ctx->cfg, "prep:ver_stride", height);
    mpp_enc_cfg_set_s32(ctx->cfg, "prep:format", fmt);
    mpp_enc_cfg_set_s32(ctx->cfg, "rc:mode", MPP_ENC_RC_MODE_FIXQP);
    mpp_enc_cfg_set_s32(ctx->cfg, "jpeg:quant", quality);

    ret = ctx->mpi->control(ctx->ctx, MPP_ENC_SET_CFG, ctx->cfg);
    if (ret != MPP_OK) {
        log_errorf("MPP_ENC_SET_CFG failed: %d\n", ret);
        return -1;
    }

    ret = mpp_buffer_group_get_internal(&ctx->buf_grp, MPP_BUFFER_TYPE_DRM);
    if (ret != MPP_OK) {
        log_errorf("mpp_buffer_group_get_internal failed: %d\n", ret);
        return -1;
    }

    return 0;
}

__attribute__((unused)) static int mpp_h264_encoder_init(mpp_enc_ctx_t *ctx, unsigned int width, unsigned int height, MppFrameFormat fmt, unsigned int bitrate, unsigned int fps)
{
    MPP_RET ret;

    ctx->width = width;
    ctx->height = height;
    ctx->fmt = fmt;

    ret = mpp_create(&ctx->ctx, &ctx->mpi);
    if (ret != MPP_OK) {
        log_errorf("mpp_create failed: %d\n", ret);
        return -1;
    }

    ret = mpp_init(ctx->ctx, MPP_CTX_ENC, MPP_VIDEO_CodingAVC);
    if (ret != MPP_OK) {
        log_errorf("mpp_init failed: %d\n", ret);
        return -1;
    }

    ret = mpp_enc_cfg_init(&ctx->cfg);
    if (ret != MPP_OK) {
        log_errorf("mpp_enc_cfg_init failed: %d\n", ret);
        return -1;
    }

    mpp_enc_cfg_set_s32(ctx->cfg, "prep:width", width);
    mpp_enc_cfg_set_s32(ctx->cfg, "prep:height", height);
    mpp_enc_cfg_set_s32(ctx->cfg, "prep:hor_stride", width);
    mpp_enc_cfg_set_s32(ctx->cfg, "prep:ver_stride", height);
    mpp_enc_cfg_set_s32(ctx->cfg, "prep:format", fmt);
    mpp_enc_cfg_set_s32(ctx->cfg, "rc:mode", MPP_ENC_RC_MODE_CBR);
    mpp_enc_cfg_set_s32(ctx->cfg, "rc:bps_target", bitrate * 1000);
    mpp_enc_cfg_set_s32(ctx->cfg, "rc:bps_max", bitrate * 1500);
    mpp_enc_cfg_set_s32(ctx->cfg, "rc:bps_min", bitrate * 500);
    mpp_enc_cfg_set_s32(ctx->cfg, "rc:fps_in_flex", 0);
    mpp_enc_cfg_set_s32(ctx->cfg, "rc:fps_in_num", fps);
    mpp_enc_cfg_set_s32(ctx->cfg, "rc:fps_in_denorm", 1);
    mpp_enc_cfg_set_s32(ctx->cfg, "rc:fps_out_flex", 0);
    mpp_enc_cfg_set_s32(ctx->cfg, "rc:fps_out_num", fps);
    mpp_enc_cfg_set_s32(ctx->cfg, "rc:fps_out_denorm", 1);
    mpp_enc_cfg_set_s32(ctx->cfg, "rc:gop", fps * 2);
    mpp_enc_cfg_set_s32(ctx->cfg, "codec:type", MPP_VIDEO_CodingAVC);
    mpp_enc_cfg_set_s32(ctx->cfg, "h264:profile", 100);
    mpp_enc_cfg_set_s32(ctx->cfg, "h264:level", 41);
    mpp_enc_cfg_set_s32(ctx->cfg, "h264:cabac_en", 1);
    mpp_enc_cfg_set_s32(ctx->cfg, "h264:cabac_idc", 0);

    ret = ctx->mpi->control(ctx->ctx, MPP_ENC_SET_CFG, ctx->cfg);
    if (ret != MPP_OK) {
        log_errorf("MPP_ENC_SET_CFG failed: %d\n", ret);
        return -1;
    }

    MppEncHeaderMode header_mode = MPP_ENC_HEADER_MODE_EACH_IDR;
    ret = ctx->mpi->control(ctx->ctx, MPP_ENC_SET_HEADER_MODE, &header_mode);
    if (ret != MPP_OK) {
        log_errorf("MPP_ENC_SET_HEADER_MODE failed: %d\n", ret);
    }

    ret = mpp_buffer_group_get_internal(&ctx->buf_grp, MPP_BUFFER_TYPE_DRM);
    if (ret != MPP_OK) {
        log_errorf("mpp_buffer_group_get_internal failed: %d\n", ret);
        return -1;
    }

    return 0;
}

__attribute__((unused)) static MppPacket mpp_encode_mppframe(mpp_enc_ctx_t *ctx, MppFrame frame, int force_idr)
{
    MPP_RET ret;
    MppPacket packet = NULL;
    MppMeta meta = NULL;

    if (force_idr) {
        meta = mpp_frame_get_meta(frame);
        if (meta) {
            mpp_meta_set_s32(meta, KEY_INPUT_IDR_REQ, 1);
        }
    }

    ret = ctx->mpi->encode_put_frame(ctx->ctx, frame);
    if (ret != MPP_OK) {
        log_errorf("encode_put_frame failed: %d\n", ret);
        return NULL;
    }

    ret = ctx->mpi->encode_get_packet(ctx->ctx, &packet);
    if (ret != MPP_OK || !packet) {
        log_errorf("encode_get_packet failed: %d\n", ret);
        return NULL;
    }

    return packet;
}

__attribute__((unused)) static MppPacket mpp_encode_frame(mpp_enc_ctx_t *ctx, void *data, size_t size, int force_idr)
{
    MPP_RET ret;
    MppFrame frame = NULL;
    MppPacket packet = NULL;
    MppBuffer frame_buf = NULL;
    size_t frame_size;
    void *frame_ptr;

    frame_size = ctx->width * ctx->height * 3;

    ret = mpp_buffer_get(ctx->buf_grp, &frame_buf, frame_size);
    if (ret != MPP_OK) {
        log_errorf("mpp_buffer_get frame failed: %d\n", ret);
        return NULL;
    }

    frame_ptr = mpp_buffer_get_ptr(frame_buf);
    memcpy(frame_ptr, data, size < frame_size ? size : frame_size);

    ret = mpp_frame_init(&frame);
    if (ret != MPP_OK) {
        log_errorf("mpp_frame_init failed: %d\n", ret);
        mpp_buffer_put(frame_buf);
        return NULL;
    }

    mpp_frame_set_width(frame, ctx->width);
    mpp_frame_set_height(frame, ctx->height);
    mpp_frame_set_hor_stride(frame, ctx->width);
    mpp_frame_set_ver_stride(frame, ctx->height);
    mpp_frame_set_fmt(frame, ctx->fmt);
    mpp_frame_set_buffer(frame, frame_buf);
    mpp_frame_set_eos(frame, 0);

    packet = mpp_encode_mppframe(ctx, frame, force_idr);

    mpp_frame_deinit(&frame);
    mpp_buffer_put(frame_buf);

    return packet;
}

static void mpp_encoder_close(mpp_enc_ctx_t *ctx)
{
    if (ctx->cfg) {
        mpp_enc_cfg_deinit(ctx->cfg);
    }
    if (ctx->buf_grp) {
        mpp_buffer_group_put(ctx->buf_grp);
    }
    if (ctx->ctx) {
        ctx->mpi->reset(ctx->ctx);
        mpp_destroy(ctx->ctx);
    }
}

#endif
