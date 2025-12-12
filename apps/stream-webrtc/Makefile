TARGET = stream-webrtc
SRCS = main.cpp
OBJS = $(SRCS:.cpp=.o)
DEPS = $(SRCS:.cpp=.d)

CXX ?= g++
CXXFLAGS ?= -Wall -Wextra -O2 -MMD -std=c++17  -I../../common -I../../common/stream-common
LDFLAGS ?= -static-libstdc++

LIBDATACHANNEL_PATH := $(CURDIR)/../../deps/libdatachannel

CXXFLAGS += -I$(LIBDATACHANNEL_PATH)/include
CXXFLAGS += -I$(LIBDATACHANNEL_PATH)/deps/json/include
LDFLAGS += -L$(LIBDATACHANNEL_PATH)/build -ldatachannel-static
LDFLAGS += -L$(LIBDATACHANNEL_PATH)/build/deps/usrsctp/usrsctplib -lusrsctp
LDFLAGS += -L$(LIBDATACHANNEL_PATH)/build/deps/libsrtp -lsrtp2
LDFLAGS += -L$(LIBDATACHANNEL_PATH)/build/deps/libjuice -ljuice-static
LDFLAGS += -lcrypto -lssl -lpthread

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

all: $(TARGET)

-include $(DEPS)

$(TARGET): $(OBJS)
	$(CXX) $^ -o $@ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(DEPS) $(TARGET)

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

.PHONY: all clean install uninstall
