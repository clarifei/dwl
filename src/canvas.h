#ifndef CANVAS_H
#define CANVAS_H

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <wayland-util.h>

#define CANVAS_NATIVE_ZOOM 1.0
#define CANVAS_ZOOM_EPSILON 0.001
#define CANVAS_ANIMATION_RESPONSE 0.18

typedef struct {
	int x, y;
	int width, height;
} CanvasBox;

static inline int
canvas_clamp_coordinate(long long coordinate)
{
	return coordinate < INT_MIN ? INT_MIN
			: coordinate > INT_MAX ? INT_MAX : (int)coordinate;
}

static inline int
canvas_round_coordinate(double coordinate)
{
	if (!isfinite(coordinate))
		return 0;
	if (coordinate <= (double)INT_MIN)
		return INT_MIN;
	if (coordinate >= (double)INT_MAX)
		return INT_MAX;
	return (int)llround(coordinate);
}


static inline struct wl_list *
canvas_cycle_link(struct wl_list *head, struct wl_list *link, int forward)
{
	struct wl_list *next = forward ? link->next : link->prev;

	return next == head ? (forward ? head->next : head->prev) : next;
}

static inline double
canvas_pan_delta(double delta, double speed)
{
	return delta * speed;
}

static inline double
canvas_axis_pan_delta(double delta, double speed)
{
	return canvas_pan_delta(-delta, speed);
}

static inline double
canvas_world_to_screen(double world, double origin, double pan, double zoom)
{
	return origin + (world - origin) * zoom + pan;
}

static inline double
canvas_screen_to_world(double screen, double origin, double pan, double zoom)
{
	return origin + (screen - origin - pan) / zoom;
}

static inline double
canvas_zoom_pan(double anchor, double origin, double pan,
		double old_zoom, double new_zoom)
{
	return anchor - origin - (anchor - origin - pan) * new_zoom / old_zoom;
}

static inline double
canvas_zoom_factor(double step, double delta)
{
	return pow(step, -delta / 30.0);
}

static inline double
canvas_clamp_zoom(double min, double max, double zoom)
{
	return fmin(CANVAS_NATIVE_ZOOM, fmax(min, fmin(max, zoom)));
}

static inline double
canvas_animate_value(double current, double target, double dt)
{
	double factor = 1.0 - pow(1.0 - CANVAS_ANIMATION_RESPONSE,
			fmax(0.0, dt) * 60.0);
	double next = current + (target - current) * factor;

	return fabs(target - next) < CANVAS_ZOOM_EPSILON ? target : next;
}

static inline int
canvas_scaled_extent(int position, int length, double scale)
{
	int extent;

	if (!length)
		return 0;
	extent = (int)round((position + length) * scale)
			- (int)round(position * scale);
	return extent > 0 ? extent : 1;
}

static inline int
canvas_buffer_base_length(int base, int destination, int scaled, int committed)
{
	return committed || destination != scaled ? destination : base;
}

static inline int
canvas_output_mode_better(int width, int height, int refresh,
		int best_width, int best_height, int best_refresh)
{
	int64_t area = (int64_t)width * height;
	int64_t best_area = (int64_t)best_width * best_height;

	return area > best_area || (area == best_area && refresh > best_refresh);
}


static inline int
canvas_boxes_overlap(CanvasBox a, CanvasBox b, int gap)
{
	return (long long)a.x < (long long)b.x + b.width + gap
			&& (long long)a.x + a.width + gap > b.x
			&& (long long)a.y < (long long)b.y + b.height + gap
			&& (long long)a.y + a.height + gap > b.y;
}

static inline CanvasBox
canvas_spawn_box(CanvasBox box, size_t ordinal, int step)
{
	static const int offsets[][2] = {
		{0, 0}, {1, 1}, {-1, 1}, {1, -1}, {-1, -1},
		{2, 0}, {0, 2}, {-2, 0}, {0, -2},
	};
	const size_t count = sizeof(offsets) / sizeof(offsets[0]);
	const int *offset = offsets[ordinal % count];
	long long distance = (long long)(step > 0 ? step : 1)
			* (long long)(ordinal / count + 1);

	box.x = canvas_clamp_coordinate((long long)box.x + offset[0] * distance);
	box.y = canvas_clamp_coordinate((long long)box.y + offset[1] * distance);
	return box;
}


static inline CanvasBox
canvas_place_nearest(CanvasBox moving, const CanvasBox *fixed, size_t count,
		int gap)
{
	CanvasBox best = moving;
	double best_distance = INFINITY;
	size_t xi, yi, i;

	for (i = 0; i < count; i++)
		if (canvas_boxes_overlap(moving, fixed[i], gap))
			break;
	if (i == count)
		return moving;

	for (xi = 0; xi <= count * 2; xi++) {
		int x = moving.x;

		if (xi)
			x = canvas_clamp_coordinate(xi & 1
					? (long long)fixed[(xi - 1) / 2].x - moving.width - gap
					: (long long)fixed[(xi - 1) / 2].x
							+ fixed[(xi - 1) / 2].width + gap);
		for (yi = 0; yi <= count * 2; yi++) {
			CanvasBox candidate = moving;
			double dx, dy, distance;

			candidate.x = x;
			if (yi)
				candidate.y = canvas_clamp_coordinate(yi & 1
						? (long long)fixed[(yi - 1) / 2].y - moving.height - gap
						: (long long)fixed[(yi - 1) / 2].y
								+ fixed[(yi - 1) / 2].height + gap);
			for (i = 0; i < count; i++)
				if (canvas_boxes_overlap(candidate, fixed[i], gap))
					break;
			if (i != count)
				continue;
			dx = (double)candidate.x - moving.x;
			dy = (double)candidate.y - moving.y;
			distance = dx * dx + dy * dy;
			if (distance < best_distance) {
				best = candidate;
				best_distance = distance;
			}
		}
	}
	return best;
}


static inline double
canvas_edge_pan_velocity(double position, double start, double length,
		double zone, double min_speed, double max_speed)
{
	double distance, depth, direction;

	if (zone <= 0.0 || length <= 0.0)
		return 0.0;
	if (position < start + zone) {
		distance = position - start;
		direction = -1.0;
	} else if (position > start + length - zone) {
		distance = start + length - position;
		direction = 1.0;
	} else {
		return 0.0;
	}
	depth = fmax(0.0, fmin(1.0, (zone - distance) / zone));
	return direction * (min_speed + (max_speed - min_speed) * depth * depth)
			* depth;
}

#endif
