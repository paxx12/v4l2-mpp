TARGET = stream-http.py
SRC = main.py

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

all: $(TARGET)

clean:
	rm -f $(TARGET)

$(TARGET): main.py html_index.py html_player.py html_webrtc.py
	sed -e '/^from html_index import/r html_index.py' \
	    -e '/^from html_index import/d' \
	    -e '/^from html_player import/r html_player.py' \
	    -e '/^from html_player import/d' \
	    -e '/^from html_webrtc import/r html_webrtc.py' \
	    -e '/^from html_webrtc import/d' \
	    main.py > $(TARGET).tmp
	chmod +x $@.tmp
	mv $(TARGET).tmp $@

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

.PHONY: all clean install uninstall
