// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-PackageHomePage: https://github.com/paxx12/v4l2-mpp
// SPDX-FileCopyrightText: Copyright (c) 2025 @paxx12

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

static const uint8_t *h264_nal_header(const uint8_t *nal, size_t nal_size) {
    if (nal_size >= 5 && nal[0] == 0 && nal[1] == 0 && nal[2] == 0 && nal[3] == 1)
        return nal + 4;
    if (nal_size >= 4 && nal[0] == 0 && nal[1] == 0 && nal[2] == 1)
        return nal + 3;
    return NULL;
}

static const uint8_t* h264_find_nal(const uint8_t* data, size_t size) {
    for (size_t i = 0; i + 3 < size; i++) {
        if (h264_nal_header(data + i, size - i))
            return data + i;
    }
    return NULL;
}

static bool h264_is_new_frame(const uint8_t* nal, size_t nal_size) {
    const uint8_t *header = h264_nal_header(nal, nal_size);
    if (!header) return false;

    uint8_t nal_type = header[0] & 0x1f;
    if (nal_type == 1 || nal_type == 5) {
        size_t header_offset = (size_t)(header - nal);
        if (nal_size <= header_offset + 1) return true;
        return (header[1] & 0x80) != 0;
    }
    return false;
}

static bool h264_is_idr_frame(const uint8_t *nal, size_t nal_size) {
    const uint8_t *header = h264_nal_header(nal, nal_size);
    return h264_is_new_frame(nal, nal_size) && header && (header[0] & 0x1f) == 5;
}

// Returns true when the first slice NAL in an Annex-B frame is an IDR slice.
// A valid non-IDR type 1 slice returns false. Parameter sets and other
// non-slice NALs before the first slice are skipped.
static bool h264_starts_with_idr(const uint8_t *data, size_t size) {
    if (!data) return false;
    const uint8_t *end = data + size;
    while (data < end) {
        const uint8_t *nal = h264_find_nal(data, (size_t)(end - data));
        if (!nal) return false;

        size_t nal_size = (size_t)(end - nal);
        if (h264_is_new_frame(nal, nal_size))
            return h264_is_idr_frame(nal, nal_size);
        data = nal + 3;
    }
    return false;
}

static bool h264_is_aud_frame(const uint8_t* nal, size_t nal_size) {
    const uint8_t *header = h264_nal_header(nal, nal_size);
    if (!header) return false;
    size_t header_offset = (size_t)(header - nal);
    return (header[0] & 0x1f) == 9 && nal_size == header_offset + 2 && (header[1] & 0x80) != 0;
}

static const uint8_t *h264_process_frames(const uint8_t *data, const uint8_t *end, void (*store_frame)(const uint8_t*, size_t)) {
    while ((end - data) >= 8) {
        const uint8_t* start = h264_find_nal(data, end - data);
        if (!start) {
            if (end - data > 4) {
                return end - 4;
            }
            return NULL;
        }

        const uint8_t* next = start;
        bool found_slice = false;

        while ((next = h264_find_nal(next + 4, end - next - 4)) != NULL) {
            if (h264_is_aud_frame(next, end - next)) {
                break;
            } else if (h264_is_new_frame(next, end - next)) {
                if (found_slice)
                    break;
                found_slice = true;
            }
        }
        if (!next) {
            return NULL;
        }

        store_frame(start, next - start);
        data = next;
    }

    return data;
}
