TARGET = stream-rtsp
SRCS = main.cpp
OBJS = $(SRCS:.cpp=.o)

CXX ?= g++
CXXFLAGS ?= -Wall -Wextra -O2 -std=c++17 -I../../common/stream-common
LDFLAGS ?= -static-libstdc++

LIVE555_PATH ?= ../../deps/live/usr-local/

CXXFLAGS += -I$(LIVE555_PATH)/include/liveMedia
CXXFLAGS += -I$(LIVE555_PATH)/include/groupsock
CXXFLAGS += -I$(LIVE555_PATH)/include/BasicUsageEnvironment
CXXFLAGS += -I$(LIVE555_PATH)/include/UsageEnvironment

LDFLAGS += -L$(LIVE555_PATH)/lib
LDFLAGS += -lliveMedia -lgroupsock -lBasicUsageEnvironment -lUsageEnvironment
LDFLAGS += -lssl -lcrypto -lpthread

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $^ -o $@ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

.PHONY: all clean install uninstall
