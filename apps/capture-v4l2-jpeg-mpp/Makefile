TARGET = capture-v4l2-jpeg-mpp
SRCS = main.c
OBJS = $(SRCS:.c=.o)
DEPS = $(SRCS:.c=.d)

CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -MMD  -I../../common -I../../common/capture-common
LDFLAGS ?=

ifneq (x,x$(wildcard $(CURDIR)/../../deps/mpp/usr-local/lib/pkgconfig))
PKG_CONFIG_PATH := $(CURDIR)/../../deps/mpp/usr-local/lib/pkgconfig:$(PKG_CONFIG_PATH)
endif

CFLAGS += $(shell PKG_CONFIG_PATH="$(PKG_CONFIG_PATH)" pkg-config --cflags rockchip_mpp)
LDFLAGS += $(shell PKG_CONFIG_PATH="$(PKG_CONFIG_PATH)" pkg-config --libs rockchip_mpp)

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

all: $(TARGET)

-include $(DEPS)

$(TARGET): $(OBJS)
	$(CC) $^ -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(DEPS) $(TARGET)

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

.PHONY: all clean install uninstall
