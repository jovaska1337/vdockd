CC = gcc
STRIP = strip
CFLAGS = -march=native -mtune=native -O2 -pipe -Wall -Wextra -pedantic
LDFLAGS = -flto -fuse-linker-plugin
PREFIX = /usr/local

CFLAGS_LIBS = $(shell pkg-config --cflags libsystemd libevdev)
LDFLAGS_LIBS = $(shell pkg-config --libs libsystemd libevdev)

.PHONY: all
all: vdockd update-dock-status

.PHONY: install
install: all
	mkdir -vp \
		$(PREFIX)/lib/udev/rules.d \
		$(PREFIX)/lib/systemd/system \
		$(PREFIX)/bin $(PREFIX)/libexec \
		/etc/acpi/events
	cp -v vdockd $(PREFIX)/bin
	cp -v update-dock-status $(PREFIX)/libexec
	cp -v 70-vdockd-power-switch.rules $(PREFIX)/lib/udev/rules.d
	sed 's|@PREFIX@|$(PREFIX)|g' vdockd.service > $(PREFIX)/lib/systemd/system/vdockd.service
	sed 's|@PREFIX@|$(PREFIX)|g' update-dock-status.service > $(PREFIX)/lib/systemd/system/update-dock-status.service
	sed 's|@PREFIX@|$(PREFIX)|g' acpid/dock > /etc/acpi/events/dock
	sed 's|@PREFIX@|$(PREFIX)|g' acpid/undock > /etc/acpi/events/undock

.PHONY: clean
clean:
	-rm -v *.o vdockd update-dock-status

vdockd: vdockd.o
	$(CC) $(LDFLAGS) $(LDFLAGS_LIBS) -o $@ $^
	$(STRIP) -s $@

update-dock-status: update-dock-status.o 
	$(CC) $(LDFLAGS) -o $@ $^
	$(STRIP) -s $@

%.o : %.c
	$(CC) -c $(CFLAGS) $(CFLAGS_LIBS) -o $@ $^
