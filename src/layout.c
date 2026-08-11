/* See LICENSE file for copyright and license details. */
/* Layout algorithms, tag operations, and layout-related commands. */

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
	motionnotify(0, NULL, 0, 0, 0, 0);
	checkidleinhibitor(NULL);
}

void
pancanvas(Monitor *m, double dx, double dy)
{
	Client *c, *focused;
	int move_x, move_y;

	if (!ISCANVAS(m) || !m->wlr_output->enabled)
		return;
	if ((focused = focustop(m)) && focused->isfullscreen)
		return;

	m->canvas_remainder_x += dx;
	m->canvas_remainder_y += dy;
	move_x = (int)m->canvas_remainder_x;
	move_y = (int)m->canvas_remainder_y;
	m->canvas_remainder_x -= move_x;
	m->canvas_remainder_y -= move_y;
	if (!move_x && !move_y)
		return;

	m->canvas_x += move_x;
	m->canvas_y += move_y;
	wl_list_for_each(c, &clients, link) {
		if (c->mon != m || client_is_unmanaged(c))
			continue;
		if (c->isfullscreen) {
			c->prev.x += move_x;
			c->prev.y += move_y;
			continue;
		}
		c->geom.x += move_x;
		c->geom.y += move_y;
		wlr_scene_node_set_position(&c->scene->node, c->geom.x, c->geom.y);
	}
}

void
homecanvas(const Arg *arg)
{
	Client *focused;
	int dx, dy;

	if (!ISCANVAS(selmon) || ((focused = focustop(selmon)) && focused->isfullscreen))
		return;
	dx = -selmon->canvas_x;
	dy = -selmon->canvas_y;
	selmon->canvas_remainder_x = 0;
	selmon->canvas_remainder_y = 0;
	pancanvas(selmon, dx, dy);
	motionnotify(0, NULL, 0, 0, 0, 0);
}

void
centercanvas(const Arg *arg)
{
	Client *c;
	int dx, dy;

	if (!ISCANVAS(selmon) || !(c = focustop(selmon)) || c->isfullscreen)
		return;
	dx = selmon->w.x + (selmon->w.width - c->geom.width) / 2 - c->geom.x;
	dy = selmon->w.y + (selmon->w.height - c->geom.height) / 2 - c->geom.y;
	pancanvas(selmon, dx, dy);
	motionnotify(0, NULL, 0, 0, 0, 0);
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
