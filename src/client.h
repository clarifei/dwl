/* xdg-shell client helpers shared by the compositor modules. */
#ifndef INCA_CLIENT_H
#define INCA_CLIENT_H

static inline struct wlr_surface *
client_surface(Client *client)
{
	return client->surface->surface;
}

static inline int
toplevel_from_wlr_surface(struct wlr_surface *surface, Client **client_out,
		LayerSurface **layer_out)
{
	struct wlr_layer_surface_v1 *layer_surface;
	struct wlr_surface *parent;
	struct wlr_surface *root;
	struct wlr_xdg_surface *xdg_surface;
	Client *client;
	LayerSurface *layer;

	if (client_out)
		*client_out = NULL;
	if (layer_out)
		*layer_out = NULL;
	if (!surface)
		return -1;

	root = wlr_surface_get_root_surface(surface);
	if ((layer_surface = wlr_layer_surface_v1_try_from_wlr_surface(root))) {
		layer = layer_surface->data;
		if (!layer)
			return -1;
		if (layer_out)
			*layer_out = layer;
		return SceneLayer;
	}

	xdg_surface = wlr_xdg_surface_try_from_wlr_surface(root);
	while (xdg_surface && xdg_surface->role == WLR_XDG_SURFACE_ROLE_POPUP) {
		if (!xdg_surface->popup || !xdg_surface->popup->parent)
			return -1;
		parent = xdg_surface->popup->parent;
		xdg_surface = wlr_xdg_surface_try_from_wlr_surface(parent);
		if (!xdg_surface)
			return toplevel_from_wlr_surface(parent, client_out, layer_out);
	}
	if (!xdg_surface || xdg_surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL)
		return -1;

	client = xdg_surface->data;
	if (!client)
		return -1;
	if (client_out)
		*client_out = client;
	return SceneClient;
}

static inline void
client_activate_surface(struct wlr_surface *surface, int activated)
{
	struct wlr_xdg_toplevel *toplevel;

	if ((toplevel = wlr_xdg_toplevel_try_from_wlr_surface(surface)))
		wlr_xdg_toplevel_set_activated(toplevel, activated);
}

static inline uint32_t
client_set_bounds(Client *client, int32_t width, int32_t height)
{
	if (wl_resource_get_version(client->surface->toplevel->resource) >=
			XDG_TOPLEVEL_CONFIGURE_BOUNDS_SINCE_VERSION && width >= 0 && height >= 0
			&& (client->bounds.width != width || client->bounds.height != height)) {
		client->bounds.width = width;
		client->bounds.height = height;
		return wlr_xdg_toplevel_set_bounds(client->surface->toplevel, width, height);
	}
	return 0;
}

static inline const char *
client_get_appid(Client *client)
{
	return client->surface->toplevel->app_id ? client->surface->toplevel->app_id
		: "unknown";
}

static inline void
client_get_clip(Client *client, struct wlr_box *clip)
{
	*clip = (struct wlr_box){
		.x = client->surface->geometry.x,
		.y = client->surface->geometry.y,
		.width = client->geom.width - client->bw,
		.height = client->geom.height - client->bw,
	};
}

static inline void
client_get_geometry(Client *client, struct wlr_box *geometry)
{
	*geometry = client->surface->geometry;
}

static inline Client *
client_get_parent(Client *client)
{
	Client *parent = NULL;

	if (client->surface->toplevel->parent)
		toplevel_from_wlr_surface(client->surface->toplevel->parent->base->surface,
			&parent, NULL);
	return parent;
}

static inline int
client_has_children(Client *client)
{
	return wl_list_length(&client->surface->link) > 1;
}

static inline const char *
client_get_title(Client *client)
{
	return client->surface->toplevel->title ? client->surface->toplevel->title
		: "unknown";
}

static inline void
client_notify_enter(struct wlr_surface *surface, struct wlr_keyboard *keyboard)
{
	if (keyboard)
		wlr_seat_keyboard_notify_enter(seat, surface, keyboard->keycodes,
			keyboard->num_keycodes, &keyboard->modifiers);
	else
		wlr_seat_keyboard_notify_enter(seat, surface, NULL, 0, NULL);
}

static inline void
client_send_close(Client *client)
{
	wlr_xdg_toplevel_send_close(client->surface->toplevel);
}

static inline void
client_set_border_color(Client *client, const float color[static 4])
{
	if (client->border)
		wlr_scene_rect_set_color(client->border, color);
}

static inline void
client_set_fullscreen(Client *client, int fullscreen)
{
	wlr_xdg_toplevel_set_fullscreen(client->surface->toplevel, fullscreen);
}

static inline void
client_set_scale(struct wlr_surface *surface, double scale)
{
	wlr_fractional_scale_v1_notify_scale(surface, scale);
	wlr_surface_set_preferred_buffer_scale(surface, (int32_t)ceil(scale));
}

static inline uint32_t
client_set_size(Client *client, uint32_t width, uint32_t height)
{
	if ((int32_t)width == client->surface->toplevel->current.width
		&& (int32_t)height == client->surface->toplevel->current.height)
		return 0;
	return wlr_xdg_toplevel_set_size(client->surface->toplevel, (int32_t)width,
		(int32_t)height);
}

static inline void
client_set_suspended(Client *client, int suspended)
{
	wlr_xdg_toplevel_set_suspended(client->surface->toplevel, suspended);
}

static inline int
client_wants_fullscreen(Client *client)
{
	return client->surface->toplevel->requested.fullscreen;
}

#endif
