// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-PackageHomePage: https://github.com/paxx12/v4l2-mpp
// SPDX-FileCopyrightText: Copyright (c) 2026 @paxx12

#include <cassert>
#include <cstdint>

#include "h264_frames.h"

int main()
{
    static const uint8_t idr[] = { 0, 0, 0, 1, 0x65, 0x80 };
    assert(h264_starts_with_idr(idr, sizeof(idr)));
    return 0;
}
