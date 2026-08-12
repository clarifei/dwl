#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "include/canvas.h"

static int
close_enough(double a, double b)
{
	return fabs(a - b) < 0.000001;
}

int
main(void)
{
	double anchor = 750.0;
	double origin = 100.0;
	double pan = 35.5;
	double world = canvas_screen_to_world(anchor, origin, pan, 1.0);
	double zoomed_pan = canvas_zoom_pan(anchor, origin, pan, 1.0, 1.5);
	double subpixel_pan = 0.0;
	CanvasBox fixed[] = {
		{.x = 0, .y = 0, .width = 100, .height = 100},
		{.x = 120, .y = 0, .width = 100, .height = 100},
	};
	CanvasBox placed;

	assert(close_enough(canvas_pan_delta(12.0, 1.0), 12.0));
	assert(close_enough(canvas_axis_pan_delta(12.0, 1.0), -12.0));
	subpixel_pan += canvas_pan_delta(0.4, 1.0);
	subpixel_pan += canvas_pan_delta(0.4, 1.0);
	assert(close_enough(subpixel_pan, 0.8));

	assert(close_enough(canvas_world_to_screen(world, origin, pan, 1.0), anchor));
	assert(close_enough(canvas_world_to_screen(world, origin, zoomed_pan, 1.5), anchor));
	assert(close_enough(canvas_screen_to_world(
		canvas_world_to_screen(420.25, origin, pan, 2.0), origin, pan, 2.0),
		420.25));
	assert(canvas_zoom_factor(1.2, -30.0) > 1.0);
	assert(canvas_zoom_factor(1.2, 30.0) < 1.0);
	assert(close_enough(canvas_clamp_zoom(0.25, 4.0, 1.2), 1.0));
	assert(close_enough(canvas_animate_zoom(0.5, 1.0, 1.0 / 60.0), 0.59));
	assert(close_enough(
		canvas_animate_zoom(canvas_animate_zoom(0.5, 1.0, 1.0 / 120.0),
			1.0, 1.0 / 120.0),
		canvas_animate_zoom(0.5, 1.0, 1.0 / 60.0)));
	assert(canvas_scaled_extent(1, 2, 0.5) == 1);
	assert(canvas_scaled_extent(1, 3, 0.5) == 1);
	assert(canvas_buffer_base_length(800, 1200, 1200, 1) == 1200);
	assert(canvas_buffer_base_length(800, 1200, 1200, 0) == 800);
	assert(canvas_buffer_base_length(800, 900, 1200, 0) == 900);

	assert(canvas_output_mode_better(1920, 1080, 390297,
		1920, 1080, 60000));
	assert(canvas_output_mode_better(3840, 2160, 60000,
		1920, 1080, 390297));
	assert(!canvas_output_mode_better(1920, 1080, 60000,
		1920, 1080, 390297));

	assert(canvas_boxes_overlap((CanvasBox){10, 10, 50, 50}, fixed[0], 16));
	assert(!canvas_boxes_overlap((CanvasBox){116, 0, 50, 50}, fixed[0], 16));
	placed = canvas_place_nearest((CanvasBox){300, 20, 80, 60}, fixed, 2, 16);
	assert(placed.x == 300 && placed.y == 20);
	placed = canvas_place_nearest((CanvasBox){80, 20, 80, 60}, fixed, 2, 16);
	assert(!canvas_boxes_overlap(placed, fixed[0], 16));
	assert(!canvas_boxes_overlap(placed, fixed[1], 16));
	assert(placed.x == 80 && placed.y == -76);

	assert(canvas_edge_pan_velocity(50, 0, 1920, 80, 120, 900) < 0);
	assert(close_enough(canvas_edge_pan_velocity(960, 0, 1920, 80, 120, 900), 0));
	assert(canvas_edge_pan_velocity(1919, 0, 1920, 80, 120, 900) > 800);
	assert(canvas_clamp_coordinate((long long)INT_MAX + 1) == INT_MAX);
	assert(canvas_clamp_coordinate((long long)INT_MIN - 1) == INT_MIN);

	puts("canvas tests passed");
	return 0;
}
