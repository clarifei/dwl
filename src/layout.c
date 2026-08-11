/* See LICENSE file for copyright and license details. */
/* Layout algorithms, tag operations, and layout-related commands. */

typedef struct {
	struct wlr_addon addon;
	struct wl_listener commit;
	struct wlr_scene_buffer *buffer;
	struct wlr_surface *surface;
	int x, y;
	int scaled_x, scaled_y;
	int width, height;
	int scaled_width, scaled_height;
	double scale;
	wlr_scene_buffer_point_accepts_input_func_t point_accepts_input;
} CanvasNodeState;

static const struct wlr_addon_interface canvas_node_addon_impl;

static void
canvasnodebufferupdate(CanvasNodeState *state, int committed)
{
	struct wlr_scene_buffer *buffer = state->buffer;

	state->width = canvas_buffer_base_length(state->width,
			buffer->dst_width, state->scaled_width, committed);
	state->height = canvas_buffer_base_length(state->height,
			buffer->dst_height, state->scaled_height, committed);
	state->scaled_width = canvas_scaled_extent(state->x,
			state->width, state->scale);
	state->scaled_height = canvas_scaled_extent(state->y,
			state->height, state->scale);
	wlr_scene_buffer_set_dest_size(buffer,
			state->scaled_width, state->scaled_height);
	wlr_scene_buffer_set_filter_mode(buffer, WLR_SCALE_FILTER_BILINEAR);
}

static void
canvasnodepositionupdate(struct wlr_scene_node *node, CanvasNodeState *state)
{
	int x, y;

	if (node->x != state->scaled_x || node->y != state->scaled_y) {
		state->x = node->x;
		state->y = node->y;
	}
	x = (int)round(state->x * state->scale);
	y = (int)round(state->y * state->scale);
	state->scaled_x = x;
	state->scaled_y = y;
	wlr_scene_node_set_position(node, x, y);
}

static void
canvasnodecommit(struct wl_listener *listener, void *data)
{
	CanvasNodeState *buffer_state = wl_container_of(listener, buffer_state, commit);
	struct wlr_scene_node *node = &buffer_state->buffer->node;
	struct wlr_addon *addon;
	CanvasNodeState *state;

	canvasnodebufferupdate(buffer_state, 1);
	while ((addon = wlr_addon_find(&node->addons,
				node, &canvas_node_addon_impl))) {
		state = wl_container_of(addon, state, addon);
		canvasnodepositionupdate(node, state);
		if (!node->parent)
			break;
		node = &node->parent->node;
	}
}

static void
canvasnodestatedestroy(struct wlr_addon *addon)
{
	CanvasNodeState *state = wl_container_of(addon, state, addon);

	if (state->surface)
		wl_list_remove(&state->commit.link);
	wlr_addon_finish(&state->addon);
	free(state);
}

static const struct wlr_addon_interface canvas_node_addon_impl = {
	.name = "dwl_canvas_node",
	.destroy = canvasnodestatedestroy,
};

static bool
canvaspointacceptsinput(struct wlr_scene_buffer *buffer, double *sx, double *sy)
{
	struct wlr_addon *addon = wlr_addon_find(&buffer->node.addons,
			&buffer->node, &canvas_node_addon_impl);
	CanvasNodeState *state;

	if (!addon)
		return false;
	state = wl_container_of(addon, state, addon);
	*sx /= state->scale;
	*sy /= state->scale;
	return state->point_accepts_input(buffer, sx, sy);
}

static CanvasNodeState *
canvasnodestate(struct wlr_scene_node *node)
{
	struct wlr_addon *addon = wlr_addon_find(&node->addons,
			node, &canvas_node_addon_impl);
	CanvasNodeState *state;

	if (addon) {
		state = wl_container_of(addon, state, addon);
		return state;
	}

	state = ecalloc(1, sizeof(*state));
	state->x = state->scaled_x = node->x;
	state->y = state->scaled_y = node->y;
	state->scale = 1.0;
	wlr_addon_init(&state->addon, &node->addons, node,
			&canvas_node_addon_impl);

	if (node->type == WLR_SCENE_NODE_RECT) {
		struct wlr_scene_rect *rect = wlr_scene_rect_from_node(node);
		state->width = state->scaled_width = rect->width;
		state->height = state->scaled_height = rect->height;
	} else if (node->type == WLR_SCENE_NODE_BUFFER) {
		struct wlr_scene_buffer *buffer = wlr_scene_buffer_from_node(node);
		struct wlr_scene_surface *surface = wlr_scene_surface_try_from_buffer(buffer);

		state->buffer = buffer;
		state->width = state->scaled_width = buffer->dst_width;
		state->height = state->scaled_height = buffer->dst_height;
		if ((!state->width || !state->height) && surface) {
			state->width = state->scaled_width = surface->surface->current.width;
			state->height = state->scaled_height = surface->surface->current.height;
		}
		if (surface) {
			state->surface = surface->surface;
			state->commit.notify = canvasnodecommit;
			wl_signal_add(&state->surface->events.commit, &state->commit);
		}
		state->point_accepts_input = buffer->point_accepts_input;
		if (state->point_accepts_input)
			buffer->point_accepts_input = canvaspointacceptsinput;
	}
	return state;
}

static void
canvasnodescale(struct wlr_scene_node *node, double scale)
{
	CanvasNodeState *state = canvasnodestate(node);

	state->scale = scale;
	canvasnodepositionupdate(node, state);

	if (node->type == WLR_SCENE_NODE_RECT) {
		struct wlr_scene_rect *rect = wlr_scene_rect_from_node(node);

		if (rect->width != state->scaled_width
				|| rect->height != state->scaled_height) {
			state->width = rect->width;
			state->height = rect->height;
		}
		state->scaled_width = canvas_scaled_extent(state->x,
				state->width, scale);
		state->scaled_height = canvas_scaled_extent(state->y,
				state->height, scale);
		wlr_scene_rect_set_size(rect,
				state->scaled_width, state->scaled_height);
	} else if (node->type == WLR_SCENE_NODE_BUFFER) {
		canvasnodebufferupdate(state, 0);
	} else {
		struct wlr_scene_tree *tree = wlr_scene_tree_from_node(node);
		struct wlr_scene_node *child;

		wl_list_for_each(child, &tree->children, link)
			canvasnodescale(child, scale);
	}
}

double
clientcanvasscale(Client *c)
{
	if (!c || !c->mon || !ISCANVAS(c->mon) || c->isfullscreen
			|| client_is_unmanaged(c))
		return 1.0;
	return c->mon->canvas_zoom > 0.0 ? c->mon->canvas_zoom : 1.0;
}

void
canvaspointtoscreen(Monitor *m, double x, double y,
		double *screen_x, double *screen_y)
{
	double zoom;

	if (!m || !ISCANVAS(m)) {
		if (screen_x)
			*screen_x = x;
		if (screen_y)
			*screen_y = y;
		return;
	}
	zoom = m->canvas_zoom > 0.0 ? m->canvas_zoom : 1.0;

	if (screen_x)
		*screen_x = canvas_world_to_screen(x, m->m.x, m->canvas_x, zoom);
	if (screen_y)
		*screen_y = canvas_world_to_screen(y, m->m.y, m->canvas_y, zoom);
}

void
canvaspointtoworld(Monitor *m, double x, double y,
		double *world_x, double *world_y)
{
	double zoom;

	if (!m || !ISCANVAS(m)) {
		if (world_x)
			*world_x = x;
		if (world_y)
			*world_y = y;
		return;
	}
	zoom = m->canvas_zoom > 0.0 ? m->canvas_zoom : 1.0;
	if (world_x)
		*world_x = canvas_screen_to_world(x, m->m.x, m->canvas_x, zoom);
	if (world_y)
		*world_y = canvas_screen_to_world(y, m->m.y, m->canvas_y, zoom);
}

void
canvasvisiblebox(Monitor *m, struct wlr_box *box)
{
	double left, top, right, bottom;

	canvaspointtoworld(m, m->w.x, m->w.y, &left, &top);
	canvaspointtoworld(m, m->w.x + m->w.width,
			m->w.y + m->w.height, &right, &bottom);
	box->x = (int)floor(left);
	box->y = (int)floor(top);
	box->width = (int)ceil(right) - box->x;
	box->height = (int)ceil(bottom) - box->y;
}

void
clientsceneposition(Client *c)
{
	double x, y;

	if (!c || !c->scene)
		return;
	if (c->mon && ISCANVAS(c->mon) && !c->isfullscreen
			&& !client_is_unmanaged(c)) {
		canvaspointtoscreen(c->mon, c->geom.x, c->geom.y, &x, &y);
	} else {
		x = c->geom.x;
		y = c->geom.y;
	}
	wlr_scene_node_set_position(&c->scene->node,
			(int)round(x), (int)round(y));
}

void
clientsceneupdate(Client *c)
{
	struct wlr_scene_node *node;
	double scale;

	clientsceneposition(c);
	if (!c || !c->scene)
		return;
	if (client_is_unmanaged(c))
		return;
	scale = clientcanvasscale(c);
	wl_list_for_each(node, &c->scene->children, link)
		canvasnodescale(node, scale);
}

void
updatecanvas(Monitor *m, int rescale)
{
	Client *c;

	if (!m)
		return;
	wl_list_for_each(c, &clients, link) {
		if (c->mon != m)
			continue;
		if (rescale)
			clientsceneupdate(c);
		else
			clientsceneposition(c);
	}
}

void
arrange(Monitor *m)
{
	Client *c;

	if (!m->wlr_output->enabled)
		return;

	wl_list_for_each(c, &clients, link) {
		if (c->mon == m) {
			wlr_scene_node_set_enabled(&c->scene->node, VISIBLEON(c, m));
			client_set_suspended(c, !VISIBLEON(c, m));
		}
	}

	wlr_scene_node_set_enabled(&m->fullscreen_bg->node,
			(c = focustop(m)) && c->isfullscreen);

	strncpy(m->ltsymbol, m->lt[m->sellt]->symbol, LENGTH(m->ltsymbol));

	/* We move all clients (except fullscreen and unmanaged) to LyrTile while
	 * in floating layout to avoid "real" floating clients be always on top */
	wl_list_for_each(c, &clients, link) {
		if (c->mon != m || c->scene->node.parent == layers[LyrFS])
			continue;

		wlr_scene_node_reparent(&c->scene->node,
				(!m->lt[m->sellt]->arrange && c->isfloating)
						? layers[LyrTile]
						: (m->lt[m->sellt]->arrange && c->isfloating)
								? layers[LyrFloat]
								: c->scene->node.parent);
	}

	if (m->lt[m->sellt]->arrange)
		m->lt[m->sellt]->arrange(m);
	updatecanvas(m, 1);
	motionnotify(0, NULL, 0, 0, 0, 0);
	checkidleinhibitor(NULL);
}

void
pancanvas(Monitor *m, double dx, double dy)
{
	Client *focused;

	if (!ISCANVAS(m) || !m->wlr_output->enabled)
		return;
	if ((focused = focustop(m)) && focused->isfullscreen)
		return;

	if (!dx && !dy)
		return;
	m->canvas_zoom_target = m->canvas_zoom;
	m->canvas_x += dx;
	m->canvas_y += dy;
	updatecanvas(m, 0);
}

void
homecanvas(const Arg *arg)
{
	Client *focused;

	if (!ISCANVAS(selmon) || ((focused = focustop(selmon)) && focused->isfullscreen))
		return;
	selmon->canvas_x = 0.0;
	selmon->canvas_y = 0.0;
	selmon->canvas_zoom = 1.0;
	selmon->canvas_zoom_target = 1.0;
	updatecanvas(selmon, 1);
	motionnotify(0, NULL, 0, 0, 0, 0);
}

void
centercanvas(const Arg *arg)
{
	Client *c;
	double center_x, center_y, client_x, client_y, zoom;

	if (!ISCANVAS(selmon) || !(c = focustop(selmon)) || c->isfullscreen)
		return;
	zoom = selmon->canvas_zoom;
	center_x = selmon->w.x + selmon->w.width / 2.0;
	center_y = selmon->w.y + selmon->w.height / 2.0;
	client_x = c->geom.x + c->geom.width / 2.0;
	client_y = c->geom.y + c->geom.height / 2.0;
	selmon->canvas_x = center_x - selmon->m.x
			- (client_x - selmon->m.x) * zoom;
	selmon->canvas_y = center_y - selmon->m.y
			- (client_y - selmon->m.y) * zoom;
	updatecanvas(selmon, 0);
	motionnotify(0, NULL, 0, 0, 0, 0);
}

void
zoomcanvasby(Monitor *m, double factor)
{
	Client *focused;
	double base_zoom, new_zoom;
	int was_active;

	if (!ISCANVAS(m) || !isfinite(factor) || factor <= 0.0
			|| ((focused = focustop(m)) && focused->isfullscreen))
		return;
	was_active = fabs(m->canvas_zoom_target - m->canvas_zoom)
			>= CANVAS_ZOOM_EPSILON;
	base_zoom = was_active ? m->canvas_zoom_target : m->canvas_zoom;
	new_zoom = canvas_clamp_zoom(config.zoom_min, config.zoom_max,
			base_zoom * factor);
	if (new_zoom == m->canvas_zoom_target)
		return;
	if (!was_active)
		clock_gettime(CLOCK_MONOTONIC, &m->canvas_zoom_frame);
	m->canvas_zoom_target = new_zoom;
	wlr_output_schedule_frame(m->wlr_output);
}

void
setcanvaszoom(Monitor *m, double zoom)
{
	double anchor_x, anchor_y, old_zoom;

	if (!m || zoom == m->canvas_zoom)
		return;
	anchor_x = m->w.x + m->w.width / 2.0;
	anchor_y = m->w.y + m->w.height / 2.0;
	old_zoom = m->canvas_zoom;
	m->canvas_x = canvas_zoom_pan(anchor_x, m->m.x, m->canvas_x,
			old_zoom, zoom);
	m->canvas_y = canvas_zoom_pan(anchor_y, m->m.y, m->canvas_y,
			old_zoom, zoom);
	m->canvas_zoom = zoom;
	updatecanvas(m, 1);
	motionnotify(0, NULL, 0, 0, 0, 0);
}

int
tickcanvaszoom(Monitor *m, const struct timespec *now)
{
	double dt, new_zoom;

	if (!m || fabs(m->canvas_zoom_target - m->canvas_zoom)
			< CANVAS_ZOOM_EPSILON) {
		if (m && m->canvas_zoom != m->canvas_zoom_target)
			setcanvaszoom(m, m->canvas_zoom_target);
		return 0;
	}
	dt = now->tv_sec - m->canvas_zoom_frame.tv_sec
			+ (now->tv_nsec - m->canvas_zoom_frame.tv_nsec) / 1000000000.0;
	dt = MAX(0.0, MIN(dt, 1.0 / 30.0));
	m->canvas_zoom_frame = *now;
	new_zoom = canvas_animate_zoom(m->canvas_zoom,
			m->canvas_zoom_target, dt);
	setcanvaszoom(m, new_zoom);
	return new_zoom != m->canvas_zoom_target;
}

void
zoomcanvas(const Arg *arg)
{
	if (!arg || !ISCANVAS(selmon))
		return;
	zoomcanvasby(selmon, pow(config.zoom_step, arg->f));
}

void
focusmon(const Arg *arg)
{
	int i = 0, nmons = wl_list_length(&mons);
	if (nmons) {
		do /* don't switch to disabled mons */
			selmon = dirtomon(arg->i);
		while (!selmon->wlr_output->enabled && i++ < nmons);
	}
	focusclient(focustop(selmon), 1);
}

void
focusstack(const Arg *arg)
{
	/* Focus the next or previous client (in tiling order) on selmon */
	Client *c, *sel = focustop(selmon);
	if (!sel || (sel->isfullscreen && !client_has_children(sel)))
		return;
	if (arg->i > 0) {
		wl_list_for_each(c, &sel->link, link) {
			if (&c->link == &clients)
				continue; /* wrap past the sentinel node */
			if (VISIBLEON(c, selmon))
				break; /* found it */
		}
	} else {
		wl_list_for_each_reverse(c, &sel->link, link) {
			if (&c->link == &clients)
				continue; /* wrap past the sentinel node */
			if (VISIBLEON(c, selmon))
				break; /* found it */
		}
	}
	/* If only one client is visible on selmon, then c == sel */
	focusclient(c, 1);
	centercanvas(NULL);
}

void
incnmaster(const Arg *arg)
{
	if (!arg || !selmon)
		return;
	selmon->nmaster = MAX(selmon->nmaster + arg->i, 0);
	arrange(selmon);
}

void
monocle(Monitor *m)
{
	Client *c;
	int n = 0;

	wl_list_for_each(c, &clients, link) {
		if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen)
			continue;
		resize(c, m->w, 0);
		n++;
	}
	if (n)
		snprintf(m->ltsymbol, LENGTH(m->ltsymbol), "[%d]", n);
	if ((c = focustop(m)))
		wlr_scene_node_raise_to_top(&c->scene->node);
}

void
setlayout(const Arg *arg)
{
	if (!selmon)
		return;
	if (!arg || !arg->v || arg->v != selmon->lt[selmon->sellt])
		selmon->sellt ^= 1;
	if (arg && arg->v)
		selmon->lt[selmon->sellt] = (Layout *)arg->v;
	strncpy(selmon->ltsymbol, selmon->lt[selmon->sellt]->symbol, LENGTH(selmon->ltsymbol));
	arrange(selmon);
	printstatus();
}

void
setmfact(const Arg *arg)
{
	float f;

	if (!arg || !selmon || !selmon->lt[selmon->sellt]->arrange)
		return;
	f = arg->f < 1.0f ? arg->f + selmon->mfact : arg->f - 1.0f;
	if (f < 0.1 || f > 0.9)
		return;
	selmon->mfact = f;
	arrange(selmon);
}

void
tag(const Arg *arg)
{
	Client *sel = focustop(selmon);
	if (!sel || (arg->ui & TAGMASK) == 0)
		return;

	sel->tags = arg->ui & TAGMASK;
	focusclient(focustop(selmon), 1);
	arrange(selmon);
	printstatus();
}

void
tagmon(const Arg *arg)
{
	Client *sel = focustop(selmon);
	if (sel)
		setmon(sel, dirtomon(arg->i), 0);
}

void
tile(Monitor *m)
{
	unsigned int mw, my, ty;
	int i, n = 0;
	Client *c;

	wl_list_for_each(c, &clients, link)
		if (VISIBLEON(c, m) && !c->isfloating && !c->isfullscreen)
			n++;
	if (n == 0)
		return;

	if (n > m->nmaster)
		mw = m->nmaster ? (int)roundf(m->w.width * m->mfact) : 0;
	else
		mw = m->w.width;
	i = my = ty = 0;
	wl_list_for_each(c, &clients, link) {
		if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen)
			continue;
		if (i < m->nmaster) {
			resize(c, (struct wlr_box){.x = m->w.x, .y = m->w.y + my, .width = mw,
				.height = (m->w.height - my) / (MIN(n, m->nmaster) - i)}, 0);
			my += c->geom.height;
		} else {
			resize(c, (struct wlr_box){.x = m->w.x + mw, .y = m->w.y + ty,
				.width = m->w.width - mw, .height = (m->w.height - ty) / (n - i)}, 0);
			ty += c->geom.height;
		}
		i++;
	}
}

void
togglefloating(const Arg *arg)
{
	Client *sel = focustop(selmon);
	/* return if fullscreen */
	if (sel && !sel->isfullscreen)
		setfloating(sel, !sel->isfloating);
}

void
togglefullscreen(const Arg *arg)
{
	Client *sel = focustop(selmon);
	if (sel)
		setfullscreen(sel, !sel->isfullscreen);
}

void
toggletag(const Arg *arg)
{
	uint32_t newtags;
	Client *sel = focustop(selmon);
	if (!sel || !(newtags = sel->tags ^ (arg->ui & TAGMASK)))
		return;

	sel->tags = newtags;
	focusclient(focustop(selmon), 1);
	arrange(selmon);
	printstatus();
}

void
toggleview(const Arg *arg)
{
	uint32_t newtagset;
	if (!(newtagset = selmon ? selmon->tagset[selmon->seltags] ^ (arg->ui & TAGMASK) : 0))
		return;

	selmon->tagset[selmon->seltags] = newtagset;
	focusclient(focustop(selmon), 1);
	arrange(selmon);
	printstatus();
}

void
view(const Arg *arg)
{
	if (!selmon || (arg->ui & TAGMASK) == selmon->tagset[selmon->seltags])
		return;
	selmon->seltags ^= 1; /* toggle sel tagset */
	if (arg->ui & TAGMASK)
		selmon->tagset[selmon->seltags] = arg->ui & TAGMASK;
	focusclient(focustop(selmon), 1);
	arrange(selmon);
	printstatus();
}

void
zoom(const Arg *arg)
{
	Client *c, *sel = focustop(selmon);

	if (!sel || !selmon || !selmon->lt[selmon->sellt]->arrange || sel->isfloating)
		return;

	/* Search for the first tiled window that is not sel, marking sel as
	 * NULL if we pass it along the way */
	wl_list_for_each(c, &clients, link) {
		if (VISIBLEON(c, selmon) && !c->isfloating) {
			if (c != sel)
				break;
			sel = NULL;
		}
	}

	/* Return if no other tiled window was found */
	if (&c->link == &clients)
		return;

	/* If we passed sel, move c to the front; otherwise, move sel to the
	 * front */
	if (!sel)
		sel = c;
	wl_list_remove(&sel->link);
	wl_list_insert(&clients, &sel->link);

	focusclient(sel, 1);
	arrange(selmon);
}
