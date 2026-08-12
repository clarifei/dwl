/* ext-background-effect-v1 state and SceneFX layer-shell integration. */

#include "ext-background-effect-v1-protocol.h"
#include <assert.h>
#include <wlr/util/addon.h>

typedef struct {
	pixman_region32_t blur_region;
	int committed;
} BackgroundEffectState;

typedef struct {
	struct wl_resource *resource;
	struct wlr_surface *surface;
	struct wlr_addon addon;
	struct wlr_surface_synced synced;
	BackgroundEffectState pending, current;
} BackgroundEffect;

typedef struct {
	struct wl_list link;
	struct wlr_scene_rect *rect;
} LayerEffectRect;

#define MAX_LAYER_EFFECT_RECTS 256

static const struct ext_background_effect_surface_v1_interface background_surface_impl;
static const struct wlr_addon_interface background_addon_impl;

static BackgroundEffect *
backgroundeffectfromresource(struct wl_resource *resource)
{
	assert(wl_resource_instance_of(resource,
			&ext_background_effect_surface_v1_interface, &background_surface_impl));
	return wl_resource_get_user_data(resource);
}

static BackgroundEffect *
backgroundeffectfromsurface(struct wlr_surface *surface)
{
	struct wlr_addon *addon = wlr_addon_find(&surface->addons, NULL,
			&background_addon_impl);
	BackgroundEffect *effect;

	return addon ? wl_container_of(addon, effect, addon) : NULL;
}

static LayerSurface *
layersurfacefromsurface(struct wlr_surface *surface)
{
	struct wlr_layer_surface_v1 *layer_surface =
			wlr_layer_surface_v1_try_from_wlr_surface(surface);
	return layer_surface ? layer_surface->data : NULL;
}

static void
backgroundeffectdestroy(BackgroundEffect *effect)
{
	if (!effect)
		return;
	wlr_surface_synced_finish(&effect->synced);
	wlr_addon_finish(&effect->addon);
	if (effect->resource) {
		wl_resource_set_user_data(effect->resource, NULL);
		effect->resource = NULL;
	}
	free(effect);
}

static void
backgroundresourcedestroy(struct wl_resource *resource)
{
	BackgroundEffect *effect = backgroundeffectfromresource(resource);
	struct wlr_surface *surface;

	if (!effect)
		return;
	surface = effect->surface;
	backgroundeffectdestroy(effect);
	if (surface)
		layereffectsupdate(layersurfacefromsurface(surface));
}

static void
backgroundsurfacerequestdestroy(struct wl_client *client,
		struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
backgroundsetblurregion(struct wl_client *client, struct wl_resource *resource,
		struct wl_resource *region_resource)
{
	BackgroundEffect *effect = backgroundeffectfromresource(resource);

	if (!effect) {
		wl_resource_post_error(resource,
				EXT_BACKGROUND_EFFECT_SURFACE_V1_ERROR_SURFACE_DESTROYED,
				"the wl_surface object has been destroyed");
		return;
	}
	pixman_region32_clear(&effect->pending.blur_region);
	if (region_resource)
		pixman_region32_copy(&effect->pending.blur_region,
				wlr_region_from_resource(region_resource));
	effect->pending.committed = 1;
}

static const struct ext_background_effect_surface_v1_interface background_surface_impl = {
	.destroy = backgroundsurfacerequestdestroy,
	.set_blur_region = backgroundsetblurregion,
};

static void
backgroundstateinit(void *data)
{
	BackgroundEffectState *state = data;
	pixman_region32_init(&state->blur_region);
}

static void
backgroundstatefinish(void *data)
{
	BackgroundEffectState *state = data;
	pixman_region32_fini(&state->blur_region);
}

static void
backgroundstatemove(void *destination, void *source)
{
	BackgroundEffectState *dst = destination;
	BackgroundEffectState *src = source;

	if (src->committed)
		pixman_region32_copy(&dst->blur_region, &src->blur_region);
	dst->committed = src->committed;
	src->committed = 0;
}

static const struct wlr_surface_synced_impl background_synced_impl = {
	.state_size = sizeof(BackgroundEffectState),
	.init_state = backgroundstateinit,
	.finish_state = backgroundstatefinish,
	.move_state = backgroundstatemove,
};

static void
backgroundaddondestroy(struct wlr_addon *addon)
{
	BackgroundEffect *effect = wl_container_of(addon, effect, addon);
	backgroundeffectdestroy(effect);
}

static const struct wlr_addon_interface background_addon_impl = {
	.name = "ext_background_effect_surface_v1",
	.destroy = backgroundaddondestroy,
};

static void
backgroundmanagerdestroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
backgroundmanagerget(struct wl_client *client, struct wl_resource *manager,
		uint32_t id, struct wl_resource *surface_resource)
{
	struct wlr_surface *surface = wlr_surface_from_resource(surface_resource);
	BackgroundEffect *effect;

	if (backgroundeffectfromsurface(surface)) {
		wl_resource_post_error(manager,
				EXT_BACKGROUND_EFFECT_MANAGER_V1_ERROR_BACKGROUND_EFFECT_EXISTS,
				"the wl_surface already has a background effect object");
		return;
	}
	effect = calloc(1, sizeof(*effect));
	if (!effect) {
		wl_resource_post_no_memory(manager);
		return;
	}
	if (!wlr_surface_synced_init(&effect->synced, surface,
			&background_synced_impl, &effect->pending, &effect->current)) {
		free(effect);
		wl_resource_post_no_memory(manager);
		return;
	}
	effect->resource = wl_resource_create(client,
			&ext_background_effect_surface_v1_interface,
			wl_resource_get_version(manager), id);
	if (!effect->resource) {
		wlr_surface_synced_finish(&effect->synced);
		free(effect);
		wl_resource_post_no_memory(manager);
		return;
	}
	effect->surface = surface;
	wlr_addon_init(&effect->addon, &surface->addons, NULL, &background_addon_impl);
	wl_resource_set_implementation(effect->resource, &background_surface_impl,
			effect, backgroundresourcedestroy);
}

static const struct ext_background_effect_manager_v1_interface background_manager_impl = {
	.destroy = backgroundmanagerdestroy,
	.get_background_effect = backgroundmanagerget,
};

static void
backgroundmanagerbind(struct wl_client *client, void *data,
		uint32_t version, uint32_t id)
{
	struct wl_resource *resource = wl_resource_create(client,
			&ext_background_effect_manager_v1_interface, version, id);

	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &background_manager_impl, NULL, NULL);
	ext_background_effect_manager_v1_send_capabilities(resource,
			EXT_BACKGROUND_EFFECT_MANAGER_V1_CAPABILITY_BLUR);
}

void
backgroundeffectsinit(void)
{
	if (!wl_global_create(dpy, &ext_background_effect_manager_v1_interface,
			1, NULL, backgroundmanagerbind))
		die("failed to create ext-background-effect-v1 global");
}

const pixman_region32_t *
backgroundeffectregion(struct wlr_surface *surface)
{
	BackgroundEffect *effect = backgroundeffectfromsurface(surface);
	return effect ? &effect->current.blur_region : NULL;
}

static void
layeropacity(struct wlr_scene_buffer *buffer, int sx, int sy, void *data)
{
	struct wlr_scene_surface *surface = wlr_scene_surface_try_from_buffer(buffer);
	float opacity = *(float *)data;
	const struct wlr_alpha_modifier_surface_v1_state *alpha;

	if (surface && (alpha = wlr_alpha_modifier_v1_get_surface_state(surface->surface)))
		opacity *= (float)alpha->multiplier;
	wlr_scene_buffer_set_opacity(buffer, opacity);
}

void
layereffectsclear(LayerSurface *l)
{
	LayerEffectRect *effect, *tmp;

	if (!l)
		return;
	wl_list_for_each_safe(effect, tmp, &l->effect_rects, link) {
		wlr_scene_node_destroy(&effect->rect->node);
		wl_list_remove(&effect->link);
		free(effect);
	}
}

void
layereffectsupdate(LayerSurface *l)
{
	const pixman_region32_t *requested;
	pixman_region32_t region;
	LayerEffectRect *effect, *tmp;
	const pixman_box32_t *boxes;
	struct wlr_surface *surface;
	float opacity;
	int enabled, height, i, width, count = 0;

	if (!l || !l->effects || !l->layer_surface)
		return;
	surface = l->layer_surface->surface;
	width = surface->current.width;
	height = surface->current.height;
	requested = backgroundeffectregion(surface);
	pixman_region32_init(&region);
	if (requested && width > 0 && height > 0)
		pixman_region32_intersect_rect(&region, requested, 0, 0,
				width, height);
	enabled = config.layer_effects_enabled && pixman_region32_not_empty(&region);
	opacity = enabled ? config.layer_opacity : 1.0f;
	wlr_scene_node_for_each_buffer(&l->scene->node, layeropacity, &opacity);

	boxes = enabled && config.blur_enabled
			? pixman_region32_rectangles(&region, &count) : NULL;
	if (count > MAX_LAYER_EFFECT_RECTS) {
		boxes = pixman_region32_extents(&region);
		count = 1;
	}
	effect = wl_container_of(l->effect_rects.next, effect, link);
	for (i = 0; i < count; i++) {
		if (&effect->link == &l->effect_rects) {
			tmp = calloc(1, sizeof(*tmp));
			if (!tmp)
				break;
			effect = tmp;
			effect->rect = wlr_scene_rect_create(l->effects, 1, 1,
					(float[4]){0.0f, 0.0f, 0.0f, 0.001f});
			if (!effect->rect) {
				free(effect);
				effect = wl_container_of(l->effect_rects.next, effect, link);
				break;
			}
			effect->rect->accepts_input = false;
			wlr_scene_rect_set_backdrop_blur(effect->rect, true);
			wlr_scene_rect_set_backdrop_blur_optimized(effect->rect, false);
			wl_list_insert(l->effect_rects.prev, &effect->link);
		}
		wlr_scene_node_set_position(&effect->rect->node, boxes[i].x1, boxes[i].y1);
		wlr_scene_rect_set_size(effect->rect,
				boxes[i].x2 - boxes[i].x1, boxes[i].y2 - boxes[i].y1);
		wlr_scene_node_set_enabled(&effect->rect->node, true);
		effect = wl_container_of(effect->link.next, effect, link);
	}
	while (&effect->link != &l->effect_rects) {
		tmp = wl_container_of(effect->link.next, tmp, link);
		wlr_scene_node_destroy(&effect->rect->node);
		wl_list_remove(&effect->link);
		free(effect);
		effect = tmp;
	}
	pixman_region32_fini(&region);
}

void
layereffectsupdateall(void)
{
	Monitor *m;
	LayerSurface *l;
	size_t i;

	wl_list_for_each(m, &mons, link)
		for (i = 0; i < LENGTH(m->layers); i++)
			wl_list_for_each(l, &m->layers[i], link)
				layereffectsupdate(l);
}
