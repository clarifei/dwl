#ifndef CANVAS_H
#define CANVAS_H

#include <math.h>
#include <stdint.h>

static inline double
canvas_pan_delta(double delta, double speed)
{
	return delta * speed;
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

static inline int
canvas_output_mode_better(int width, int height, int refresh,
		int best_width, int best_height, int best_refresh)
{
	int64_t area = (int64_t)width * height;
	int64_t best_area = (int64_t)best_width * best_height;

	return area > best_area || (area == best_area && refresh > best_refresh);
}

#endif
