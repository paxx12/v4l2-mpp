# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-PackageHomePage: https://github.com/paxx12/v4l2-mpp
# SPDX-FileCopyrightText: Copyright (c) 2026 @paxx12

CC ?= gcc
CXX ?= g++
CFLAGS ?= -Wall -Wextra -Werror -Wno-unused-function -O2
CXXFLAGS ?= -Wall -Wextra -Werror -Wno-unused-function -O2 -std=c++17
CPPFLAGS += -I../common -I../common/capture-common -I../common/stream-common

all: test-common test-h264-cpp

check: all
	./test-common
	./test-h264-cpp

test-common: test-common.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@

test-h264-cpp: test-h264-cpp.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@

clean:
	rm -f test-common test-h264-cpp test-common.sock

.PHONY: all check clean
