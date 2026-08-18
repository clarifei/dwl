/* See LICENSE file for copyright and license details. */
/* Canvas transforms, scene scaling, placement, and navigation. */

#include "inca.h"

typedef struct {
	struct wlr_addon addon;
	struct wl_listener commit;
	struct wlr_scene_buffer *buffer;
	struct wlr_surface *surface;
	int x, y;
	int scaled_x, scaled_y;
	int width, height;
	int scaled_width, scaled_height;
	int corner_radius, scaled_corner_radius;
	struct clipped_region clipped_region, scaled_clipped_region;
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
	if (buffer->dst_width != state->scaled_width
			|| buffer->dst_height != state->scaled_height)
		wlr_scene_buffer_set_dest_size(buffer,
				state->scaled_width, state->scaled_height);
	if (buffer->filter_mode != WLR_SCALE_FILTER_BILINEAR)
		wlr_scene_buffer_set_filter_mode(buffer, WLR_SCALE_FILTER_BILINEAR);
}

static struct clipped_region
canvasclippedregionscale(const struct clipped_region *region, double scale)
{
	struct clipped_region scaled = *region;

	scaled.area.x = (int)round(region->area.x * scale);
	scaled.area.y = (int)round(region->area.y * scale);
	scaled.area.width = canvas_scaled_extent(region->area.x,
			region->area.width, scale);
	scaled.area.height = canvas_scaled_extent(region->area.y,
			region->area.height, scale);
	scaled.corner_radius = (int)round(region->corner_radius * scale);
	return scaled;
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
	if (node->x != x || node->y != y)
		wlr_scene_node_set_position(node, x, y);
}

static void
canvasnodecommit(struct wl_listener *listener, void *data)
{
	CanvasNodeState *buffer_state = wl_container_of(listener, buffer_state, commit);
	struct wlr_scene_node *node = &buffer_state->buffer->node;
	struct wlr_addon *addon;
	Client *c = NULL;
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
	toplevel_from_wlr_surface(buffer_state->surface, &c, NULL);
	if (c) {
		if (clientcanvasscale(c) != 1.0)
			clientsceneupdate(c);
		else
			clientbufferfxupdate(c, buffer_state->buffer, 1.0);
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
	.name = "inca_canvas_node",
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
		state->corner_radius = state->scaled_corner_radius = rect->corner_radius;
		state->clipped_region = state->scaled_clipped_region = rect->clipped_region;
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
	CanvasNodeState *state;

	if (node->type == WLR_SCENE_NODE_SHADOW
			|| node->type == WLR_SCENE_NODE_OPTIMIZED_BLUR)
		return;

	state = canvasnodestate(node);

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
		if (memcmp(&rect->clipped_region, &state->scaled_clipped_region,
				sizeof(rect->clipped_region)))
			state->clipped_region = rect->clipped_region;
		if (rect->corner_radius != state->scaled_corner_radius)
			state->corner_radius = rect->corner_radius;
		state->scaled_corner_radius = (int)round(state->corner_radius * scale);
		state->scaled_clipped_region = canvasclippedregionscale(
				&state->clipped_region, scale);
		if (rect->width != state->scaled_width
				|| rect->height != state->scaled_height)
			wlr_scene_rect_set_size(rect,
					state->scaled_width, state->scaled_height);
		if (rect->corner_radius != state->scaled_corner_radius)
			wlr_scene_rect_set_corner_radius(rect,
					state->scaled_corner_radius, rect->corners);
		if (memcmp(&rect->clipped_region, &state->scaled_clipped_region,
				sizeof(rect->clipped_region)))
			wlr_scene_rect_set_clipped_region(rect,
					state->scaled_clipped_region);
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
	if (!c || !c->mon || c->isfullscreen)
		return 1.0;
	return c->mon->canvas_zoom > 0.0 ? c->mon->canvas_zoom : 1.0;
}

void
canvaspointtoscreen(Monitor *m, double x, double y,
		double *screen_x, double *screen_y)
{
	double zoom;

	if (!m) {
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

	if (!m) {
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
	int sx, sy;

	if (!c || !c->scene)
		return;
	if (c->mon && !c->isfullscreen) {
		canvaspointtoscreen(c->mon, c->geom.x, c->geom.y, &x, &y);
	} else {
		x = c->geom.x;
		y = c->geom.y;
	}
	sx = (int)round(x);
	sy = (int)round(y);
	if (c->scene->node.x != sx || c->scene->node.y != sy)
		wlr_scene_node_set_position(&c->scene->node, sx, sy);
}

void
clientsceneupdate(Client *c)
{
	struct wlr_scene_node *node;
	double scale;

	clientsceneposition(c);
	if (!c || !c->scene)
		return;
	scale = clientcanvasscale(c);
	wl_list_for_each(node, &c->scene->children, link) {
		if (node != &c->border->node)
			canvasnodescale(node, scale);
	}
	clienteffectsupdate(c);
}


void
clientsnap(Client *c, struct wlr_box *geo)
{
	Client *other;
	long long best_dx = config.snap_distance + 1;
	long long best_dy = config.snap_distance + 1;

	if (!c || !geo || !c->mon || config.snap_distance <= 0)
		return;
	wl_list_for_each(other, &clients, link) {
		long long delta;

		if (other == c || other->mon != c->mon || other->iscollapsed
				|| other->isfullscreen)
			continue;
		if ((long long)geo->y < (long long)other->geom.y + other->geom.height
				&& (long long)geo->y + geo->height > other->geom.y) {
			delta = (long long)other->geom.x - config.window_gap
					- geo->width - geo->x;
			if (llabs(delta) < llabs(best_dx))
				best_dx = delta;
			delta = (long long)other->geom.x + other->geom.width
					+ config.window_gap - geo->x;
			if (llabs(delta) < llabs(best_dx))
				best_dx = delta;
		}
		if ((long long)geo->x < (long long)other->geom.x + other->geom.width
				&& (long long)geo->x + geo->width > other->geom.x) {
			delta = (long long)other->geom.y - config.window_gap
					- geo->height - geo->y;
			if (llabs(delta) < llabs(best_dy))
				best_dy = delta;
			delta = (long long)other->geom.y + other->geom.height
					+ config.window_gap - geo->y;
			if (llabs(delta) < llabs(best_dy))
				best_dy = delta;
		}
	}
	if (llabs(best_dx) <= config.snap_distance)
		geo->x = canvas_clamp_coordinate((long long)geo->x + best_dx);
	if (llabs(best_dy) <= config.snap_distance)
		geo->y = canvas_clamp_coordinate((long long)geo->y + best_dy);
}


void
clientsettle(Client *c)
{
	CanvasBox moving, placed;
	CanvasBox *fixed;
	Client *other;
	size_t count = 0, i = 0;
	int overlap = 0;

	if (!c || !c->mon || c->isfullscreen)
		return;
	moving = (CanvasBox){c->geom.x, c->geom.y, c->geom.width, c->geom.height};
	wl_list_for_each(other, &clients, link)
		if (other != c && other->mon == c->mon && !other->iscollapsed
				&& !other->isfullscreen) {
			overlap |= canvas_boxes_overlap(moving,
					(CanvasBox){other->geom.x, other->geom.y,
							other->geom.width, other->geom.height},
					config.window_gap);
			count++;
		}
	if (!count || !overlap)
		return;
	fixed = ecalloc(count, sizeof(*fixed));
	wl_list_for_each(other, &clients, link) {
		if (other == c || other->mon != c->mon || other->iscollapsed
				|| other->isfullscreen)
			continue;
		fixed[i++] = (CanvasBox){other->geom.x, other->geom.y,
				other->geom.width, other->geom.height};
	}
	placed = canvas_place_nearest(moving, fixed, count, config.window_gap);
	free(fixed);
	if (placed.x != c->geom.x || placed.y != c->geom.y)
		resize(c, (struct wlr_box){placed.x, placed.y,
				placed.width, placed.height}, 1);
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
			wlr_scene_node_set_enabled(&c->scene->node, 1);
			client_set_suspended(c, c->iscollapsed);
		}
	}

	wlr_scene_node_set_enabled(&m->fullscreen_bg->node,
			(c = focustop(m)) && c->isfullscreen);
	updatecanvas(m, 1);
	motionnotify(0, NULL, 0, 0, 0, 0);
	checkidleinhibitor(NULL);
}

void
pancanvas(Monitor *m, double dx, double dy)
{
	Client *focused;

	if (!m || !m->wlr_output->enabled)
		return;
	if ((focused = focustop(m)) && focused->isfullscreen)
		return;

	if (!dx && !dy)
		return;
	m->canvas_zoom_target = m->canvas_zoom;
	m->canvas_x += dx;
	m->canvas_y += dy;
	m->canvas_x_target = m->canvas_x;
	m->canvas_y_target = m->canvas_y;
	updatecanvas(m, 0);
}

void
homecanvas(const Arg *arg)
{
	Client *focused;

	if (!selmon || ((focused = focustop(selmon)) && focused->isfullscreen))
		return;
	selmon->canvas_x_target = 0.0;
	selmon->canvas_y_target = 0.0;
	selmon->canvas_zoom_target = 1.0;
	clock_gettime(CLOCK_MONOTONIC, &selmon->canvas_camera_frame);
	clock_gettime(CLOCK_MONOTONIC, &selmon->canvas_zoom_frame);
	wlr_output_schedule_frame(selmon->wlr_output);
}

void
centercanvas(const Arg *arg)
{
	Client *c;
	double center_x, center_y, client_x, client_y, zoom;

	if (!selmon || !(c = focustop(selmon)) || c->isfullscreen)
		return;
	zoom = selmon->canvas_zoom_target;
	center_x = selmon->w.x + selmon->w.width / 2.0;
	center_y = selmon->w.y + selmon->w.height / 2.0;
	client_x = c->geom.x + c->geom.width / 2.0;
	client_y = c->geom.y + c->geom.height / 2.0;
	selmon->canvas_x_target = center_x - selmon->m.x
			- (client_x - selmon->m.x) * zoom;
	selmon->canvas_y_target = center_y - selmon->m.y
			- (client_y - selmon->m.y) * zoom;
	clock_gettime(CLOCK_MONOTONIC, &selmon->canvas_camera_frame);
	wlr_output_schedule_frame(selmon->wlr_output);
}

void
zoomcanvasby(Monitor *m, double factor)
{
	Client *focused;
	double base_zoom, new_zoom;
	int was_active;

	if (!m || !isfinite(factor) || factor <= 0.0
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
	/* Zoom owns the viewport anchor; do not let an older camera target pull
	 * the view sideways while the scale is still interpolating. */
	m->canvas_x_target = m->canvas_x;
	m->canvas_y_target = m->canvas_y;
	m->canvas_zoom_target = new_zoom;
	wlr_output_schedule_frame(m->wlr_output);
}

void
setcanvaszoom(Monitor *m, double zoom)
{
	double anchor_x, anchor_y, old_zoom;
	int pan_target_matches;

	if (!m || zoom == m->canvas_zoom)
		return;
	pan_target_matches = fabs(m->canvas_x_target - m->canvas_x)
			< CANVAS_ZOOM_EPSILON
			&& fabs(m->canvas_y_target - m->canvas_y)
			< CANVAS_ZOOM_EPSILON;
	anchor_x = m->w.x + m->w.width / 2.0;
	anchor_y = m->w.y + m->w.height / 2.0;
	old_zoom = m->canvas_zoom;
	m->canvas_x = canvas_zoom_pan(anchor_x, m->m.x, m->canvas_x,
			old_zoom, zoom);
	m->canvas_y = canvas_zoom_pan(anchor_y, m->m.y, m->canvas_y,
			old_zoom, zoom);
	m->canvas_zoom = zoom;
	m->canvas_dirty = 1;
	if (pan_target_matches) {
		m->canvas_x_target = m->canvas_x;
		m->canvas_y_target = m->canvas_y;
	}
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
	new_zoom = canvas_animate_value(m->canvas_zoom,
			m->canvas_zoom_target, dt);
	setcanvaszoom(m, new_zoom);
	return new_zoom != m->canvas_zoom_target;
}


int
tickcanvascamera(Monitor *m, const struct timespec *now)
{
	double dt, x, y;

	if (!m)
		return 0;
	if (fabs(m->canvas_x_target - m->canvas_x) < CANVAS_ZOOM_EPSILON
			&& fabs(m->canvas_y_target - m->canvas_y) < CANVAS_ZOOM_EPSILON) {
		if (m->canvas_x == m->canvas_x_target
				&& m->canvas_y == m->canvas_y_target)
			return 0;
		m->canvas_x = m->canvas_x_target;
		m->canvas_y = m->canvas_y_target;
		return 0;
	}
	dt = now->tv_sec - m->canvas_camera_frame.tv_sec
			+ (now->tv_nsec - m->canvas_camera_frame.tv_nsec) / 1000000000.0;
	dt = MAX(0.0, MIN(dt, 1.0 / 30.0));
	m->canvas_camera_frame = *now;
	x = canvas_animate_value(m->canvas_x, m->canvas_x_target, dt);
	y = canvas_animate_value(m->canvas_y, m->canvas_y_target, dt);
	m->canvas_x = x;
	m->canvas_y = y;
	return x != m->canvas_x_target || y != m->canvas_y_target;
}


int
tickcanvasedgepan(Monitor *m, const struct timespec *now)
{
	double dt, length, vx, vy, world_x, world_y;
	struct wlr_box geo;

	if (!m || cursor_mode != CurMove || !grabc || grabc->mon != m
			|| xytomon(cursor->x, cursor->y) != m)
		return 0;
	vx = canvas_edge_pan_velocity(cursor->x, m->w.x, m->w.width,
			config.edge_pan_zone, config.edge_pan_min_speed,
			config.edge_pan_max_speed);
	vy = canvas_edge_pan_velocity(cursor->y, m->w.y, m->w.height,
			config.edge_pan_zone, config.edge_pan_min_speed,
			config.edge_pan_max_speed);
	if (!vx && !vy) {
		m->canvas_pan_frame = (struct timespec){0};
		return 0;
	}
	length = hypot(vx, vy);
	if (length > config.edge_pan_max_speed && length > 0.0) {
		vx *= config.edge_pan_max_speed / length;
		vy *= config.edge_pan_max_speed / length;
	}
	if (!m->canvas_pan_frame.tv_sec && !m->canvas_pan_frame.tv_nsec) {
		m->canvas_pan_frame = *now;
		return 1;
	}
	dt = now->tv_sec - m->canvas_pan_frame.tv_sec
			+ (now->tv_nsec - m->canvas_pan_frame.tv_nsec) / 1000000000.0;
	dt = MAX(0.0, MIN(dt, 1.0 / 30.0));
	m->canvas_pan_frame = *now;
	m->canvas_zoom_target = m->canvas_zoom;
	m->canvas_x -= vx * dt;
	m->canvas_y -= vy * dt;
	m->canvas_x_target = m->canvas_x;
	m->canvas_y_target = m->canvas_y;
	canvaspointtoworld(m, cursor->x, cursor->y, &world_x, &world_y);
	geo = (struct wlr_box){
		.x = canvas_round_coordinate(world_x - grabcx),
		.y = canvas_round_coordinate(world_y - grabcy),
		.width = grabc->geom.width,
		.height = grabc->geom.height,
	};
	clientsnap(grabc, &geo);
	resize(grabc, geo, 1);
	return 1;
}

void
zoomcanvas(const Arg *arg)
{
	if (!arg || !selmon)
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
	/* Traverse stable client order; focusclient() reorders fstack on every switch. */
	Client *c, *sel, *next = NULL;
	struct wl_list *link;

	if (!arg || !selmon || !arg->i)
		return;
	sel = focustop(selmon);
	if (!sel || (sel->isfullscreen && !client_has_children(sel)))
		return;
	link = &sel->link;
	while ((link = canvas_cycle_link(&clients, link, arg->i > 0))
			!= &sel->link) {
		c = wl_container_of(link, c, link);
		if (CLIENTON(c, selmon) && !c->iscollapsed) {
			next = c;
			break;
		}
	}
	if (next) {
		focusclient(next, 1);
		centercanvas(NULL);
	}
}

void
sendtomonitor(const Arg *arg)
{
	Client *sel = focustop(selmon);
	if (sel)
		setclientmonitor(sel, dirtomon(arg->i));
}

void
togglefullscreen(const Arg *arg)
{
	Client *sel = focustop(selmon);
	if (sel)
		setfullscreen(sel, !sel->isfullscreen);
}
