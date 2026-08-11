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

	assert(close_enough(canvas_pan_delta(12.0, 1.0), 12.0));
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

	assert(canvas_output_mode_better(1920, 1080, 390297,
		1920, 1080, 60000));
	assert(canvas_output_mode_better(3840, 2160, 60000,
		1920, 1080, 390297));
	assert(!canvas_output_mode_better(1920, 1080, 60000,
		1920, 1080, 390297));

	puts("canvas tests passed");
	return 0;
}
