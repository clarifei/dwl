#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>

#include "xdg-shell-client-protocol.h"

typedef struct {
	bool compositor;
	bool layer_shell;
	bool output;
	bool seat;
	bool xdg_shell;
	bool argb8888;
	bool configured;
	bool mapped;
	bool closed;
	struct wl_compositor *wl_compositor;
	struct wl_shm *wl_shm;
	struct xdg_wm_base *xdg_wm_base;
	struct wl_surface *surface;
	struct wl_shm_pool *pool;
	struct wl_buffer *buffer;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *toplevel;
} Globals;


static void
wm_base_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial)
{
	(void)data;
	xdg_wm_base_pong(wm_base, serial);
}


static void
shm_format(void *data, struct wl_shm *wl_shm, uint32_t format)
{
	Globals *globals = data;

	(void)wl_shm;
	if (format == WL_SHM_FORMAT_ARGB8888)
		globals->argb8888 = true;
}


static int
create_buffer(Globals *globals)
{
	char path[] = "/tmp/inca-wayland-smoke-XXXXXX";
	const int width = 32, height = 32, stride = width * 4;
	const size_t length = (size_t)stride * height;
	uint32_t *pixels;
	int fd;

	fd = mkstemp(path);
	if (fd < 0)
		return -1;
	if (unlink(path) || ftruncate(fd, (off_t)length)) {
		close(fd);
		return -1;
	}
	pixels = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (pixels == MAP_FAILED) {
		close(fd);
		return -1;
	}
	for (size_t i = 0; i < (size_t)width * height; i++)
		pixels[i] = 0xff27465a;
	globals->pool = wl_shm_create_pool(globals->wl_shm, fd, (int)length);
	globals->buffer = globals->pool ? wl_shm_pool_create_buffer(globals->pool,
			0, width, height, stride, WL_SHM_FORMAT_ARGB8888) : NULL;
	munmap(pixels, length);
	close(fd);
	return globals->buffer ? 0 : -1;
}


static void
xdg_surface_configure(void *data, struct xdg_surface *xdg_surface,
		uint32_t serial)
{
	Globals *globals = data;

	xdg_surface_ack_configure(xdg_surface, serial);
	globals->configured = true;
	if (globals->mapped)
		return;
	wl_surface_attach(globals->surface, globals->buffer, 0, 0);
	wl_surface_damage(globals->surface, 0, 0, 32, 32);
	wl_surface_commit(globals->surface);
	globals->mapped = true;
}


static void
toplevel_configure(void *data, struct xdg_toplevel *toplevel,
		int32_t width, int32_t height, struct wl_array *states)
{
	(void)data;
	(void)toplevel;
	(void)width;
	(void)height;
	(void)states;
}


static void
toplevel_close(void *data, struct xdg_toplevel *toplevel)
{
	Globals *globals = data;

	(void)toplevel;
	globals->closed = true;
}


static const struct xdg_wm_base_listener wm_base_listener = {
	.ping = wm_base_ping,
};

static const struct wl_shm_listener shm_listener = {
	.format = shm_format,
};

static const struct xdg_surface_listener xdg_surface_listener = {
	.configure = xdg_surface_configure,
};

static const struct xdg_toplevel_listener toplevel_listener = {
	.configure = toplevel_configure,
	.close = toplevel_close,
};


static void
global(void *data, struct wl_registry *registry, uint32_t name,
		const char *interface, uint32_t version)
{
	Globals *globals = data;

	(void)version;
	if (!strcmp(interface, "wl_compositor"))
		globals->wl_compositor = wl_registry_bind(registry, name,
				&wl_compositor_interface, 1);
	else if (!strcmp(interface, "wl_shm"))
		globals->wl_shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	else if (!strcmp(interface, "wl_output"))
		globals->output = true;
	else if (!strcmp(interface, "wl_seat"))
		globals->seat = true;
	else if (!strcmp(interface, "xdg_wm_base"))
		globals->xdg_wm_base = wl_registry_bind(registry, name,
				&xdg_wm_base_interface, 1);
	else if (!strcmp(interface, "zwlr_layer_shell_v1"))
		globals->layer_shell = true;
	globals->compositor = globals->wl_compositor != NULL;
	globals->xdg_shell = globals->xdg_wm_base != NULL;
}


static void
global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
	(void)data;
	(void)registry;
	(void)name;
}


static void
destroy_toplevel(Globals *globals)
{
	if (globals->toplevel) {
		xdg_toplevel_destroy(globals->toplevel);
		globals->toplevel = NULL;
	}
	if (globals->xdg_surface) {
		xdg_surface_destroy(globals->xdg_surface);
		globals->xdg_surface = NULL;
	}
	if (globals->surface) {
		wl_surface_destroy(globals->surface);
		globals->surface = NULL;
	}
}


static void
destroy_globals(Globals *globals)
{
	destroy_toplevel(globals);
	if (globals->buffer)
		wl_buffer_destroy(globals->buffer);
	if (globals->pool)
		wl_shm_pool_destroy(globals->pool);
	if (globals->xdg_wm_base)
		xdg_wm_base_destroy(globals->xdg_wm_base);
	if (globals->wl_shm)
		wl_shm_destroy(globals->wl_shm);
	if (globals->wl_compositor)
		wl_compositor_destroy(globals->wl_compositor);
}


int
main(void)
{
	static const struct wl_registry_listener listener = {
		.global = global,
		.global_remove = global_remove,
	};
	Globals globals = {0};
	struct wl_display *display;
	struct wl_registry *registry;
	int status = 1;

	display = wl_display_connect(NULL);
	if (!display)
		return 1;
	registry = wl_display_get_registry(display);
	if (wl_registry_add_listener(registry, &listener, &globals) < 0
			|| wl_display_roundtrip(display) < 0)
		goto done;
	if (!globals.compositor || !globals.layer_shell || !globals.output
			|| !globals.seat || !globals.xdg_shell || !globals.wl_shm) {
		fprintf(stderr, "missing required Wayland globals\n");
		goto done;
	}
	if (xdg_wm_base_add_listener(globals.xdg_wm_base, &wm_base_listener,
				&globals) < 0
			|| wl_shm_add_listener(globals.wl_shm, &shm_listener, &globals) < 0
			|| wl_display_roundtrip(display) < 0 || !globals.argb8888
			|| create_buffer(&globals) < 0)
		goto done;
	globals.surface = wl_compositor_create_surface(globals.wl_compositor);
	globals.xdg_surface = globals.surface
		? xdg_wm_base_get_xdg_surface(globals.xdg_wm_base, globals.surface) : NULL;
	if (!globals.xdg_surface
			|| xdg_surface_add_listener(globals.xdg_surface,
					&xdg_surface_listener, &globals) < 0)
		goto done;
	globals.toplevel = xdg_surface_get_toplevel(globals.xdg_surface);
	if (!globals.toplevel
			|| xdg_toplevel_add_listener(globals.toplevel, &toplevel_listener,
					&globals) < 0)
		goto done;
	xdg_toplevel_set_title(globals.toplevel, "Inca smoke");
	wl_surface_commit(globals.surface);
	if (wl_display_roundtrip(display) < 0 || wl_display_roundtrip(display) < 0
			|| !globals.configured || !globals.mapped || globals.closed)
		goto done;
	destroy_toplevel(&globals);
	if (wl_display_roundtrip(display) < 0)
		goto done;
	status = 0;

done:
	destroy_globals(&globals);
	wl_display_disconnect(display);
	return status;
}
