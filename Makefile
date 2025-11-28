APPS = stream-http stream-snap-mqtt
APPS_DIR = apps

ifneq (x,x$(wildcard deps/mpp/usr-local/lib/librockchip_mpp.a))
APPS += capture-v4l2-jpeg-mpp capture-v4l2-raw-mpp
else
$(warning "MPP not compiled. Run ./deps/compile_mpp.sh to compile it.")
endif

ifneq (x,x$(wildcard deps/libdatachannel/build/libdatachannel-static.a))
APPS += stream-webrtc
else
$(warning "libdatachannel not compiled. Run ./deps/compile_libdatachannel.sh to compile it.")
endif

ifneq (x,x$(wildcard deps/live/liveMedia/libliveMedia.a))
APPS += stream-rtsp
else
$(warning "libliveMedia not compiled. Run ./deps/compile_livemedia.sh to compile it.")
endif

.PHONY: all clean install uninstall deps $(APPS)

all: $(APPS)

$(APPS):
	$(MAKE) -C $(APPS_DIR)/$@

deps:
	deps/compile_mpp.sh
	deps/compile_libdatachannel.sh
	deps/compile_livemedia.sh

clean:
	@for app in $(APPS); do \
		$(MAKE) -C $(APPS_DIR)/$$app clean; \
	done

install:
	@for app in $(APPS); do \
		$(MAKE) -C $(APPS_DIR)/$$app install DESTDIR=$(DESTDIR); \
	done

uninstall:
	@for app in $(APPS); do \
		$(MAKE) -C $(APPS_DIR)/$$app uninstall DESTDIR=$(DESTDIR); \
	done
