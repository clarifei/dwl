.POSIX:
.SUFFIXES:

include config.mk

# flags for compiling
INCACPPFLAGS = -I. -Iinclude -Iconfig -DWLR_USE_UNSTABLE -D_POSIX_C_SOURCE=200809L \
	-DVERSION=\"$(VERSION)\" -DINCA_SYSTEM_CONFIG=\"$(DATADIR)/inca/config.lua\" $(XWAYLAND)
INCADEVCFLAGS = -g -Wpedantic -Wall -Wextra -Wdeclaration-after-statement \
	-Wno-unused-parameter -Wshadow -Wunused-macros -Werror=strict-prototypes \
	-Werror=implicit -Werror=return-type -Werror=incompatible-pointer-types \
	-Wfloat-conversion

# CFLAGS / LDFLAGS
PKGS      = scenefx-0.4 wayland-server xkbcommon libinput lua5.4 cairo fontconfig libdrm pixman-1 $(XLIBS)
INCACFLAGS = `$(PKG_CONFIG) --cflags $(PKGS)` $(WLR_INCS) $(INCACPPFLAGS) $(INCADEVCFLAGS) $(CFLAGS)
LDLIBS    = `$(PKG_CONFIG) --libs $(PKGS)` $(WLR_LIBS) -lm $(LIBS)
INCA_SRC  = inca.c include/inca.h include/canvas.h include/client.h include/util.h src/background-effect.c src/client.c \
		src/input.c src/layout.c src/output.c src/server.c src/session.c src/config.c src/xwayland.c
PROTOCOL_OBJ = ext-background-effect-v1-protocol.o

all: inca
inca: inca.o util.o $(PROTOCOL_OBJ)
	$(CC) inca.o util.o $(PROTOCOL_OBJ) $(INCACFLAGS) $(LDFLAGS) $(LDLIBS) -o $@
inca.o: $(INCA_SRC) config.mk cursor-shape-v1-protocol.h \
		ext-background-effect-v1-protocol.h \
		pointer-constraints-unstable-v1-protocol.h wlr-layer-shell-unstable-v1-protocol.h \
		wlr-output-power-management-unstable-v1-protocol.h xdg-shell-protocol.h
util.o: util.c include/util.h
ext-background-effect-v1-protocol.o: ext-background-effect-v1-protocol.c \
		ext-background-effect-v1-protocol.h

test: tests/canvas_test
	./tests/canvas_test

tests/canvas_test: tests/canvas_test.c include/canvas.h
	$(CC) $(CPPFLAGS) -I. $(CFLAGS) tests/canvas_test.c -lm -o $@

# wayland-scanner is a tool which generates C headers and rigging for Wayland
# protocols, which are specified in XML. wlroots requires you to rig these up
# to your build system yourself and provide them in the include path.
WAYLAND_SCANNER   = `$(PKG_CONFIG) --variable=wayland_scanner wayland-scanner`
WAYLAND_PROTOCOLS = `$(PKG_CONFIG) --variable=pkgdatadir wayland-protocols`

cursor-shape-v1-protocol.h:
	$(WAYLAND_SCANNER) enum-header \
		$(WAYLAND_PROTOCOLS)/staging/cursor-shape/cursor-shape-v1.xml $@
ext-background-effect-v1-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		$(WAYLAND_PROTOCOLS)/staging/ext-background-effect/ext-background-effect-v1.xml $@
ext-background-effect-v1-protocol.c:
	$(WAYLAND_SCANNER) private-code \
		$(WAYLAND_PROTOCOLS)/staging/ext-background-effect/ext-background-effect-v1.xml $@
pointer-constraints-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) enum-header \
		$(WAYLAND_PROTOCOLS)/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml $@
wlr-layer-shell-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) enum-header \
		protocols/wlr-layer-shell-unstable-v1.xml $@
wlr-output-power-management-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		protocols/wlr-output-power-management-unstable-v1.xml $@
xdg-shell-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		$(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@

clean:
	rm -f inca *.o *-protocol.c *-protocol.h tests/canvas_test

dist: clean
	mkdir -p inca-$(VERSION)
	cp -R LICENSE* Makefile CHANGELOG.md README.md config include \
		config.mk protocols src inca.1 inca.c util.c inca.desktop \
		inca-$(VERSION)
	tar -caf inca-$(VERSION).tar.gz inca-$(VERSION)
	rm -rf inca-$(VERSION)

install: inca
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	rm -f $(DESTDIR)$(PREFIX)/bin/inca
	cp -f inca $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/inca
	mkdir -p $(DESTDIR)$(DATADIR)/inca
	cp -f config/config.lua $(DESTDIR)$(DATADIR)/inca/config.lua
	chmod 644 $(DESTDIR)$(DATADIR)/inca/config.lua
	mkdir -p $(DESTDIR)$(MANDIR)/man1
	cp -f inca.1 $(DESTDIR)$(MANDIR)/man1
	chmod 644 $(DESTDIR)$(MANDIR)/man1/inca.1
	mkdir -p $(DESTDIR)$(DATADIR)/wayland-sessions
	cp -f inca.desktop $(DESTDIR)$(DATADIR)/wayland-sessions/inca.desktop
	chmod 644 $(DESTDIR)$(DATADIR)/wayland-sessions/inca.desktop
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/inca $(DESTDIR)$(MANDIR)/man1/inca.1 \
		$(DESTDIR)$(DATADIR)/inca/config.lua \
		$(DESTDIR)$(DATADIR)/wayland-sessions/inca.desktop

.SUFFIXES: .c .o
.c.o:
	$(CC) $(CPPFLAGS) $(INCACFLAGS) -o $@ -c $<
