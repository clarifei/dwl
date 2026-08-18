/* See LICENSE file for copyright and license details. */
/* Client lifecycle, focus, rules, geometry, and decoration handling. */

#include "inca.h"

typedef struct {
	struct wl_listener commit;
	struct wl_listener destroy;
} PopupListener;

typedef struct {
	struct wlr_buffer base;
	cairo_surface_t *surface;
} CollapsedBuffer;

typedef struct {
	Client *client;
	double scale;
} ClientEffectUpdate;

static void
clientbordergeometry(Client *c, double scale)
{
	int inner_width, inner_height, radius;
	enum corner_location corners;

	if (!c->border)
		return;
	inner_width = MAX(0, (int)round((c->geom.width - 2 * (int)c->bw) * scale));
	inner_height = MAX(0, (int)round((c->geom.height - 2 * (int)c->bw) * scale));
	radius = c->isfullscreen ? 0 : MIN((int)round(config.corner_radius * scale),
			MIN(inner_width, inner_height) / 2);
	corners = radius ? CORNER_LOCATION_ALL : CORNER_LOCATION_NONE;
	struct clipped_region clip = {
		.area = {
			(int)round(c->bw * scale), (int)round(c->bw * scale),
			inner_width, inner_height,
		},
		.corner_radius = radius,
		.corners = corners,
	};
	int width = MAX(1, (int)round(c->geom.width * scale));
	int height = MAX(1, (int)round(c->geom.height * scale));
	int border_radius = radius + (int)round(c->bw * scale);

	if (c->border->node.enabled != (c->bw > 0))
		wlr_scene_node_set_enabled(&c->border->node, c->bw > 0);
	if (c->border->width != width || c->border->height != height)
		wlr_scene_rect_set_size(c->border, width, height);
	if (c->border->corner_radius != border_radius
			|| c->border->corners != corners)
		wlr_scene_rect_set_corner_radius(c->border, border_radius, corners);
	if (memcmp(&c->border->clipped_region, &clip, sizeof(clip)))
		wlr_scene_rect_set_clipped_region(c->border, clip);
}

void
clientshadowgeometry(Client *c, double scale)
{
	float sigma;
	int height, offset_x, offset_y, padding, radius, width;

	if (!c || !c->shadow)
		return;
	width = MAX(1, (int)round(c->geom.width * scale));
	height = MAX(1, (int)round(c->geom.height * scale));
	sigma = MAX(0.0f, (float)(config.shadow_sigma * scale));
	padding = (int)ceil(sigma);
	offset_x = (int)round(config.shadow_offset_x * scale);
	offset_y = (int)round(config.shadow_offset_y * scale);
	radius = MAX(0, (int)round((config.corner_radius + c->bw) * scale));
	struct clipped_region clip = {
		.area = {padding - offset_x, padding - offset_y, width, height},
		.corner_radius = radius,
		.corners = radius ? CORNER_LOCATION_ALL : CORNER_LOCATION_NONE,
	};
	int x = offset_x - padding;
	int y = offset_y - padding;
	int shadow_width = width + 2 * padding;
	int shadow_height = height + 2 * padding;

	if (c->shadow->node.x != x || c->shadow->node.y != y)
		wlr_scene_node_set_position(&c->shadow->node, x, y);
	if (c->shadow->width != shadow_width || c->shadow->height != shadow_height)
		wlr_scene_shadow_set_size(c->shadow, shadow_width, shadow_height);
	if (c->shadow->corner_radius != radius)
		wlr_scene_shadow_set_corner_radius(c->shadow, radius);
	if (c->shadow->blur_sigma != sigma)
		wlr_scene_shadow_set_blur_sigma(c->shadow, sigma);
	if (memcmp(&c->shadow->clipped_region, &clip, sizeof(clip)))
		wlr_scene_shadow_set_clipped_region(c->shadow, clip);
}

static enum corner_location
clientbuffercorners(Client *c, struct wlr_scene_buffer *buffer, double scale)
{
	struct wlr_scene_surface *scene_surface;
	int content_x, content_y, content_width, content_height;
	int buffer_x, buffer_y, buffer_width, buffer_height;
	enum corner_location corners = CORNER_LOCATION_NONE;

	scene_surface = buffer ? wlr_scene_surface_try_from_buffer(buffer) : NULL;
	if (!c || !c->scene_surface || !scene_surface
			|| wlr_surface_get_root_surface(scene_surface->surface)
					!= client_surface(c)
			|| !wlr_scene_node_coords(&c->scene_surface->node,
					&content_x, &content_y)
			|| !wlr_scene_node_coords(&buffer->node, &buffer_x, &buffer_y))
		return CORNER_LOCATION_NONE;
	content_width = MAX(0, (int)round((c->geom.width - 2 * (int)c->bw) * scale));
	content_height = MAX(0, (int)round((c->geom.height - 2 * (int)c->bw) * scale));
	buffer_width = buffer->dst_width;
	buffer_height = buffer->dst_height;
	if (!buffer_width && buffer->buffer)
		buffer_width = buffer->buffer->width;
	if (!buffer_height && buffer->buffer)
		buffer_height = buffer->buffer->height;
	if (content_width <= 0 || content_height <= 0
			|| buffer_width <= 0 || buffer_height <= 0)
		return CORNER_LOCATION_NONE;
	if (buffer_x == content_x && buffer_y == content_y)
		corners |= CORNER_LOCATION_TOP_LEFT;
	if (buffer_x + buffer_width == content_x + content_width
			&& buffer_y == content_y)
		corners |= CORNER_LOCATION_TOP_RIGHT;
	if (buffer_x + buffer_width == content_x + content_width
			&& buffer_y + buffer_height == content_y + content_height)
		corners |= CORNER_LOCATION_BOTTOM_RIGHT;
	if (buffer_x == content_x
			&& buffer_y + buffer_height == content_y + content_height)
		corners |= CORNER_LOCATION_BOTTOM_LEFT;
	return corners;
}

void
clientbufferfxupdate(Client *c, struct wlr_scene_buffer *buffer, double scale)
{
	float opacity;
	int buffer_height, buffer_width, enabled, focused, radius;
	enum corner_location corners;

	if (!c || !buffer)
		return;
	enabled = !c->isfullscreen;
	focused = c->mon && focustop(c->mon) == c;
	radius = enabled ? MIN((int)round(config.corner_radius * scale),
			MIN(MAX(0, (int)round((c->geom.width - 2 * (int)c->bw) * scale)),
					MAX(0, (int)round((c->geom.height - 2 * (int)c->bw) * scale))) / 2) : 0;
	buffer_width = buffer->dst_width ? buffer->dst_width
		: buffer->buffer ? buffer->buffer->width : 0;
	buffer_height = buffer->dst_height ? buffer->dst_height
		: buffer->buffer ? buffer->buffer->height : 0;
	corners = radius ? clientbuffercorners(c, buffer, scale) : CORNER_LOCATION_NONE;
	if (corners)
		radius = MIN(radius, MIN(buffer_width, buffer_height) / 2);
	else
		radius = 0;
	opacity = config.opacity_enabled && enabled
			? (focused ? config.opacity_active : config.opacity_inactive) : 1.0f;
	if (buffer->opacity != opacity)
		wlr_scene_buffer_set_opacity(buffer, opacity);
	if (buffer->corner_radius != radius
			|| buffer->corners != corners)
		wlr_scene_buffer_set_corner_radius(buffer, radius,
				corners);
	if (buffer->backdrop_blur != (enabled && config.blur_enabled))
		wlr_scene_buffer_set_backdrop_blur(buffer,
				enabled && config.blur_enabled);
	if (buffer->backdrop_blur_optimized)
		wlr_scene_buffer_set_backdrop_blur_optimized(buffer, false);
	if (buffer->backdrop_blur_ignore_transparent != config.blur_ignore_transparent)
		wlr_scene_buffer_set_backdrop_blur_ignore_transparent(buffer,
				config.blur_ignore_transparent);
}

static void
clienteffectbuffer(struct wlr_scene_buffer *buffer, int sx, int sy, void *data)
{
	ClientEffectUpdate *update = data;
	struct wlr_scene_surface *surface = wlr_scene_surface_try_from_buffer(buffer);
	Client *client = NULL;

	if (surface
			&& toplevel_from_wlr_surface(surface->surface, &client, NULL)
				== SceneClient && client == update->client)
		clientbufferfxupdate(update->client, buffer, update->scale);
}

void
clienteffectsupdate(Client *c)
{
	ClientEffectUpdate update;
	double scale;
	int enabled;

	if (!c || !c->scene)
		return;
	enabled = !c->isfullscreen;
	scale = clientcanvasscale(c);
	update = (ClientEffectUpdate){.client = c, .scale = scale};
	wlr_scene_node_for_each_buffer(&c->scene->node,
			clienteffectbuffer, &update);
	clientbordergeometry(c, scale);
	if (!config.shadow_enabled && c->shadow) {
		wlr_scene_node_destroy(&c->shadow->node);
		c->shadow = NULL;
	} else if (enabled && config.shadow_enabled && !c->shadow) {
		c->shadow = wlr_scene_shadow_create(c->scene, 0, 0,
				config.corner_radius, config.shadow_sigma, config.shadow_color);
		if (c->shadow)
			wlr_scene_node_lower_to_bottom(&c->shadow->node);
	}
	if (c->shadow) {
		if (memcmp(c->shadow->color, config.shadow_color,
				sizeof(c->shadow->color)))
			wlr_scene_shadow_set_color(c->shadow, config.shadow_color);
		if (c->shadow->node.enabled != enabled)
			wlr_scene_node_set_enabled(&c->shadow->node, enabled);
		clientshadowgeometry(c, scale);
	}
}

static void
collapsedbufferdestroy(struct wlr_buffer *wlr_buffer)
{
	CollapsedBuffer *buffer = wl_container_of(wlr_buffer, buffer, base);

	wlr_buffer_finish(wlr_buffer);
	cairo_surface_destroy(buffer->surface);
	free(buffer);
}

static bool
collapsedbufferbegin(struct wlr_buffer *wlr_buffer, uint32_t flags,
		void **data, uint32_t *format, size_t *stride)
{
	CollapsedBuffer *buffer = wl_container_of(wlr_buffer, buffer, base);

	if (flags & WLR_BUFFER_DATA_PTR_ACCESS_WRITE)
		return false;
	cairo_surface_flush(buffer->surface);
	*data = cairo_image_surface_get_data(buffer->surface);
	*format = DRM_FORMAT_ARGB8888;
	*stride = (size_t)cairo_image_surface_get_stride(buffer->surface);
	return true;
}

static void
collapsedbufferend(struct wlr_buffer *wlr_buffer)
{
}

static const struct wlr_buffer_impl collapsed_buffer_impl = {
	.destroy = collapsedbufferdestroy,
	.begin_data_ptr_access = collapsedbufferbegin,
	.end_data_ptr_access = collapsedbufferend,
};

static struct wlr_buffer *
collapsedbuffercreate(Client *c, int width, int height)
{
	CollapsedBuffer *buffer;
	cairo_t *cr;
	const char *appid = client_get_appid(c);
	const char *title = client_get_title(c);
	double detail_baseline, title_baseline;

	buffer = ecalloc(1, sizeof(*buffer));
	buffer->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
			width, height);
	if (cairo_surface_status(buffer->surface) != CAIRO_STATUS_SUCCESS)
		die("collapsed label: cairo surface");
	cr = cairo_create(buffer->surface);
	if (cairo_status(cr) != CAIRO_STATUS_SUCCESS)
		die("collapsed label: cairo context");
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_rgba(cr, 0, 0, 0, 0);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	cairo_rectangle(cr, 16, 0, MAX(1, width - 32), height);
	cairo_clip(cr);
	cairo_select_font_face(cr, "Noto Sans", CAIRO_FONT_SLANT_NORMAL,
			CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(cr, config.collapsed_font_size);
	cairo_set_source_rgba(cr, config.collapsed_title_color[0],
			config.collapsed_title_color[1], config.collapsed_title_color[2],
			config.collapsed_title_color[3]);
	title_baseline = height / 2.0 - 3.0;
	cairo_move_to(cr, 16, title_baseline);
	cairo_show_text(cr, title);
	cairo_set_font_size(cr, MAX(8, config.collapsed_font_size - 3));
	cairo_set_source_rgba(cr, config.collapsed_detail_color[0],
			config.collapsed_detail_color[1], config.collapsed_detail_color[2],
			config.collapsed_detail_color[3]);
	detail_baseline = height / 2.0 + config.collapsed_font_size + 4.0;
	cairo_move_to(cr, 16, detail_baseline);
	cairo_show_text(cr, "Minimized | ");
	cairo_show_text(cr, appid);
	cairo_destroy(cr);
	cairo_surface_flush(buffer->surface);
	wlr_buffer_init(&buffer->base, &collapsed_buffer_impl, width, height);
	return &buffer->base;
}

void
clientcollapsedupdate(Client *c, int redraw)
{
	struct wlr_buffer *buffer;
	float scrim[4];
	enum corner_location corners;
	int width, height, label_width, label_height, radius, x, y;

	if (!c || !c->scene || !c->scene_surface)
		return;
	if (!c->iscollapsed) {
		if (c->collapsed_label)
			wlr_scene_node_destroy(&c->collapsed_label->node);
		if (c->collapsed_scrim)
			wlr_scene_node_destroy(&c->collapsed_scrim->node);
		c->collapsed_label = NULL;
		c->collapsed_scrim = NULL;
		c->collapsed_label_width = c->collapsed_label_height = 0;
		return;
	}
	if (!c->collapsed_scrim) {
		c->collapsed_scrim = wlr_scene_rect_create(c->scene, 1, 1,
				config.collapsed_scrim);
		c->collapsed_scrim->node.data = c;
		c->collapsed_scrim->accepts_input = true;
	}
	scrim[0] = config.collapsed_scrim[0] * config.collapsed_scrim[3];
	scrim[1] = config.collapsed_scrim[1] * config.collapsed_scrim[3];
	scrim[2] = config.collapsed_scrim[2] * config.collapsed_scrim[3];
	scrim[3] = config.collapsed_scrim[3];
	width = MAX(1, c->geom.width - 2 * (int)c->bw);
	height = MAX(1, c->geom.height - 2 * (int)c->bw);
	radius = c->isfullscreen ? 0 : MIN(config.corner_radius,
			MIN(width, height) / 2);
	corners = radius ? CORNER_LOCATION_ALL : CORNER_LOCATION_NONE;
	wlr_scene_rect_set_size(c->collapsed_scrim, width, height);
	wlr_scene_rect_set_color(c->collapsed_scrim, scrim);
	wlr_scene_rect_set_corner_radius(c->collapsed_scrim, radius, corners);
	wlr_scene_rect_set_clipped_region(c->collapsed_scrim, (struct clipped_region){
		.area = {0, 0, width, height},
		.corner_radius = radius,
		.corners = corners,
	});
	wlr_scene_rect_set_backdrop_blur(c->collapsed_scrim, config.blur_enabled);
	wlr_scene_rect_set_backdrop_blur_optimized(c->collapsed_scrim, false);
	wlr_scene_node_set_position(&c->collapsed_scrim->node, c->bw, c->bw);
	label_width = MIN(width, 640);
	label_height = MIN(height, 2 * config.collapsed_font_size + 24);
	if (c->collapsed_label && (c->collapsed_label_width != label_width
			|| c->collapsed_label_height != label_height))
		redraw = 1;
	if (redraw && c->collapsed_label) {
		wlr_scene_node_destroy(&c->collapsed_label->node);
		c->collapsed_label = NULL;
		c->collapsed_label_width = c->collapsed_label_height = 0;
	}
	if (!c->collapsed_label) {
		buffer = collapsedbuffercreate(c, label_width, label_height);
		c->collapsed_label = wlr_scene_buffer_create(c->scene, buffer);
		wlr_buffer_drop(buffer);
		wlr_scene_buffer_set_dest_size(c->collapsed_label,
				label_width, label_height);
		c->collapsed_label->node.data = c;
		c->collapsed_label_width = label_width;
		c->collapsed_label_height = label_height;
	}
	x = c->bw + (width - c->collapsed_label_width) / 2;
	y = c->bw + (height - c->collapsed_label_height) / 2;
	wlr_scene_node_set_position(&c->collapsed_label->node, x, y);
}

static void
destroypopuplistener(struct wl_listener *listener, void *data)
{
	PopupListener *popup_listener = wl_container_of(listener, popup_listener, destroy);

	wl_list_remove(&popup_listener->commit.link);
	wl_list_remove(&popup_listener->destroy.link);
	free(popup_listener);
}

void
applybounds(Client *c, struct wlr_box *bbox)
{
	/* set minimum possible */
	c->geom.width = MAX(1 + 2 * (int)c->bw, c->geom.width);
	c->geom.height = MAX(1 + 2 * (int)c->bw, c->geom.height);

	if (c->geom.x >= bbox->x + bbox->width)
		c->geom.x = bbox->x + bbox->width - c->geom.width;
	if (c->geom.y >= bbox->y + bbox->height)
		c->geom.y = bbox->y + bbox->height - c->geom.height;
	if (c->geom.x + c->geom.width <= bbox->x)
		c->geom.x = bbox->x;
	if (c->geom.y + c->geom.height <= bbox->y)
		c->geom.y = bbox->y;
}

void
applyrules(Client *c)
{
	const char *appid, *title;
	int i;
	const Rule *r;
	Monitor *mon = selmon, *m;

	appid = client_get_appid(c);
	title = client_get_title(c);

	for (r = config.rules; r < config.rules + config.rule_count; r++) {
		if ((!r->title || strstr(title, r->title))
				&& (!r->app_id || strstr(appid, r->app_id))) {
			i = 0;
			wl_list_for_each(m, &mons, link) {
				if (r->monitor == i++)
					mon = m;
			}
		}
	}

	setclientmonitor(c, mon);
}

void
commitnotify(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, commit);

	if (c->surface->initial_commit) {
		/*
		 * Get the monitor this client will be rendered on
		 * Note that if the user set a rule in which the client is placed on
		 * a different monitor based on its title, this will likely select
		 * a wrong monitor.
		 */
		applyrules(c);
		if (c->mon) {
			client_set_scale(client_surface(c), c->mon->wlr_output->scale);
		}
		setclientmonitor(c, NULL); /* Reapply rules after the surface maps. */

		wlr_xdg_toplevel_set_wm_capabilities(c->surface->toplevel,
				WLR_XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN);
		if (c->decoration)
			requestdecorationmode(&c->set_decoration_mode, c->decoration);
		wlr_xdg_toplevel_set_size(c->surface->toplevel, 0, 0);
		return;
	}

	resize(c, c->geom, !c->isfullscreen);

	/* mark a pending resize as completed */
	if (c->resize && c->resize <= c->surface->current.configure_serial)
		c->resize = 0;
}

void
commitpopup(struct wl_listener *listener, void *data)
{
	PopupListener *popup_listener = wl_container_of(listener, popup_listener, commit);
	struct wlr_surface *surface = data;
	struct wlr_xdg_popup *popup = wlr_xdg_popup_try_from_wlr_surface(surface);
	LayerSurface *l = NULL;
	Client *c = NULL;
	struct wlr_box box;
	int type = -1;

	if (!popup->base->initial_commit)
		return;

	type = toplevel_from_wlr_surface(popup->base->surface, &c, &l);
	if (!popup->parent || type < 0)
		return;
	popup->base->surface->data = wlr_scene_xdg_surface_create(
			popup->parent->data, popup->base);
	if ((l && !l->mon) || (c && !c->mon)) {
		wlr_xdg_popup_destroy(popup);
		return;
	}
	if (c && !c->isfullscreen)
		canvasvisiblebox(c->mon, &box);
	else
		box = type == SceneLayer ? l->mon->m : c->mon->w;
	box.x -= type == SceneLayer ? l->scene->node.x : c->geom.x;
	box.y -= type == SceneLayer ? l->scene->node.y : c->geom.y;
	wlr_xdg_popup_unconstrain_from_box(popup, &box);
	if (c)
		clientsceneupdate(c);
	destroypopuplistener(&popup_listener->destroy, NULL);
}

void
createdecoration(struct wl_listener *listener, void *data)
{
	struct wlr_xdg_toplevel_decoration_v1 *deco = data;
	Client *c = deco->toplevel->base->data;
	c->decoration = deco;

	LISTEN(&deco->events.request_mode, &c->set_decoration_mode, requestdecorationmode);
	LISTEN(&deco->events.destroy, &c->destroy_decoration, destroydecoration);

	requestdecorationmode(&c->set_decoration_mode, deco);
}

void
createnotify(struct wl_listener *listener, void *data)
{
	/* This event is raised when a client creates a new toplevel (application window). */
	struct wlr_xdg_toplevel *toplevel = data;
	Client *c = NULL;

	/* Allocate a Client for this surface */
	c = toplevel->base->data = ecalloc(1, sizeof(*c));
	c->type = SceneClient;
	c->surface = toplevel->base;
	c->bw = config.borderpx;

	LISTEN(&toplevel->base->surface->events.commit, &c->commit, commitnotify);
	LISTEN(&toplevel->base->surface->events.map, &c->map, mapnotify);
	LISTEN(&toplevel->base->surface->events.unmap, &c->unmap, unmapnotify);
	LISTEN(&toplevel->events.destroy, &c->destroy, destroynotify);
	LISTEN(&toplevel->events.request_fullscreen, &c->fullscreen, fullscreennotify);
	LISTEN(&toplevel->events.request_maximize, &c->maximize, maximizenotify);
	LISTEN(&toplevel->events.set_title, &c->set_title, updatetitle);
}

void
createpopup(struct wl_listener *listener, void *data)
{
	/* This event is raised when a client (either xdg-shell or layer-shell)
	 * creates a new popup. */
	struct wlr_xdg_popup *popup = data;
	PopupListener *popup_listener = ecalloc(1, sizeof(*popup_listener));

	LISTEN(&popup->base->surface->events.commit, &popup_listener->commit, commitpopup);
	LISTEN(&popup->events.destroy, &popup_listener->destroy, destroypopuplistener);
}

void
destroydecoration(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, destroy_decoration);

	wl_list_remove(&c->destroy_decoration.link);
	wl_list_remove(&c->set_decoration_mode.link);
	c->decoration = NULL;
}

void
destroynotify(struct wl_listener *listener, void *data)
{
	/* Called when the xdg_toplevel is destroyed. */
	Client *c = wl_container_of(listener, c, destroy);
	wl_list_remove(&c->destroy.link);
	wl_list_remove(&c->set_title.link);
	wl_list_remove(&c->fullscreen.link);
	wl_list_remove(&c->commit.link);
	wl_list_remove(&c->map.link);
	wl_list_remove(&c->unmap.link);
	wl_list_remove(&c->maximize.link);
	free(c);
}

void
focusclient(Client *c, int lift)
{
	struct wlr_surface *old = seat->keyboard_state.focused_surface;
	int unused_lx, unused_ly, old_client_type;
	Client *old_c = NULL;
	LayerSurface *old_l = NULL;

	if (locked)
		return;
	/* A collapsed window remains clickable in the scene, but it is never a
	 * keyboard-focus target. */
	if (c && c->iscollapsed)
		c = focustop(c->mon);

	/* Raise client in stacking order if requested */
	if (c && lift)
		wlr_scene_node_raise_to_top(&c->scene->node);

	if (c && client_surface(c) == old)
		return;

	if ((old_client_type = toplevel_from_wlr_surface(old, &old_c, &old_l)) == SceneClient) {
		struct wlr_xdg_popup *popup, *tmp;
		wl_list_for_each_safe(popup, tmp, &old_c->surface->popups, link)
			wlr_xdg_popup_destroy(popup);
	}

	/* Put the new client atop the focus stack and select its monitor */
	if (c) {
		wl_list_remove(&c->flink);
		wl_list_insert(&fstack, &c->flink);
		selmon = c->mon;
		c->isurgent = 0;

		/* Don't change border color if there is an exclusive focus or we are
		 * handling a drag operation */
		if (!exclusive_focus && !seat->drag) {
			client_set_border_color(c, config.focuscolor);
		}
		clienteffectsupdate(c);
	}

	/* Deactivate old client if focus is changing */
	if (old && (!c || client_surface(c) != old)) {
		/* If an overlay is focused, don't focus or activate the client,
		 * but only update its position in fstack to render its border with focuscolor
		 * and focus it after the overlay is closed. */
		if (old_client_type == SceneLayer && wlr_scene_node_coords(
					&old_l->scene->node, &unused_lx, &unused_ly)
				&& old_l->layer_surface->current.layer >= ZWLR_LAYER_SHELL_V1_LAYER_TOP) {
			return;
		} else if (old_c) {
			client_set_border_color(old_c, config.bordercolor);
			clienteffectsupdate(old_c);

			client_activate_surface(old, 0);
		}
	}
	printstatus();

	if (!c) {
		/* With no client, all we have left is to clear focus */
		wlr_seat_keyboard_notify_clear_focus(seat);
		return;
	}
	if (c->iscollapsed) {
		motionnotify(0, NULL, 0, 0, 0, 0);
		wlr_seat_keyboard_notify_clear_focus(seat);
		return;
	}

	/* Change cursor surface */
	motionnotify(0, NULL, 0, 0, 0, 0);

	/* Have a client, so focus its top-level wlr_surface */
	client_notify_enter(client_surface(c), wlr_seat_get_keyboard(seat));

	/* Activate the new client */
	client_activate_surface(client_surface(c), 1);
}

Client *
focustop(Monitor *m)
{
	Client *c;
	if (!m)
		return NULL;
	wl_list_for_each(c, &fstack, flink) {
		if (CLIENTON(c, m) && !c->iscollapsed)
			return c;
	}
	return NULL;
}

void
fullscreennotify(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, fullscreen);
	setfullscreen(c, client_wants_fullscreen(c));
}

void
killclient(const Arg *arg)
{
	Client *sel = focustop(selmon);
	if (sel)
		client_send_close(sel);
}

void
mapnotify(struct wl_listener *listener, void *data)
{
	/* Called when the surface is mapped, or ready to display on-screen. */
	Client *p = NULL;
	Client *w, *c = wl_container_of(listener, c, map);
	CanvasBox spawn;
	Monitor *m;
	double world_x, world_y;
	int spawn_step;

	/* Create scene tree for this client and its border */
	c->scene = client_surface(c)->data = wlr_scene_tree_create(layers[LyrClients]);
	wlr_scene_node_set_enabled(&c->scene->node, 0);
	c->scene_surface = wlr_scene_xdg_surface_create(c->scene, c->surface);
	c->scene->node.data = c->scene_surface->node.data = c;

	client_get_geometry(c, &c->geom);

	c->border = wlr_scene_rect_create(c->scene, 0, 0,
			c->isurgent ? config.urgentcolor : config.bordercolor);
	c->border->node.data = c;
	wlr_scene_node_lower_to_bottom(&c->border->node);
	/* Reserve room for the compositor border. */
	c->geom.width += 2 * c->bw;
	c->geom.height += 2 * c->bw;

	/* Insert this client into client lists. */
	wl_list_insert(&clients, &c->link);
	wl_list_insert(&fstack, &c->flink);

	/* Child toplevels inherit their parent's canvas; roots use configured rules. */
	if ((p = client_get_parent(c))) {
		setclientmonitor(c, p->mon);
	} else {
		applyrules(c);
	}
	if (!p && c->mon) {
		canvaspointtoworld(c->mon,
				c->mon->w.x + c->mon->w.width / 2.0,
				c->mon->w.y + c->mon->w.height / 2.0,
				&world_x, &world_y);
		spawn_step = MIN(160, MAX(48,
				MIN(c->geom.width, c->geom.height) / 8));
		spawn = canvas_spawn_box((CanvasBox){
			.x = canvas_round_coordinate(world_x - c->geom.width / 2.0),
			.y = canvas_round_coordinate(world_y - c->geom.height / 2.0),
			.width = c->geom.width,
			.height = c->geom.height,
		}, c->mon->spawn_serial++, spawn_step);
		resize(c, (struct wlr_box){spawn.x, spawn.y, spawn.width, spawn.height}, 1);
		clientsettle(c);
	}
	if (c->mon)
		arrange(c->mon);
	focusclient(c, 1);
	centercanvas(NULL);
	clienteffectsupdate(c);
	printstatus();

	m = c->mon ? c->mon : xytomon(c->geom.x, c->geom.y);
	wl_list_for_each(w, &clients, link) {
		if (w != c && w != p && w->isfullscreen && m == w->mon)
			setfullscreen(w, 0);
	}
}

void
maximizenotify(struct wl_listener *listener, void *data)
{
	/* This event is raised when a client would like to maximize itself,
	 * typically because the user clicked on the maximize button on
	 * client-side decorations. Inca! doesn't support maximization, but
	 * to conform to xdg-shell protocol we still must send a configure.
	 * Since xdg-shell protocol v5 we should ignore request of unsupported
	 * capabilities, just schedule a empty configure when the client uses <5
	 * protocol version
	 * wlr_xdg_surface_schedule_configure() is used to send an empty reply. */
	Client *c = wl_container_of(listener, c, maximize);
	if (c->surface->initialized
			&& wl_resource_get_version(c->surface->toplevel->resource)
					< XDG_TOPLEVEL_WM_CAPABILITIES_SINCE_VERSION)
		wlr_xdg_surface_schedule_configure(c->surface);
}

void
requestdecorationmode(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, set_decoration_mode);
	if (c->surface->initialized)
		wlr_xdg_toplevel_decoration_v1_set_mode(c->decoration,
				WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

void
resize(Client *c, struct wlr_box geo, int interact)
{
	struct wlr_box *bbox;
	struct wlr_box clip;

	if (!c->mon || !client_surface(c)->mapped)
		return;

	bbox = interact ? &sgeom : &c->mon->w;

	client_set_bounds(c, geo.width, geo.height);
	c->geom = geo;
	if (c->isfullscreen)
		applybounds(c, bbox);

	/* Update scene-graph, including borders */
	wlr_scene_node_set_position(&c->scene->node, c->geom.x, c->geom.y);
	wlr_scene_node_set_position(&c->scene_surface->node, c->bw, c->bw);
	/* this is a no-op if size hasn't changed */
	c->resize = client_set_size(c, c->geom.width - 2 * c->bw,
			c->geom.height - 2 * c->bw);
	client_get_clip(c, &clip);
	wlr_scene_subsurface_tree_set_clip(&c->scene_surface->node, &clip);
	if (c->collapsed_scrim)
		clientcollapsedupdate(c, 0);
	clientsceneupdate(c);
}

void
setfullscreen(Client *c, int fullscreen)
{
	if (fullscreen && c->iscollapsed) {
		c->iscollapsed = 0;
		clientcollapsedupdate(c, 0);
		client_set_suspended(c, 0);
	}
	c->isfullscreen = fullscreen;
	if (!c->mon || !client_surface(c)->mapped)
		return;
	c->bw = fullscreen ? 0 : config.borderpx;
	client_set_fullscreen(c, fullscreen);
	wlr_scene_node_reparent(&c->scene->node,
			layers[c->isfullscreen ? LyrFullscreen : LyrClients]);

	if (fullscreen) {
		c->prev = c->geom;
		resize(c, c->mon->m, 0);
	} else {
		/* Restore the canvas position and size selected by the user. */
		resize(c, c->prev, 0);
	}
	arrange(c->mon);
	printstatus();
}

void
setcollapsed(Client *c, int collapsed)
{
	Client *next;
	int was_focused;

	if (!c || c->isfullscreen)
		return;
	was_focused = c->mon && focustop(c->mon) == c;
	c->iscollapsed = collapsed;
	clientcollapsedupdate(c, 1);
	client_set_suspended(c, c->iscollapsed);
	clientsceneupdate(c);
	if (c->iscollapsed) {
		client_activate_surface(client_surface(c), 0);
		next = was_focused ? focustop(c->mon) : NULL;
		focusclient(next, 1);
		if (next)
			centercanvas(NULL);
	} else {
		focusclient(c, 1);
		centercanvas(NULL);
	}
	printstatus();
}

void
togglecollapse(const Arg *arg)
{
	Client *c = focustop(selmon);

	if (c)
		setcollapsed(c, !c->iscollapsed);
}

void
setclientmonitor(Client *c, Monitor *m)
{
	Monitor *oldmon = c->mon;
	double screen_x, screen_y, world_x, world_y;

	if (oldmon == m)
		return;
	if (oldmon && m && !c->isfullscreen) {
		canvaspointtoscreen(oldmon, c->geom.x, c->geom.y,
				&screen_x, &screen_y);
		canvaspointtoworld(m, screen_x - oldmon->m.x + m->m.x,
				screen_y - oldmon->m.y + m->m.y, &world_x, &world_y);
		c->geom.x = (int)round(world_x);
		c->geom.y = (int)round(world_y);
	}
	c->mon = m;

	/* Scene graph sends surface leave/enter events on move and resize */
	if (oldmon)
		arrange(oldmon);
	if (m) {
		client_set_scale(client_surface(c), m->wlr_output->scale);
		resize(c, c->isfullscreen ? m->m : c->geom, 0);
	}
	focusclient(focustop(selmon), 1);
}

void
unmapnotify(struct wl_listener *listener, void *data)
{
	/* Called when the surface is unmapped, and should no longer be shown. */
	Client *c = wl_container_of(listener, c, unmap);
	Client *next;
	Monitor *mon = c->mon;
	int was_focused = mon && focustop(mon) == c;
	if (c == grabc) {
		cursor_mode = CurNormal;
		grabc = NULL;
	}

	wl_list_remove(&c->link);
	setclientmonitor(c, NULL);
	wl_list_remove(&c->flink);
	if (was_focused) {
		next = focustop(mon);
		focusclient(next, 1);
		if (next)
			centercanvas(NULL);
	}

	wlr_scene_node_destroy(&c->scene->node);
	c->border = NULL;
	c->shadow = NULL;
	c->collapsed_scrim = NULL;
	c->collapsed_label = NULL;
	c->collapsed_label_width = c->collapsed_label_height = 0;
	printstatus();
	motionnotify(0, NULL, 0, 0, 0, 0);
}

void
updatetitle(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, set_title);
	if (c->iscollapsed) {
		clientcollapsedupdate(c, 1);
		clientsceneupdate(c);
	}
	if (c == focustop(c->mon))
		printstatus();
}

void
urgent(struct wl_listener *listener, void *data)
{
	struct wlr_xdg_activation_v1_request_activate_event *event = data;
	Client *c = NULL;
	toplevel_from_wlr_surface(event->surface, &c, NULL);
	if (!c || c == focustop(selmon))
		return;

	c->isurgent = 1;
	printstatus();

	if (client_surface(c)->mapped)
		client_set_border_color(c, config.urgentcolor);
}
