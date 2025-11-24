APPS = capture-usb-mpp capture-mipi-mpp stream-http stream-snap-mqtt stream-webrtc stream-rtsp
APPS_DIR = apps

.PHONY: all clean install uninstall $(APPS)

all: $(APPS)

$(APPS):
	$(MAKE) -C $(APPS_DIR)/$@

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
