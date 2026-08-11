#ifndef CANVAS_H
#define CANVAS_H

#include <math.h>
#include <stdint.h>

#define CANVAS_NATIVE_ZOOM 1.0
#define CANVAS_ZOOM_EPSILON 0.001
#define CANVAS_ZOOM_RESPONSE 0.18

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
canvas_animate_zoom(double current, double target, double dt)
{
	double factor = 1.0 - pow(1.0 - CANVAS_ZOOM_RESPONSE,
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

#endif
