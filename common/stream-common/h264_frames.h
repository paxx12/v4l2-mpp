#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

static const uint8_t* h264_find_nal(const uint8_t* data, size_t size) {
    for (size_t i = 0; i + 3 < size; i++) {
        if (data[i] == 0 && data[i+1] == 0 && data[i+2] == 0 && data[i+3] == 1 && size - i > 4) {
            return data + i;
        }
    }
    return nullptr;
}

static bool h264_is_new_frame(const uint8_t* nal, size_t nal_size) {
    if (nal_size < 5) return false;
    uint8_t nal_type = nal[4] & 0x1f;
    if (nal_type == 1 || nal_type == 5) {
        if (nal_size < 6) return true;
        uint8_t first_byte = nal[5];
        return (first_byte & 0x80) != 0;
    }
    return false;
}

static bool h264_is_aud_frame(const uint8_t* nal, size_t nal_size) {
    if (nal_size < 5) return false;
    uint8_t nal_type = nal[4] & 0x1f;
    return nal_type == 9 && nal_size == 6 && (nal[5] & 0x80) != 0;
}

static const uint8_t *h264_process_frames(const uint8_t *data, const uint8_t *end, void (*store_frame)(const uint8_t*, size_t)) {
    while ((end - data) >= 8) {
        const uint8_t* start = h264_find_nal(data, end - data);
        if (!start) {
            if (end - data > 4) {
                return end - 4;
            }
            return nullptr;
        }

        const uint8_t* next = start;
        bool found_slice = false;

        while ((next = h264_find_nal(next + 4, end - next - 4)) != nullptr) {
            if (h264_is_aud_frame(next, end - next)) {
                break;
            } else if (h264_is_new_frame(next, end - next)) {
                if (found_slice)
                    break;
                found_slice = true;
            }
        }
        if (!next) {
            return nullptr;
        }

        store_frame(start, next - start);
        data = next;
    }

    return data;
}
