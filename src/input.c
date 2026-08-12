/* See LICENSE file for copyright and license details. */
/* Keyboard, pointer, cursor, bindings, and input-device handling. */

void
axisnotify(struct wl_listener *listener, void *data)
{
	/* This event is forwarded by the cursor when a pointer emits an axis event,
	 * for example when you move the scroll wheel. */
	struct wlr_pointer_axis_event *event = data;
	struct wlr_keyboard *keyboard;
	struct wlr_surface *surface = NULL;
	Client *c = NULL;
	Monitor *m = xytomon(cursor->x, cursor->y);
	uint32_t mods;
	double dx = 0, dy = 0;
	int wheel;

	wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
	kb_group->release_armed = 0;
	xytonode(cursor->x, cursor->y, &surface, &c, NULL, NULL, NULL);
	keyboard = wlr_seat_get_keyboard(seat);
	mods = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;
	wheel = event->source == WL_POINTER_AXIS_SOURCE_WHEEL
			|| event->source == WL_POINTER_AXIS_SOURCE_WHEEL_TILT;
	if (!locked && ISCANVAS(m) && (CLEANMASK(mods) & WLR_MODIFIER_LOGO)) {
		if (event->orientation == WL_POINTER_AXIS_VERTICAL_SCROLL)
			zoomcanvasby(m, canvas_zoom_factor(config.zoom_step, event->delta));
		return;
	}
	if (!locked && ISCANVAS(m) && !surface && !c) {
		if (wheel && event->orientation == WL_POINTER_AXIS_VERTICAL_SCROLL) {
			zoomcanvasby(m, canvas_zoom_factor(config.zoom_step, event->delta));
			return;
		}
		if (event->orientation == WL_POINTER_AXIS_HORIZONTAL_SCROLL)
			dx = canvas_axis_pan_delta(event->delta, config.pan_speed);
		else
			dy = canvas_axis_pan_delta(event->delta, config.pan_speed);
		pancanvas(m, dx, dy);
		motionnotify(0, NULL, 0, 0, 0, 0);
		return;
	}
	/* Notify the client with pointer focus of the axis event. */
	wlr_seat_pointer_notify_axis(seat,
			event->time_msec, event->orientation, event->delta,
			event->delta_discrete, event->source, event->relative_direction);
}

void
buttonpress(struct wl_listener *listener, void *data)
{
	struct wlr_pointer_button_event *event = data;
	struct wlr_keyboard *keyboard;
	struct wlr_surface *surface = NULL;
	uint32_t mods;
	Client *c = NULL;
	const Button *b;

	wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);

	switch (event->state) {
	case WL_POINTER_BUTTON_STATE_PRESSED:
		kb_group->release_armed = 0;
		cursor_mode = CurPressed;
		selmon = xytomon(cursor->x, cursor->y);
		if (locked)
			break;

		/* Change focus if the button was _pressed_ over a client */
		xytonode(cursor->x, cursor->y, &surface, &c, NULL, NULL, NULL);
		if (c && (!client_is_unmanaged(c) || client_wants_focus(c)))
			focusclient(c, 1);

		keyboard = wlr_seat_get_keyboard(seat);
		mods = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;
		for (b = config.buttons; b < config.buttons + config.button_count; b++) {
			if (CLEANMASK(mods) == CLEANMASK(b->mod) &&
					event->button == b->button && b->func) {
				b->func(&b->arg);
				return;
			}
		}
		if (c && c->iscollapsed && event->button == BTN_LEFT) {
			setcollapsed(c, 0);
			cursor_mode = CurConsumed;
			return;
		}
		if (event->button == BTN_LEFT && !surface && !c && ISCANVAS(selmon)) {
			startpan(NULL);
			return;
		}
		break;
	case WL_POINTER_BUTTON_STATE_RELEASED:
		/* If you released any buttons, we exit interactive move/resize mode. */
		/* TODO: should reset to the pointer focus's current setcursor */
		if (!locked && (cursor_mode == CurMove || cursor_mode == CurResize)) {
			Client *settled = grabc;

			wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");
			cursor_mode = CurNormal;
			/* Drop the window off on its new monitor */
			selmon = xytomon(cursor->x, cursor->y);
			if (grabc)
				setmon(grabc, selmon, 0);
			grabc = NULL;
			if (settled)
				clientsettle(settled);
			return;
		}
		if (!locked && cursor_mode == CurPan) {
			wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");
			cursor_mode = CurNormal;
			motionnotify(0, NULL, 0, 0, 0, 0);
			return;
		}
		if (cursor_mode == CurConsumed) {
			cursor_mode = CurNormal;
			return;
		}
		cursor_mode = CurNormal;
		break;
	}
	/* If the event wasn't handled by the compositor, notify the client with
	 * pointer focus that a button press has occurred */
	wlr_seat_pointer_notify_button(seat,
			event->time_msec, event->button, event->state);
}

void
chvt(const Arg *arg)
{
	wlr_session_change_vt(session, arg->ui);
}

void
createkeyboard(struct wlr_keyboard *keyboard)
{
	/* Set the keymap to match the group keymap */
	wlr_keyboard_set_keymap(keyboard, kb_group->wlr_group->keyboard.keymap);

	/* Add the new keyboard to the group */
	wlr_keyboard_group_add_keyboard(kb_group->wlr_group, keyboard);
}

KeyboardGroup *
createkeyboardgroup(void)
{
	KeyboardGroup *group = ecalloc(1, sizeof(*group));
	struct xkb_context *context;
	struct xkb_keymap *keymap;

	group->wlr_group = wlr_keyboard_group_create();
	group->wlr_group->data = group;

	/* Prepare an XKB keymap and assign it to the keyboard group. */
	context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
		if (!(keymap = xkb_keymap_new_from_names(context, &config.xkb_rules,
				XKB_KEYMAP_COMPILE_NO_FLAGS)))
		die("failed to compile keymap");

	wlr_keyboard_set_keymap(&group->wlr_group->keyboard, keymap);
	xkb_keymap_unref(keymap);
	xkb_context_unref(context);

	wlr_keyboard_set_repeat_info(&group->wlr_group->keyboard,
			config.repeat_rate, config.repeat_delay);

	/* Set up listeners for keyboard events */
	LISTEN(&group->wlr_group->keyboard.events.key, &group->key, keypress);
	LISTEN(&group->wlr_group->keyboard.events.modifiers, &group->modifiers, keypressmod);

	group->key_repeat_source = wl_event_loop_add_timer(event_loop, keyrepeat, group);

	/* A seat can only have one keyboard, but this is a limitation of the
	 * Wayland protocol - not wlroots. We assign all connected keyboards to the
	 * same wlr_keyboard_group, which provides a single wlr_keyboard interface for
	 * all of them. Set this combined wlr_keyboard as the seat keyboard.
	 */
	wlr_seat_set_keyboard(seat, &group->wlr_group->keyboard);
	return group;
}

void
createpointer(struct wlr_pointer *pointer)
{
	struct libinput_device *device;
	if (wlr_input_device_is_libinput(&pointer->base)
			&& (device = wlr_libinput_get_device_handle(&pointer->base))) {

		if (libinput_device_config_tap_get_finger_count(device)) {
			libinput_device_config_tap_set_enabled(device, config.tap_to_click);
			libinput_device_config_tap_set_drag_enabled(device, config.tap_and_drag);
			libinput_device_config_tap_set_drag_lock_enabled(device, config.drag_lock);
			libinput_device_config_tap_set_button_map(device, config.button_map);
		}

		if (libinput_device_config_scroll_has_natural_scroll(device))
			libinput_device_config_scroll_set_natural_scroll_enabled(device,
					config.natural_scrolling);

		if (libinput_device_config_dwt_is_available(device))
			libinput_device_config_dwt_set_enabled(device, config.disable_while_typing);

		if (libinput_device_config_left_handed_is_available(device))
			libinput_device_config_left_handed_set(device, config.left_handed);

		if (libinput_device_config_middle_emulation_is_available(device))
			libinput_device_config_middle_emulation_set_enabled(device,
					config.middle_button_emulation);

		if (libinput_device_config_scroll_get_methods(device) != LIBINPUT_CONFIG_SCROLL_NO_SCROLL)
			libinput_device_config_scroll_set_method(device, config.scroll_method);

		if (libinput_device_config_click_get_methods(device) != LIBINPUT_CONFIG_CLICK_METHOD_NONE)
			libinput_device_config_click_set_method(device, config.click_method);

		if (libinput_device_config_send_events_get_modes(device))
			libinput_device_config_send_events_set_mode(device, config.send_events_mode);

		if (libinput_device_config_accel_is_available(device)) {
			libinput_device_config_accel_set_profile(device, config.accel_profile);
			libinput_device_config_accel_set_speed(device, config.accel_speed);
		}
	}

	wlr_cursor_attach_input_device(cursor, &pointer->base);
}

void
createpointerconstraint(struct wl_listener *listener, void *data)
{
	PointerConstraint *pointer_constraint = ecalloc(1, sizeof(*pointer_constraint));
	pointer_constraint->constraint = data;
	LISTEN(&pointer_constraint->constraint->events.destroy,
			&pointer_constraint->destroy, destroypointerconstraint);
}

void
cursorconstrain(struct wlr_pointer_constraint_v1 *constraint)
{
	if (active_constraint == constraint)
		return;

	if (active_constraint)
		wlr_pointer_constraint_v1_send_deactivated(active_constraint);

	active_constraint = constraint;
	wlr_pointer_constraint_v1_send_activated(constraint);
}

void
cursorframe(struct wl_listener *listener, void *data)
{
	/* This event is forwarded by the cursor when a pointer emits a frame
	 * event. Frame events are sent after regular pointer events to group
	 * multiple events together. For instance, two axis events may happen at the
	 * same time, in which case a frame event won't be sent in between. */
	/* Notify the client with pointer focus of the frame event. */
	wlr_seat_pointer_notify_frame(seat);
}

void
cursorwarptohint(void)
{
	Client *c = NULL;
	double sx = active_constraint->current.cursor_hint.x;
	double sy = active_constraint->current.cursor_hint.y;
	double x, y;

	toplevel_from_wlr_surface(active_constraint->surface, &c, NULL);
	if (c && active_constraint->current.cursor_hint.enabled) {
		x = sx + c->geom.x + c->bw;
		y = sy + c->geom.y + c->bw;
		if (c->mon && ISCANVAS(c->mon) && !c->isfullscreen
				&& !client_is_unmanaged(c))
			canvaspointtoscreen(c->mon, x, y, &x, &y);
		wlr_cursor_warp(cursor, NULL, x, y);
		wlr_seat_pointer_warp(active_constraint->seat, sx, sy);
	}
}

void
destroypointerconstraint(struct wl_listener *listener, void *data)
{
	PointerConstraint *pointer_constraint = wl_container_of(listener, pointer_constraint, destroy);

	if (active_constraint == pointer_constraint->constraint) {
		cursorwarptohint();
		active_constraint = NULL;
	}

	wl_list_remove(&pointer_constraint->destroy.link);
	free(pointer_constraint);
}

void
destroykeyboardgroup(struct wl_listener *listener, void *data)
{
	KeyboardGroup *group = wl_container_of(listener, group, destroy);
	wl_event_source_remove(group->key_repeat_source);
	wl_list_remove(&group->key.link);
	wl_list_remove(&group->modifiers.link);
	wl_list_remove(&group->destroy.link);
	wlr_keyboard_group_destroy(group->wlr_group);
	free(group);
}

void
inputdevice(struct wl_listener *listener, void *data)
{
	/* This event is raised by the backend when a new input device becomes
	 * available. */
	struct wlr_input_device *device = data;
	uint32_t caps;

	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		createkeyboard(wlr_keyboard_from_input_device(device));
		break;
	case WLR_INPUT_DEVICE_POINTER:
		createpointer(wlr_pointer_from_input_device(device));
		break;
	default:
		/* TODO handle other input device types */
		break;
	}

	/* We need to let the wlr_seat know what our capabilities are, which is
	 * communiciated to the client. In Inca! we always have a cursor, even if
	 * there are no pointer devices, so we always include that capability. */
	/* TODO do we actually require a cursor? */
	caps = WL_SEAT_CAPABILITY_POINTER;
	if (!wl_list_empty(&kb_group->wlr_group->devices))
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	wlr_seat_set_capabilities(seat, caps);
}

int
keybinding(uint32_t mods, xkb_keysym_t sym, int on_release, int run)
{
	/*
	 * Here we handle compositor keybindings. This is when the compositor is
	 * processing keys, rather than passing them on to the client for its own
	 * processing.
	 */
	const Key *k;
	for (k = config.keys; k < config.keys + config.key_count; k++) {
		if (CLEANMASK(mods) == CLEANMASK(k->mod)
				&& xkb_keysym_to_lower(sym) == xkb_keysym_to_lower(k->keysym)
				&& k->on_release == on_release
				&& k->func) {
			if (run)
				k->func(&k->arg);
			return 1;
		}
	}
	return 0;
}

uint32_t
keymod(xkb_keysym_t sym)
{
	switch (sym) {
	case XKB_KEY_Shift_L:
	case XKB_KEY_Shift_R:
		return WLR_MODIFIER_SHIFT;
	case XKB_KEY_Control_L:
	case XKB_KEY_Control_R:
		return WLR_MODIFIER_CTRL;
	case XKB_KEY_Alt_L:
	case XKB_KEY_Alt_R:
		return WLR_MODIFIER_ALT;
	case XKB_KEY_Super_L:
	case XKB_KEY_Super_R:
		return WLR_MODIFIER_LOGO;
	default:
		return 0;
	}
}

void
keypress(struct wl_listener *listener, void *data)
{
	int i;
	/* This event is raised when a key is pressed or released. */
	KeyboardGroup *group = wl_container_of(listener, group, key);
	struct wlr_keyboard_key_event *event = data;

	/* Translate libinput keycode -> xkbcommon */
	uint32_t keycode = event->keycode + 8;
	/* Get a list of keysyms based on the keymap for this keyboard */
	const xkb_keysym_t *syms;
	int nsyms = xkb_state_key_get_syms(
			group->wlr_group->keyboard.xkb_state, keycode, &syms);

	int handled = 0, matched, repeatable = 0;
	int pressed = event->state == WL_KEYBOARD_KEY_STATE_PRESSED;
	uint32_t mods = wlr_keyboard_get_modifiers(&group->wlr_group->keyboard);

	wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);

	/* Arm release bindings only while their key is used alone. */
	if (!locked) {
		if (pressed && group->release_armed
				&& group->release_keycode != event->keycode)
			group->release_armed = 0;
		for (i = 0; i < nsyms; i++) {
			if (pressed) {
				matched = keybinding(mods, syms[i], 0, 1);
				handled = matched || handled;
				repeatable = matched || repeatable;
				if (keybinding(mods & ~keymod(syms[i]), syms[i], 1, 0)) {
					group->release_keycode = event->keycode;
					group->release_armed = 1;
					handled = 1;
				}
			} else if (group->release_armed
					&& group->release_keycode == event->keycode) {
				handled = keybinding(mods & ~keymod(syms[i]),
						syms[i], 1, 1) || handled;
			}
		}
	}
	if (!pressed && group->release_keycode == event->keycode)
		group->release_armed = 0;

	if (repeatable
			&& group->wlr_group->keyboard.repeat_info.delay > 0) {
		group->mods = mods;
		group->keysyms = syms;
		group->nsyms = nsyms;
		wl_event_source_timer_update(group->key_repeat_source,
				group->wlr_group->keyboard.repeat_info.delay);
	} else {
		group->nsyms = 0;
		wl_event_source_timer_update(group->key_repeat_source, 0);
	}

	if (handled)
		return;

	wlr_seat_set_keyboard(seat, &group->wlr_group->keyboard);
	/* Pass unhandled keycodes along to the client. */
	wlr_seat_keyboard_notify_key(seat, event->time_msec,
			event->keycode, event->state);
}

void
keypressmod(struct wl_listener *listener, void *data)
{
	/* This event is raised when a modifier key, such as shift or alt, is
	 * pressed. We simply communicate this to the client. */
	KeyboardGroup *group = wl_container_of(listener, group, modifiers);

	wlr_seat_set_keyboard(seat, &group->wlr_group->keyboard);
	/* Send modifiers to the client. */
	wlr_seat_keyboard_notify_modifiers(seat,
			&group->wlr_group->keyboard.modifiers);
}

int
keyrepeat(void *data)
{
	KeyboardGroup *group = data;
	int i;
	if (!group->nsyms || group->wlr_group->keyboard.repeat_info.rate <= 0)
		return 0;

	wl_event_source_timer_update(group->key_repeat_source,
			1000 / group->wlr_group->keyboard.repeat_info.rate);

	for (i = 0; i < group->nsyms; i++)
		keybinding(group->mods, group->keysyms[i], 0, 1);

	return 0;
}

void
motionabsolute(struct wl_listener *listener, void *data)
{
	/* This event is forwarded by the cursor when a pointer emits an _absolute_
	 * motion event, from 0..1 on each axis. This happens, for example, when
	 * wlroots is running under a Wayland window rather than KMS+DRM, and you
	 * move the mouse over the window. You could enter the window from any edge,
	 * so we have to warp the mouse there. Also, some hardware emits these events. */
	struct wlr_pointer_motion_absolute_event *event = data;
	double lx, ly, dx, dy;

	if (!event->time_msec) /* this is 0 with virtual pointers */
		wlr_cursor_warp_absolute(cursor, &event->pointer->base, event->x, event->y);

	wlr_cursor_absolute_to_layout_coords(cursor, &event->pointer->base, event->x, event->y, &lx, &ly);
	dx = lx - cursor->x;
	dy = ly - cursor->y;
	motionnotify(event->time_msec, &event->pointer->base, dx, dy, dx, dy);
}

void
motionnotify(uint32_t time, struct wlr_input_device *device, double dx, double dy,
		double dx_unaccel, double dy_unaccel)
{
	double sx = 0, sy = 0, sx_confined, sy_confined;
	double x, y, scale, logical_dx, logical_dy;
	Client *c = NULL, *w = NULL;
	LayerSurface *l = NULL;
	struct wlr_surface *surface = NULL;
	struct wlr_pointer_constraint_v1 *constraint;

	/* Find the client under the pointer and send the event along. */
	xytonode(cursor->x, cursor->y, &surface, &c, NULL, &sx, &sy);

	if (cursor_mode == CurPressed && !seat->drag
			&& surface != seat->pointer_state.focused_surface
			&& toplevel_from_wlr_surface(seat->pointer_state.focused_surface, &w, &l) >= 0) {
		c = w;
		surface = seat->pointer_state.focused_surface;
		if (l) {
			sx = cursor->x - l->scene->node.x;
			sy = cursor->y - l->scene->node.y;
		} else {
			x = cursor->x;
			y = cursor->y;
			if (w->mon && ISCANVAS(w->mon) && !w->isfullscreen
					&& !client_is_unmanaged(w))
				canvaspointtoworld(w->mon, x, y, &x, &y);
			sx = x - w->geom.x - w->bw;
			sy = y - w->geom.y - w->bw;
		}
	}

	/* time is 0 in internal calls meant to restore pointer focus. */
	if (time) {
		wlr_relative_pointer_manager_v1_send_relative_motion(
				relative_pointer_mgr, seat, (uint64_t)time * 1000,
				dx, dy, dx_unaccel, dy_unaccel);

		wl_list_for_each(constraint, &pointer_constraints->constraints, link)
			cursorconstrain(constraint);

		if (active_constraint && cursor_mode != CurResize && cursor_mode != CurMove
				&& cursor_mode != CurPan) {
			toplevel_from_wlr_surface(active_constraint->surface, &c, NULL);
			if (c && active_constraint->surface == seat->pointer_state.focused_surface) {
				x = cursor->x;
				y = cursor->y;
				if (c->mon && ISCANVAS(c->mon) && !c->isfullscreen
						&& !client_is_unmanaged(c))
					canvaspointtoworld(c->mon, x, y, &x, &y);
				sx = x - c->geom.x - c->bw;
				sy = y - c->geom.y - c->bw;
				scale = clientcanvasscale(c);
				logical_dx = dx / scale;
				logical_dy = dy / scale;
				if (wlr_region_confine(&active_constraint->region, sx, sy,
						sx + logical_dx, sy + logical_dy,
						&sx_confined, &sy_confined)) {
					dx = (sx_confined - sx) * scale;
					dy = (sy_confined - sy) * scale;
				}

				if (active_constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED)
					return;
			}
		}

		wlr_cursor_move(cursor, device, dx, dy);
		wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);

		/* Update selmon (even while dragging a window) */
		if (config.sloppyfocus)
			selmon = xytomon(cursor->x, cursor->y);
	}

	/* Update drag icon's position */
	wlr_scene_node_set_position(&drag_icon->node, (int)round(cursor->x), (int)round(cursor->y));

	/* If we are currently grabbing the mouse, handle and return */
	if (cursor_mode == CurMove) {
		struct wlr_box geo;

		/* Move the grabbed client to the new position. */
		canvaspointtoworld(grabc->mon, cursor->x, cursor->y, &x, &y);
		geo = (struct wlr_box){.x = (int)round(x - grabcx), .y = (int)round(y - grabcy),
			.width = grabc->geom.width, .height = grabc->geom.height};
		clientsnap(grabc, &geo);
		resize(grabc, geo, 1);
		wlr_output_schedule_frame(grabc->mon->wlr_output);
		return;
	} else if (cursor_mode == CurResize) {
		canvaspointtoworld(grabc->mon, cursor->x, cursor->y, &x, &y);
		resize(grabc, (struct wlr_box){.x = grabc->geom.x, .y = grabc->geom.y,
			.width = (int)round(x) - grabc->geom.x,
			.height = (int)round(y) - grabc->geom.y}, 1);
		return;
	} else if (cursor_mode == CurPan) {
		pancanvas(selmon, dx, dy);
		return;
	}

	/* If there's no client surface under the cursor, set the cursor image to a
	 * default. This is what makes the cursor image appear when you move it
	 * off of a client or over its border. */
	if (!surface && !seat->drag)
		wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");

	pointerfocus(c, surface, sx, sy, time);
}

void
motionrelative(struct wl_listener *listener, void *data)
{
	/* This event is forwarded by the cursor when a pointer emits a _relative_
	 * pointer motion event (i.e. a delta) */
	struct wlr_pointer_motion_event *event = data;
	/* The cursor doesn't move unless we tell it to. The cursor automatically
	 * handles constraining the motion to the output layout, as well as any
	 * special configuration applied for the specific input device which
	 * generated the event. You can pass NULL for the device if you want to move
	 * the cursor around without any input. */
	motionnotify(event->time_msec, &event->pointer->base, event->delta_x, event->delta_y,
			event->unaccel_dx, event->unaccel_dy);
}

void
moveresize(const Arg *arg)
{
	double x, y;

	if (cursor_mode != CurNormal && cursor_mode != CurPressed)
		return;
	xytonode(cursor->x, cursor->y, NULL, &grabc, NULL, NULL, NULL);
	if (!grabc || client_is_unmanaged(grabc) || grabc->isfullscreen)
		return;

	/* Float the window and tell motionnotify to grab it */
	setfloating(grabc, 1);
	canvaspointtoworld(grabc->mon, cursor->x, cursor->y, &x, &y);
	switch (cursor_mode = arg->ui) {
	case CurMove:
		clock_gettime(CLOCK_MONOTONIC, &grabc->mon->canvas_pan_frame);
		grabcx = x - grabc->geom.x;
		grabcy = y - grabc->geom.y;
		wlr_cursor_set_xcursor(cursor, cursor_mgr, "all-scroll");
		break;
	case CurResize:
		/* Doesn't work for X11 output - the next absolute motion event
		 * returns the cursor to where it started */
		x = grabc->geom.x + grabc->geom.width;
		y = grabc->geom.y + grabc->geom.height;
		if (grabc->mon && ISCANVAS(grabc->mon))
			canvaspointtoscreen(grabc->mon, x, y, &x, &y);
		wlr_cursor_warp_closest(cursor, NULL, x, y);
		wlr_cursor_set_xcursor(cursor, cursor_mgr, "se-resize");
		break;
	}
	if (cursor_mode == CurMove)
		wlr_output_schedule_frame(grabc->mon->wlr_output);
}

void
startpan(const Arg *arg)
{
	Monitor *m;
	Client *focused;

	if (cursor_mode != CurNormal && cursor_mode != CurPressed)
		return;
	m = xytomon(cursor->x, cursor->y);
	if (!ISCANVAS(m) || ((focused = focustop(m)) && focused->isfullscreen))
		return;
	selmon = m;
	grabc = NULL;
	cursor_mode = CurPan;
	wlr_cursor_set_xcursor(cursor, cursor_mgr, "all-scroll");
}

void
pinchbegin(struct wl_listener *listener, void *data)
{
	struct wlr_pointer_pinch_begin_event *event = data;
	struct wlr_keyboard *keyboard;
	struct wlr_surface *surface = NULL;
	Client *c = NULL, *focused;
	Monitor *m = xytomon(cursor->x, cursor->y);
	uint32_t mods;

	xytonode(cursor->x, cursor->y, &surface, &c, NULL, NULL, NULL);
	keyboard = wlr_seat_get_keyboard(seat);
	mods = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;
	if (!locked && ISCANVAS(m)
			&& !((focused = focustop(m)) && focused->isfullscreen)
			&& (event->fingers == 3 || (!surface && !c)
				|| (CLEANMASK(mods) & WLR_MODIFIER_LOGO))) {
		kb_group->release_armed = 0;
		wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
		pinchmon = m;
		pinchzoom = m->canvas_zoom;
		m->canvas_zoom_target = m->canvas_zoom;
		return;
	}
	wlr_pointer_gestures_v1_send_pinch_begin(pointer_gestures, seat,
			event->time_msec, event->fingers);
}

void
pinchupdate(struct wl_listener *listener, void *data)
{
	struct wlr_pointer_pinch_update_event *event = data;
	double zoom;

	if (!pinchmon) {
		wlr_pointer_gestures_v1_send_pinch_update(pointer_gestures, seat,
				event->time_msec, event->dx, event->dy,
				event->scale, event->rotation);
		return;
	}
	zoom = canvas_clamp_zoom(config.zoom_min, config.zoom_max,
			pinchzoom * event->scale);
	setcanvaszoom(pinchmon, zoom);
	pinchmon->canvas_zoom_target = pinchmon->canvas_zoom;
	wlr_output_schedule_frame(pinchmon->wlr_output);
}

void
pinchend(struct wl_listener *listener, void *data)
{
	struct wlr_pointer_pinch_end_event *event = data;

	if (!pinchmon) {
		wlr_pointer_gestures_v1_send_pinch_end(pointer_gestures, seat,
				event->time_msec, event->cancelled);
		return;
	}
	pinchmon->canvas_zoom_target = pinchmon->canvas_zoom;
	pinchmon = NULL;
}

void
swipeupdate(struct wl_listener *listener, void *data)
{
	struct wlr_pointer_swipe_update_event *event = data;
	Monitor *m;

	if (locked || event->fingers != 3)
		return;
	kb_group->release_armed = 0;
	m = xytomon(cursor->x, cursor->y);
	wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
	pancanvas(m, canvas_pan_delta(event->dx, config.pan_speed),
			canvas_pan_delta(event->dy, config.pan_speed));
	motionnotify(0, NULL, 0, 0, 0, 0);
}

void
pointerfocus(Client *c, struct wlr_surface *surface, double sx, double sy,
		uint32_t time)
{
	struct timespec now;

	if (surface != seat->pointer_state.focused_surface &&
			config.sloppyfocus && time && c && !c->iscollapsed
			&& !client_is_unmanaged(c))
		focusclient(c, 0);

	/* If surface is NULL, clear pointer focus */
	if (!surface) {
		wlr_seat_pointer_notify_clear_focus(seat);
		return;
	}

	if (!time) {
		clock_gettime(CLOCK_MONOTONIC, &now);
		time = now.tv_sec * 1000 + now.tv_nsec / 1000000;
	}

	/* Let the client know that the mouse cursor has entered one
	 * of its surfaces, and make keyboard focus follow if desired.
	 * wlroots makes this a no-op if surface is already focused */
	wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
	wlr_seat_pointer_notify_motion(seat, time, sx, sy);
}

void
setcursor(struct wl_listener *listener, void *data)
{
	/* This event is raised by the seat when a client provides a cursor image */
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	/* If we're "grabbing" the cursor, don't use the client's image, we will
	 * restore it after "grabbing" sending a leave event, followed by a enter
	 * event, which will result in the client requesting set the cursor surface */
	if (cursor_mode != CurNormal && cursor_mode != CurPressed)
		return;
	/* This can be sent by any client, so we check to make sure this one
	 * actually has pointer focus first. If so, we can tell the cursor to
	 * use the provided surface as the cursor image. It will set the
	 * hardware cursor on the output that it's currently on and continue to
	 * do so as the cursor moves between outputs. */
	if (event->seat_client == seat->pointer_state.focused_client)
		wlr_cursor_set_surface(cursor, event->surface,
				event->hotspot_x, event->hotspot_y);
}

void
setcursorshape(struct wl_listener *listener, void *data)
{
	struct wlr_cursor_shape_manager_v1_request_set_shape_event *event = data;
	if (cursor_mode != CurNormal && cursor_mode != CurPressed)
		return;
	/* This can be sent by any client, so we check to make sure this one
	 * actually has pointer focus first. If so, we can tell the cursor to
	 * use the provided cursor shape. */
	if (event->seat_client == seat->pointer_state.focused_client)
		wlr_cursor_set_xcursor(cursor, cursor_mgr,
				wlr_cursor_shape_v1_name(event->shape));
}

void
virtualkeyboard(struct wl_listener *listener, void *data)
{
	struct wlr_virtual_keyboard_v1 *kb = data;
	/* virtual keyboards shouldn't share keyboard group */
	KeyboardGroup *group = createkeyboardgroup();
	/* Set the keymap to match the group keymap */
	wlr_keyboard_set_keymap(&kb->keyboard, group->wlr_group->keyboard.keymap);
	LISTEN(&kb->keyboard.base.events.destroy, &group->destroy, destroykeyboardgroup);

	/* Add the new keyboard to the group */
	wlr_keyboard_group_add_keyboard(group->wlr_group, &kb->keyboard);
}

void
virtualpointer(struct wl_listener *listener, void *data)
{
	struct wlr_virtual_pointer_v1_new_pointer_event *event = data;
	struct wlr_input_device *device = &event->new_pointer->pointer.base;

	wlr_cursor_attach_input_device(cursor, device);
	if (event->suggested_output)
		wlr_cursor_map_input_to_output(cursor, device, event->suggested_output);
}

void
xytonode(double x, double y, struct wlr_surface **psurface,
		Client **pc, LayerSurface **pl, double *nx, double *ny)
{
	struct wlr_scene_node *node, *pnode;
	struct wlr_surface *surface = NULL;
	Client *c = NULL;
	LayerSurface *l = NULL;
	int layer;

	for (layer = NUM_LAYERS - 1; !surface && layer >= 0; layer--) {
		if (!(node = wlr_scene_node_at(&layers[layer]->node, x, y, nx, ny)))
			continue;

		if (node->type == WLR_SCENE_NODE_BUFFER) {
			struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(
					wlr_scene_buffer_from_node(node));
			if (scene_surface)
				surface = scene_surface->surface;
		}
		/* Walk the tree to find a node that knows the client */
		for (pnode = node; pnode && !c; pnode = &pnode->parent->node)
			c = pnode->data;
		if (c && c->type == LayerShell) {
			c = NULL;
			l = pnode->data;
		}
		if (c || l)
			break;
	}

	if (psurface) *psurface = surface;
	if (pc) *pc = c;
	if (pl) *pl = l;
}
