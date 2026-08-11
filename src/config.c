/* Lua configuration, validation, and inotify-backed hot reload. */

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#ifndef DWL_SYSTEM_CONFIG
#define DWL_SYSTEM_CONFIG "/usr/local/share/dwl/config.lua"
#endif

typedef enum {
	ConfigArgNone,
	ConfigArgInt,
	ConfigArgUInt,
	ConfigArgFloat,
	ConfigArgLayout,
	ConfigArgCommand,
	ConfigArgDirection,
	ConfigArgMove,
} ConfigArg;

typedef void (*ConfigAction)(const Arg *);

Config config;

static int config_fd = -1;
static int config_watch = -1;
static struct wl_event_source *config_source;
static char *config_file;
static char *config_dir;
static char *config_name;

static char *
config_strdup(const char *value)
{
	char *copy;

	if (!(copy = strdup(value)))
		die("config: strdup:");
	return copy;
}

static void *
config_realloc(void *ptr, size_t size)
{
	void *result = realloc(ptr, size);

	if (size && !result)
		die("config: realloc:");
	return result;
}

static Layout *
config_find_layout(const Config *cfg, const char *name)
{
	size_t i;

	if (!name)
		return NULL;
	for (i = 0; i < cfg->layout_count; i++)
		if (!strcmp(cfg->layouts[i].name, name))
			return &cfg->layouts[i];
	return NULL;
}

static void
config_append_layout(Config *cfg, const char *name, const char *symbol,
		void (*arrange_fn)(Monitor *))
{
	cfg->layouts = config_realloc(cfg->layouts,
			(cfg->layout_count + 1) * sizeof(*cfg->layouts));
	cfg->layouts[cfg->layout_count++] = (Layout){
		.name = config_strdup(name),
		.symbol = config_strdup(symbol),
		.arrange = arrange_fn,
	};
}

static void
config_append_rule(Config *cfg, const char *id, const char *title,
		uint32_t tags, int floating, int monitor)
{
	cfg->rules = config_realloc(cfg->rules,
			(cfg->rule_count + 1) * sizeof(*cfg->rules));
	cfg->rules[cfg->rule_count++] = (Rule){
		.id = id ? config_strdup(id) : NULL,
		.title = title ? config_strdup(title) : NULL,
		.tags = tags,
		.isfloating = floating,
		.monitor = monitor,
	};
}

static void
config_append_monrule(Config *cfg, const char *name, float mfact, int nmaster,
		float scale, Layout *layout, enum wl_output_transform transform,
		int x, int y)
{
	cfg->monrules = config_realloc(cfg->monrules,
			(cfg->monrule_count + 1) * sizeof(*cfg->monrules));
	cfg->monrules[cfg->monrule_count++] = (MonitorRule){
		.name = name ? config_strdup(name) : NULL,
		.mfact = mfact,
		.nmaster = nmaster,
		.scale = scale,
		.lt = layout,
		.rr = transform,
		.x = x,
		.y = y,
	};
}

static void
config_append_key(Config *cfg, uint32_t mod, xkb_keysym_t keysym,
		ConfigAction action, Arg arg)
{
	cfg->keys = config_realloc(cfg->keys,
			(cfg->key_count + 1) * sizeof(*cfg->keys));
	cfg->keys[cfg->key_count++] = (Key){
		.mod = mod,
		.keysym = keysym,
		.func = action,
		.arg = arg,
	};
}

static void
config_append_button(Config *cfg, uint32_t mod, unsigned int button,
		ConfigAction action, Arg arg)
{
	cfg->buttons = config_realloc(cfg->buttons,
			(cfg->button_count + 1) * sizeof(*cfg->buttons));
	cfg->buttons[cfg->button_count++] = (Button){
		.mod = mod,
		.button = button,
		.func = action,
		.arg = arg,
	};
}

static char **
config_command(const char *command)
{
	char **argv = ecalloc(4, sizeof(*argv));

	argv[0] = config_strdup("/bin/sh");
	argv[1] = config_strdup("-c");
	argv[2] = config_strdup(command);
	return argv;
}

static char **
config_exec(const char *command)
{
	char **argv = ecalloc(2, sizeof(*argv));

	argv[0] = config_strdup(command);
	return argv;
}

static void
config_free_argv(char **argv)
{
	int i;

	if (!argv)
		return;
	for (i = 0; argv[i]; i++)
		free(argv[i]);
	free(argv);
}

static void
config_free(Config *cfg)
{
	size_t i;

	for (i = 0; i < cfg->rule_count; i++) {
		free((char *)cfg->rules[i].id);
		free((char *)cfg->rules[i].title);
	}
	for (i = 0; i < cfg->layout_count; i++) {
		free(cfg->layouts[i].name);
		free((char *)cfg->layouts[i].symbol);
	}
	for (i = 0; i < cfg->monrule_count; i++)
		free((char *)cfg->monrules[i].name);
	for (i = 0; i < cfg->key_count; i++)
		if (cfg->keys[i].func == spawn)
			config_free_argv((char **)cfg->keys[i].arg.v);
	for (i = 0; i < cfg->button_count; i++)
		if (cfg->buttons[i].func == spawn)
			config_free_argv((char **)cfg->buttons[i].arg.v);
	free(cfg->rules);
	free(cfg->layouts);
	free(cfg->monrules);
	free(cfg->keys);
	free(cfg->buttons);
	free((char *)cfg->xkb_rules.rules);
	free((char *)cfg->xkb_rules.model);
	free((char *)cfg->xkb_rules.layout);
	free((char *)cfg->xkb_rules.variant);
	free((char *)cfg->xkb_rules.options);
	memset(cfg, 0, sizeof(*cfg));
}

static ConfigAction
config_action(const char *name, ConfigArg *argtype)
{
	*argtype = ConfigArgNone;
	if (!strcmp(name, "spawn")) {
		*argtype = ConfigArgCommand;
		return spawn;
	}
	if (!strcmp(name, "focusstack")) {
		*argtype = ConfigArgInt;
		return focusstack;
	}
	if (!strcmp(name, "incnmaster")) {
		*argtype = ConfigArgInt;
		return incnmaster;
	}
	if (!strcmp(name, "setmfact")) {
		*argtype = ConfigArgFloat;
		return setmfact;
	}
	if (!strcmp(name, "setlayout")) {
		*argtype = ConfigArgLayout;
		return setlayout;
	}
	if (!strcmp(name, "focusmon")) {
		*argtype = ConfigArgDirection;
		return focusmon;
	}
	if (!strcmp(name, "tagmon")) {
		*argtype = ConfigArgDirection;
		return tagmon;
	}
	if (!strcmp(name, "chvt")) {
		*argtype = ConfigArgUInt;
		return chvt;
	}
	if (!strcmp(name, "moveresize")) {
		*argtype = ConfigArgMove;
		return moveresize;
	}
	if (!strcmp(name, "pan"))
		return startpan;
	if (!strcmp(name, "homecanvas"))
		return homecanvas;
	if (!strcmp(name, "centercanvas"))
		return centercanvas;
	if (!strcmp(name, "zoomcanvas")) {
		*argtype = ConfigArgFloat;
		return zoomcanvas;
	}
	if (!strcmp(name, "view")) {
		*argtype = ConfigArgUInt;
		return view;
	}
	if (!strcmp(name, "toggleview")) {
		*argtype = ConfigArgUInt;
		return toggleview;
	}
	if (!strcmp(name, "tag")) {
		*argtype = ConfigArgUInt;
		return tag;
	}
	if (!strcmp(name, "toggletag")) {
		*argtype = ConfigArgUInt;
		return toggletag;
	}
	if (!strcmp(name, "quit"))
		return quit;
	if (!strcmp(name, "killclient"))
		return killclient;
	if (!strcmp(name, "zoom"))
		return zoom;
	if (!strcmp(name, "togglefloating"))
		return togglefloating;
	if (!strcmp(name, "togglefullscreen"))
		return togglefullscreen;
	return NULL;
}

static uint32_t
config_modifier(const char *name)
{
	if (!strcasecmp(name, "ALT") || !strcasecmp(name, "MOD"))
		return WLR_MODIFIER_ALT;
	if (!strcasecmp(name, "CTRL") || !strcasecmp(name, "CONTROL"))
		return WLR_MODIFIER_CTRL;
	if (!strcasecmp(name, "SHIFT"))
		return WLR_MODIFIER_SHIFT;
	if (!strcasecmp(name, "LOGO") || !strcasecmp(name, "SUPER")
			|| !strcasecmp(name, "META"))
		return WLR_MODIFIER_LOGO;
	return 0;
}

static uint32_t
config_mods(lua_State *lua, int index)
{
	uint32_t mods = 0;
	const char *name;
	size_t i, length;

	lua_getfield(lua, index, "mods");
	if (lua_isnil(lua, -1)) {
		lua_pop(lua, 1);
		return 0;
	}
	if (lua_isstring(lua, -1)) {
		name = lua_tostring(lua, -1);
		mods = config_modifier(name);
		if (!mods)
			luaL_error(lua, "unknown modifier '%s'", name);
	} else if (lua_istable(lua, -1)) {
		length = lua_rawlen(lua, -1);
		for (i = 1; i <= length; i++) {
			lua_rawgeti(lua, -1, (lua_Integer)i);
			name = luaL_checkstring(lua, -1);
			mods |= config_modifier(name);
			lua_pop(lua, 1);
		}
	} else {
		luaL_error(lua, "mods must be a string or an array");
	}
	lua_pop(lua, 1);
	return mods;
}

static xkb_keysym_t
config_keysym(lua_State *lua, int index)
{
	const char *name;
	xkb_keysym_t keysym;

	lua_getfield(lua, index, "key");
	name = luaL_checkstring(lua, -1);
	keysym = xkb_keysym_from_name(name, XKB_KEYSYM_CASE_INSENSITIVE);
	lua_pop(lua, 1);
	if (keysym == XKB_KEY_NoSymbol)
		luaL_error(lua, "unknown key '%s'", name);
	return keysym;
}

static int
config_direction(lua_State *lua, int index)
{
	const char *direction;

	lua_getfield(lua, index, "value");
	if (lua_isnil(lua, -1)) {
		lua_pop(lua, 1);
		lua_getfield(lua, index, "direction");
	}
	direction = luaL_checkstring(lua, -1);
	lua_pop(lua, 1);
	if (!strcasecmp(direction, "left"))
		return WLR_DIRECTION_LEFT;
	if (!strcasecmp(direction, "right"))
		return WLR_DIRECTION_RIGHT;
	if (!strcasecmp(direction, "up"))
		return WLR_DIRECTION_UP;
	if (!strcasecmp(direction, "down"))
		return WLR_DIRECTION_DOWN;
	luaL_error(lua, "unknown direction '%s'", direction);
	return 0;
}

static char **
config_lua_command(lua_State *lua, int index)
{
	char **argv;
	const char *value;
	size_t i, length;

	lua_getfield(lua, index, "command");
	if (lua_isstring(lua, -1)) {
		value = lua_tostring(lua, -1);
		argv = config_command(value);
		lua_pop(lua, 1);
		return argv;
	}
	if (!lua_istable(lua, -1))
		luaL_error(lua, "spawn binding requires command = string or array");
	length = lua_rawlen(lua, -1);
	if (!length)
		luaL_error(lua, "spawn command cannot be empty");
	argv = ecalloc(length + 1, sizeof(*argv));
	for (i = 1; i <= length; i++) {
		lua_rawgeti(lua, -1, (lua_Integer)i);
		value = luaL_checkstring(lua, -1);
		argv[i - 1] = config_strdup(value);
		lua_pop(lua, 1);
	}
	lua_pop(lua, 1);
	return argv;
}

static void
config_binding_arg(lua_State *lua, Config *cfg, int index, ConfigArg argtype, Arg *arg)
{
	const char *value;
	Layout *layout;
	lua_Integer integer;

	*arg = (Arg){0};
	switch (argtype) {
	case ConfigArgNone:
		return;
	case ConfigArgCommand:
		arg->v = config_lua_command(lua, index);
		return;
	case ConfigArgLayout:
		lua_getfield(lua, index, "layout");
		if (lua_isnil(lua, -1)) {
			lua_pop(lua, 1);
			return;
		}
		value = luaL_checkstring(lua, -1);
		layout = config_find_layout(cfg, value);
		lua_pop(lua, 1);
		if (!layout)
			luaL_error(lua, "unknown layout '%s'", value);
		arg->v = layout;
		return;
	case ConfigArgDirection:
		arg->i = config_direction(lua, index);
		return;
	case ConfigArgMove:
		lua_getfield(lua, index, "value");
		value = luaL_checkstring(lua, -1);
		lua_pop(lua, 1);
		if (!strcasecmp(value, "move"))
			arg->ui = CurMove;
		else if (!strcasecmp(value, "resize"))
			arg->ui = CurResize;
		else
			luaL_error(lua, "moveresize value must be 'move' or 'resize'");
		return;
	case ConfigArgInt:
		lua_getfield(lua, index, "value");
		integer = luaL_checkinteger(lua, -1);
		lua_pop(lua, 1);
		if (integer < INT_MIN || integer > INT_MAX)
			luaL_error(lua, "binding integer is out of range");
		arg->i = (int)integer;
		return;
	case ConfigArgUInt:
		lua_getfield(lua, index, "value");
		integer = luaL_optinteger(lua, -1, 0);
		lua_pop(lua, 1);
		if (integer < 0 || (lua_Unsigned)integer > UINT_MAX)
			luaL_error(lua, "binding unsigned integer is out of range");
		arg->ui = (uint32_t)integer;
		return;
	case ConfigArgFloat:
		lua_getfield(lua, index, "value");
		arg->f = (float)luaL_checknumber(lua, -1);
		lua_pop(lua, 1);
		return;
	}
}

static void
config_parse_key(lua_State *lua, Config *cfg, int index, int button)
{
	const char *action_name;
	ConfigAction action;
	ConfigArg argtype;
	Arg arg;
	unsigned int button_code = 0;
	xkb_keysym_t keysym;
	uint32_t mods;
	lua_Integer integer;

	luaL_checktype(lua, index, LUA_TTABLE);
	mods = config_mods(lua, index);
	lua_getfield(lua, index, "action");
	action_name = luaL_checkstring(lua, -1);
	action = config_action(action_name, &argtype);
	lua_pop(lua, 1);
	if (!action)
		luaL_error(lua, "unknown action '%s'", action_name);
	config_binding_arg(lua, cfg, index, argtype, &arg);
	if (button) {
		lua_getfield(lua, index, "button");
		if (lua_isstring(lua, -1)) {
			const char *name = lua_tostring(lua, -1);
			if (!strcasecmp(name, "left"))
				button_code = BTN_LEFT;
			else if (!strcasecmp(name, "middle"))
				button_code = BTN_MIDDLE;
			else if (!strcasecmp(name, "right"))
				button_code = BTN_RIGHT;
			else
				luaL_error(lua, "unknown mouse button '%s'", name);
		} else {
			integer = luaL_checkinteger(lua, -1);
			if (integer < 0 || (lua_Unsigned)integer > UINT_MAX)
				luaL_error(lua, "mouse button is out of range");
			button_code = (unsigned int)integer;
		}
		lua_pop(lua, 1);
		config_append_button(cfg, mods, button_code, action, arg);
	} else {
		keysym = config_keysym(lua, index);
		config_append_key(cfg, mods, keysym, action, arg);
	}
}

static void
config_parse_color(lua_State *lua, int index, float color[4], const char *name)
{
	const char *value;
	char *end;
	unsigned long rgba = 0;
	size_t i;

	if (lua_isstring(lua, index)) {
		value = lua_tostring(lua, index);
		if (value[0] == '#')
			value++;
		if (strlen(value) == 6)
			rgba = strtoul(value, &end, 16) << 8 | 0xff;
		else if (strlen(value) == 8)
			rgba = strtoul(value, &end, 16);
		else
			luaL_error(lua, "%s must be #RRGGBB or #RRGGBBAA", name);
		if (*value == '\0' || *end != '\0' || rgba > 0xffffffffUL)
			luaL_error(lua, "invalid %s color", name);
		for (i = 0; i < 4; i++)
			color[i] = (float)((rgba >> (24 - i * 8)) & 0xff) / 255.0f;
		return;
	}
	if (!lua_istable(lua, index))
		luaL_error(lua, "%s must be a color string or array", name);
	if (lua_rawlen(lua, index) != 4)
		luaL_error(lua, "%s array must contain four values", name);
	for (i = 1; i <= 4; i++) {
		lua_rawgeti(lua, index, (lua_Integer)i);
		color[i - 1] = (float)luaL_checknumber(lua, -1);
		lua_pop(lua, 1);
		if (color[i - 1] < 0.0f || color[i - 1] > 1.0f)
			luaL_error(lua, "%s color channels must be between 0 and 1", name);
	}
}

static void
config_color_field(lua_State *lua, int index, const char *name, float color[4])
{
	lua_getfield(lua, index, name);
	if (!lua_isnil(lua, -1))
		config_parse_color(lua, -1, color, name);
	lua_pop(lua, 1);
}

static int
config_bool_field(lua_State *lua, int index, const char *name, int current)
{
	int value;
	lua_getfield(lua, index, name);
	if (lua_isnil(lua, -1)) {
		lua_pop(lua, 1);
		return current;
	}
	if (!lua_isboolean(lua, -1))
		luaL_error(lua, "%s must be boolean", name);
	value = lua_toboolean(lua, -1);
	lua_pop(lua, 1);
	return value;
}

static int
config_int_field(lua_State *lua, int index, const char *name, int current)
{
	lua_Integer value;
	lua_getfield(lua, index, name);
	if (lua_isnil(lua, -1)) {
		lua_pop(lua, 1);
		return current;
	}
	value = luaL_checkinteger(lua, -1);
	lua_pop(lua, 1);
	if (value < INT_MIN || value > INT_MAX)
		luaL_error(lua, "%s is out of range", name);
	return (int)value;
}

static float
config_float_field(lua_State *lua, int index, const char *name, float current)
{
	float value;
	lua_getfield(lua, index, name);
	if (lua_isnil(lua, -1)) {
		lua_pop(lua, 1);
		return current;
	}
	value = (float)luaL_checknumber(lua, -1);
	lua_pop(lua, 1);
	return value;
}

static void
config_string_field(lua_State *lua, int index, const char *name, const char **dst)
{
	const char *value;
	lua_getfield(lua, index, name);
	if (!lua_isnil(lua, -1)) {
		value = luaL_checkstring(lua, -1);
		free((char *)*dst);
		*dst = config_strdup(value);
	}
	lua_pop(lua, 1);
}

static enum wl_output_transform
config_transform(lua_State *lua, int index)
{
	const char *value;

	if (lua_isnumber(lua, index)) {
		lua_Integer transform = luaL_checkinteger(lua, index);
		if (transform < WL_OUTPUT_TRANSFORM_NORMAL || transform > WL_OUTPUT_TRANSFORM_FLIPPED_270)
			luaL_error(lua, "monitor transform is out of range");
		return (enum wl_output_transform)transform;
	}
	value = luaL_checkstring(lua, index);
	if (!strcasecmp(value, "normal"))
		return WL_OUTPUT_TRANSFORM_NORMAL;
	if (!strcasecmp(value, "90") || !strcasecmp(value, "rotate90"))
		return WL_OUTPUT_TRANSFORM_90;
	if (!strcasecmp(value, "180") || !strcasecmp(value, "rotate180"))
		return WL_OUTPUT_TRANSFORM_180;
	if (!strcasecmp(value, "270") || !strcasecmp(value, "rotate270"))
		return WL_OUTPUT_TRANSFORM_270;
	if (!strcasecmp(value, "flipped"))
		return WL_OUTPUT_TRANSFORM_FLIPPED;
	if (!strcasecmp(value, "flipped90"))
		return WL_OUTPUT_TRANSFORM_FLIPPED_90;
	if (!strcasecmp(value, "flipped180"))
		return WL_OUTPUT_TRANSFORM_FLIPPED_180;
	if (!strcasecmp(value, "flipped270"))
		return WL_OUTPUT_TRANSFORM_FLIPPED_270;
	luaL_error(lua, "unknown monitor transform '%s'", value);
	return WL_OUTPUT_TRANSFORM_NORMAL;
}

static void
config_parse_appearance(lua_State *lua, Config *cfg)
{
	lua_getfield(lua, 1, "appearance");
	if (lua_isnil(lua, -1)) {
		lua_pop(lua, 1);
		return;
	}
	luaL_checktype(lua, -1, LUA_TTABLE);
	cfg->sloppyfocus = config_bool_field(lua, -1, "sloppyfocus", cfg->sloppyfocus);
	cfg->bypass_surface_visibility = config_bool_field(lua, -1,
			"bypass_surface_visibility", cfg->bypass_surface_visibility);
	cfg->borderpx = (unsigned int)config_int_field(lua, -1, "borderpx", (int)cfg->borderpx);
	if (cfg->borderpx > 64)
		luaL_error(lua, "borderpx must be between 0 and 64");
	config_color_field(lua, -1, "rootcolor", cfg->rootcolor);
	config_color_field(lua, -1, "bordercolor", cfg->bordercolor);
	config_color_field(lua, -1, "focuscolor", cfg->focuscolor);
	config_color_field(lua, -1, "urgentcolor", cfg->urgentcolor);
	config_color_field(lua, -1, "fullscreen_bg", cfg->fullscreen_bg);
	lua_pop(lua, 1);
}

static void
config_parse_canvas(lua_State *lua, Config *cfg)
{
	lua_getfield(lua, 1, "canvas");
	if (lua_isnil(lua, -1)) {
		lua_pop(lua, 1);
		return;
	}
	luaL_checktype(lua, -1, LUA_TTABLE);
	cfg->pan_speed = config_float_field(lua, -1, "pan_speed", cfg->pan_speed);
	if (cfg->pan_speed < -10.0f || cfg->pan_speed > 10.0f || cfg->pan_speed == 0.0f)
		luaL_error(lua, "canvas.pan_speed must be non-zero and between -10 and 10");
	cfg->zoom_min = config_float_field(lua, -1, "zoom_min", cfg->zoom_min);
	cfg->zoom_max = config_float_field(lua, -1, "zoom_max", cfg->zoom_max);
	cfg->zoom_step = config_float_field(lua, -1, "zoom_step", cfg->zoom_step);
	if (cfg->zoom_min < 0.1f || cfg->zoom_min > 1.0f)
		luaL_error(lua, "canvas.zoom_min must be between 0.1 and 1");
	if (cfg->zoom_max < 1.0f || cfg->zoom_max > 8.0f
			|| cfg->zoom_max < cfg->zoom_min)
		luaL_error(lua, "canvas.zoom_max must be between 1 and 8 and at least zoom_min");
	if (cfg->zoom_step <= 1.0f || cfg->zoom_step > 2.0f)
		luaL_error(lua, "canvas.zoom_step must be greater than 1 and at most 2");
	lua_pop(lua, 1);
}

static void
config_parse_logging(lua_State *lua, Config *cfg)
{
	const char *level;

	lua_getfield(lua, 1, "logging");
	if (lua_isnil(lua, -1)) {
		lua_pop(lua, 1);
		return;
	}
	if (lua_isstring(lua, -1))
		level = lua_tostring(lua, -1);
	else {
		luaL_checktype(lua, -1, LUA_TTABLE);
		lua_getfield(lua, -1, "level");
		level = luaL_checkstring(lua, -1);
		lua_remove(lua, -2);
	}
	if (!strcasecmp(level, "silent"))
		cfg->log_level = WLR_SILENT;
	else if (!strcasecmp(level, "error"))
		cfg->log_level = WLR_ERROR;
	else if (!strcasecmp(level, "info"))
		cfg->log_level = WLR_INFO;
	else if (!strcasecmp(level, "debug"))
		cfg->log_level = WLR_DEBUG;
	else
		luaL_error(lua, "unknown log level '%s'", level);
	lua_pop(lua, 1);
}

static void
config_clear_layouts(Config *cfg)
{
	size_t i;
	for (i = 0; i < cfg->layout_count; i++) {
		free(cfg->layouts[i].name);
		free((char *)cfg->layouts[i].symbol);
	}
	free(cfg->layouts);
	cfg->layouts = NULL;
	cfg->layout_count = 0;
}

static void
config_clear_rules(Config *cfg)
{
	size_t i;
	for (i = 0; i < cfg->rule_count; i++) {
		free((char *)cfg->rules[i].id);
		free((char *)cfg->rules[i].title);
	}
	free(cfg->rules);
	cfg->rules = NULL;
	cfg->rule_count = 0;
}

static void
config_clear_monrules(Config *cfg)
{
	size_t i;
	for (i = 0; i < cfg->monrule_count; i++)
		free((char *)cfg->monrules[i].name);
	free(cfg->monrules);
	cfg->monrules = NULL;
	cfg->monrule_count = 0;
}

static void
config_clear_keys(Config *cfg)
{
	size_t i;
	for (i = 0; i < cfg->key_count; i++)
		if (cfg->keys[i].func == spawn)
			config_free_argv((char **)cfg->keys[i].arg.v);
	free(cfg->keys);
	cfg->keys = NULL;
	cfg->key_count = 0;
}

static void
config_clear_buttons(Config *cfg)
{
	size_t i;
	for (i = 0; i < cfg->button_count; i++)
		if (cfg->buttons[i].func == spawn)
			config_free_argv((char **)cfg->buttons[i].arg.v);
	free(cfg->buttons);
	cfg->buttons = NULL;
	cfg->button_count = 0;
}

static void
config_parse_layouts(lua_State *lua, Config *cfg)
{
	size_t i, length;
	char **key_layouts, **mon_layouts;
	const char *name, *symbol, *arrange_name;
	void (*arrange_fn)(Monitor *) = NULL;

	lua_getfield(lua, 1, "layouts");
	if (lua_isnil(lua, -1)) {
		lua_pop(lua, 1);
		return;
	}
	luaL_checktype(lua, -1, LUA_TTABLE);
	length = lua_rawlen(lua, -1);
	if (!length)
		luaL_error(lua, "layouts cannot be empty");
	key_layouts = ecalloc(cfg->key_count, sizeof(*key_layouts));
	for (i = 0; i < cfg->key_count; i++)
		if (cfg->keys[i].func == setlayout && cfg->keys[i].arg.v)
			key_layouts[i] = config_strdup(((Layout *)cfg->keys[i].arg.v)->name);
	mon_layouts = ecalloc(cfg->monrule_count, sizeof(*mon_layouts));
	for (i = 0; i < cfg->monrule_count; i++)
		if (cfg->monrules[i].lt)
			mon_layouts[i] = config_strdup(cfg->monrules[i].lt->name);
	config_clear_layouts(cfg);
	for (i = 1; i <= length; i++) {
		lua_rawgeti(lua, -1, (lua_Integer)i);
		luaL_checktype(lua, -1, LUA_TTABLE);
		lua_getfield(lua, -1, "name");
		name = luaL_checkstring(lua, -1);
		lua_pop(lua, 1);
		lua_getfield(lua, -1, "symbol");
		symbol = luaL_checkstring(lua, -1);
		lua_pop(lua, 1);
		lua_getfield(lua, -1, "arrange");
		arrange_name = lua_isnil(lua, -1) ? "floating" : luaL_checkstring(lua, -1);
		if (!strcmp(arrange_name, "tile"))
			arrange_fn = tile;
		else if (!strcmp(arrange_name, "monocle"))
			arrange_fn = monocle;
		else if (!strcmp(arrange_name, "floating") || !strcmp(arrange_name, "canvas")
				|| !strcmp(arrange_name, "none"))
			arrange_fn = NULL;
		else
			luaL_error(lua, "unknown layout arrange function '%s'", arrange_name);
		lua_pop(lua, 1);
		config_append_layout(cfg, name, symbol, arrange_fn);
		lua_pop(lua, 1);
	}
	for (i = 0; i < cfg->key_count; i++) {
		if (key_layouts[i]) {
			cfg->keys[i].arg.v = config_find_layout(cfg, key_layouts[i]);
			if (!cfg->keys[i].arg.v)
				cfg->keys[i].arg.v = &cfg->layouts[0];
			free(key_layouts[i]);
		}
	}
	for (i = 0; i < cfg->monrule_count; i++) {
		if (mon_layouts[i]) {
			cfg->monrules[i].lt = config_find_layout(cfg, mon_layouts[i]);
			if (!cfg->monrules[i].lt)
				cfg->monrules[i].lt = &cfg->layouts[0];
			free(mon_layouts[i]);
		}
	}
	free(key_layouts);
	free(mon_layouts);
	lua_pop(lua, 1);
}

static void
config_parse_rules(lua_State *lua, Config *cfg)
{
	size_t i, length;
	const char *id, *title;
	int floating, monitor;
	lua_Integer tags, monitor_value;

	lua_getfield(lua, 1, "rules");
	if (lua_isnil(lua, -1)) {
		lua_pop(lua, 1);
		return;
	}
	luaL_checktype(lua, -1, LUA_TTABLE);
	config_clear_rules(cfg);
	length = lua_rawlen(lua, -1);
	for (i = 1; i <= length; i++) {
		lua_rawgeti(lua, -1, (lua_Integer)i);
		luaL_checktype(lua, -1, LUA_TTABLE);
		lua_getfield(lua, -1, "app_id");
		id = lua_isnil(lua, -1) ? NULL : luaL_checkstring(lua, -1);
		lua_pop(lua, 1);
		if (!id) {
			lua_getfield(lua, -1, "id");
			id = lua_isnil(lua, -1) ? NULL : luaL_checkstring(lua, -1);
			lua_pop(lua, 1);
		}
		lua_getfield(lua, -1, "title");
		title = lua_isnil(lua, -1) ? NULL : luaL_checkstring(lua, -1);
		lua_pop(lua, 1);
		lua_getfield(lua, -1, "tags");
		tags = lua_isnil(lua, -1) ? 0 : luaL_checkinteger(lua, -1);
		lua_pop(lua, 1);
		lua_getfield(lua, -1, "floating");
		if (lua_isnil(lua, -1))
			floating = 0;
		else {
			if (!lua_isboolean(lua, -1))
				luaL_error(lua, "floating must be boolean");
			floating = lua_toboolean(lua, -1);
		}
		lua_pop(lua, 1);
		lua_getfield(lua, -1, "monitor");
		if (lua_isnil(lua, -1))
			monitor = -1;
		else {
			monitor_value = luaL_checkinteger(lua, -1);
			if (monitor_value < -1 || monitor_value > INT_MAX)
				luaL_error(lua, "monitor index is out of range");
			monitor = (int)monitor_value;
		}
		lua_pop(lua, 1);
		if (tags < 0 || (lua_Unsigned)tags > UINT32_MAX)
			luaL_error(lua, "invalid client rule values");
		config_append_rule(cfg, id, title, (uint32_t)tags, floating, monitor);
		lua_pop(lua, 1);
	}
	lua_pop(lua, 1);
}

static void
config_parse_monrules(lua_State *lua, Config *cfg)
{
	size_t i, length;
	const char *name, *layout_name;
	Layout *layout;
	float mfact, scale;
	int nmaster, x, y;
	enum wl_output_transform transform;

	lua_getfield(lua, 1, "monitors");
	if (lua_isnil(lua, -1)) {
		lua_pop(lua, 1);
		lua_getfield(lua, 1, "monitor_rules");
	}
	if (lua_isnil(lua, -1)) {
		lua_pop(lua, 1);
		return;
	}
	luaL_checktype(lua, -1, LUA_TTABLE);
	length = lua_rawlen(lua, -1);
	if (!length)
		luaL_error(lua, "monitors cannot be empty");
	config_clear_monrules(cfg);
	for (i = 1; i <= length; i++) {
		lua_rawgeti(lua, -1, (lua_Integer)i);
		luaL_checktype(lua, -1, LUA_TTABLE);
		lua_getfield(lua, -1, "name");
		name = lua_isnil(lua, -1) ? NULL : luaL_checkstring(lua, -1);
		lua_pop(lua, 1);
		mfact = config_float_field(lua, -1, "mfact", 0.55f);
		nmaster = config_int_field(lua, -1, "nmaster", 1);
		scale = config_float_field(lua, -1, "scale", 1.0f);
		x = config_int_field(lua, -1, "x", -1);
		y = config_int_field(lua, -1, "y", -1);
		lua_getfield(lua, -1, "layout");
		layout_name = lua_isnil(lua, -1) ? cfg->layouts[0].name : luaL_checkstring(lua, -1);
		layout = config_find_layout(cfg, layout_name);
		lua_pop(lua, 1);
		if (!layout)
			luaL_error(lua, "unknown monitor layout '%s'", layout_name);
		lua_getfield(lua, -1, "transform");
		if (lua_isnil(lua, -1))
			transform = WL_OUTPUT_TRANSFORM_NORMAL;
		else
			transform = config_transform(lua, -1);
		lua_pop(lua, 1);
		if (mfact < 0.1f || mfact > 0.9f || nmaster < 0 || scale <= 0.0f
				|| (x < -1 || y < -1) || ((x == -1) != (y == -1)))
			luaL_error(lua, "invalid monitor rule values");
		config_append_monrule(cfg, name, mfact, nmaster, scale, layout,
				transform, x, y);
		lua_pop(lua, 1);
	}
	lua_pop(lua, 1);
}

static void
config_parse_keyboard(lua_State *lua, Config *cfg)
{
	const char *field;

	lua_getfield(lua, 1, "keyboard");
	if (lua_isnil(lua, -1)) {
		lua_pop(lua, 1);
		return;
	}
	luaL_checktype(lua, -1, LUA_TTABLE);
	cfg->repeat_rate = config_int_field(lua, -1, "repeat_rate", cfg->repeat_rate);
	cfg->repeat_delay = config_int_field(lua, -1, "repeat_delay", cfg->repeat_delay);
	if (cfg->repeat_rate < 0 || cfg->repeat_delay < 0)
		luaL_error(lua, "keyboard repeat values cannot be negative");
	lua_getfield(lua, -1, "rules");
	if (!lua_isnil(lua, -1)) {
		luaL_checktype(lua, -1, LUA_TTABLE);
		config_string_field(lua, -1, "rules", &cfg->xkb_rules.rules);
		config_string_field(lua, -1, "model", &cfg->xkb_rules.model);
		config_string_field(lua, -1, "layout", &cfg->xkb_rules.layout);
		config_string_field(lua, -1, "variant", &cfg->xkb_rules.variant);
		config_string_field(lua, -1, "options", &cfg->xkb_rules.options);
	}
	lua_pop(lua, 1);
	lua_getfield(lua, -1, "layout");
	if (!lua_isnil(lua, -1)) {
		field = luaL_checkstring(lua, -1);
		free((char *)cfg->xkb_rules.layout);
		cfg->xkb_rules.layout = config_strdup(field);
	}
	lua_pop(lua, 1);
	lua_pop(lua, 1);
}

static int
config_enum_field(lua_State *lua, int index, const char *name,
		const char *const names[], const int values[], size_t count, int current)
{
	const char *value;
	size_t i;
	lua_Integer integer;

	lua_getfield(lua, index, name);
	if (lua_isnil(lua, -1)) {
		lua_pop(lua, 1);
		return current;
	}
	if (lua_isnumber(lua, -1)) {
		integer = luaL_checkinteger(lua, -1);
		lua_pop(lua, 1);
		if (integer < INT_MIN || integer > INT_MAX)
			luaL_error(lua, "%s value is out of range", name);
		for (i = 0; i < count; i++)
			if ((int)integer == values[i])
				return (int)integer;
		luaL_error(lua, "invalid %s value", name);
		return current;
	}
	value = luaL_checkstring(lua, -1);
	for (i = 0; i < count; i++)
		if (!strcasecmp(value, names[i])) {
			lua_pop(lua, 1);
			return values[i];
		}
	luaL_error(lua, "unknown %s value '%s'", name, value);
	return current;
}

static void
config_parse_libinput(lua_State *lua, Config *cfg)
{
	static const char *const scroll_names[] = {"none", "2fg", "edge", "button"};
	static const int scroll_values[] = {LIBINPUT_CONFIG_SCROLL_NO_SCROLL,
		LIBINPUT_CONFIG_SCROLL_2FG, LIBINPUT_CONFIG_SCROLL_EDGE,
		LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN};
	static const char *const click_names[] = {"none", "button_areas", "clickfinger"};
	static const int click_values[] = {LIBINPUT_CONFIG_CLICK_METHOD_NONE,
		LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS, LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER};
	static const char *const send_names[] = {"enabled", "disabled", "disabled_on_external_mouse"};
	static const int send_values[] = {LIBINPUT_CONFIG_SEND_EVENTS_ENABLED,
		LIBINPUT_CONFIG_SEND_EVENTS_DISABLED,
		LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE};
	static const char *const accel_names[] = {"flat", "adaptive"};
	static const int accel_values[] = {LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT,
		LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE};
	static const char *const map_names[] = {"lrm", "lmr"};
	static const int map_values[] = {LIBINPUT_CONFIG_TAP_MAP_LRM, LIBINPUT_CONFIG_TAP_MAP_LMR};

	lua_getfield(lua, 1, "libinput");
	if (lua_isnil(lua, -1)) {
		lua_pop(lua, 1);
		return;
	}
	luaL_checktype(lua, -1, LUA_TTABLE);
	cfg->tap_to_click = config_bool_field(lua, -1, "tap_to_click", cfg->tap_to_click);
	cfg->tap_and_drag = config_bool_field(lua, -1, "tap_and_drag", cfg->tap_and_drag);
	cfg->drag_lock = config_bool_field(lua, -1, "drag_lock", cfg->drag_lock);
	cfg->natural_scrolling = config_bool_field(lua, -1, "natural_scrolling", cfg->natural_scrolling);
	cfg->disable_while_typing = config_bool_field(lua, -1, "disable_while_typing", cfg->disable_while_typing);
	cfg->left_handed = config_bool_field(lua, -1, "left_handed", cfg->left_handed);
	cfg->middle_button_emulation = config_bool_field(lua, -1,
			"middle_button_emulation", cfg->middle_button_emulation);
	cfg->scroll_method = (enum libinput_config_scroll_method)config_enum_field(lua, -1,
			"scroll_method", scroll_names, scroll_values, LENGTH(scroll_names), cfg->scroll_method);
	cfg->click_method = (enum libinput_config_click_method)config_enum_field(lua, -1,
			"click_method", click_names, click_values, LENGTH(click_names), cfg->click_method);
	cfg->send_events_mode = (uint32_t)config_enum_field(lua, -1,
			"send_events_mode", send_names, send_values, LENGTH(send_names), cfg->send_events_mode);
	cfg->accel_profile = (enum libinput_config_accel_profile)config_enum_field(lua, -1,
			"accel_profile", accel_names, accel_values, LENGTH(accel_names), cfg->accel_profile);
	cfg->accel_speed = config_float_field(lua, -1, "accel_speed", (float)cfg->accel_speed);
	cfg->button_map = (enum libinput_config_tap_button_map)config_enum_field(lua, -1,
			"button_map", map_names, map_values, LENGTH(map_names), cfg->button_map);
	if (cfg->accel_speed < -1.0 || cfg->accel_speed > 1.0)
		luaL_error(lua, "accel_speed must be between -1 and 1");
	lua_pop(lua, 1);
}

static void
config_parse_bindings(lua_State *lua, Config *cfg, const char *field, int button)
{
	size_t i, length;

	lua_getfield(lua, 1, field);
	if (lua_isnil(lua, -1)) {
		lua_pop(lua, 1);
		return;
	}
	luaL_checktype(lua, -1, LUA_TTABLE);
	if (button)
		config_clear_buttons(cfg);
	else
		config_clear_keys(cfg);
	length = lua_rawlen(lua, -1);
	for (i = 1; i <= length; i++) {
		lua_rawgeti(lua, -1, (lua_Integer)i);
		config_parse_key(lua, cfg, -1, button);
		lua_pop(lua, 1);
	}
	lua_pop(lua, 1);
}

static int
config_parse_root(lua_State *lua, Config *cfg)
{
	lua_Integer tagcount;

	luaL_checktype(lua, 1, LUA_TTABLE);
	config_parse_appearance(lua, cfg);
	config_parse_canvas(lua, cfg);
	config_parse_logging(lua, cfg);
	cfg->tagcount = config_int_field(lua, 1, "tagcount", cfg->tagcount);
	if (cfg->tagcount < 1 || cfg->tagcount > 31)
		luaL_error(lua, "tagcount must be between 1 and 31");
	lua_getfield(lua, 1, "tags");
	if (lua_isnumber(lua, -1)) {
		tagcount = luaL_checkinteger(lua, -1);
		if (tagcount < 1 || tagcount > 31)
			luaL_error(lua, "tags must be between 1 and 31");
		cfg->tagcount = (int)tagcount;
	} else if (!lua_isnil(lua, -1)) {
		luaL_checktype(lua, -1, LUA_TTABLE);
		cfg->tagcount = config_int_field(lua, -1, "count", cfg->tagcount);
	}
	lua_pop(lua, 1);
	if (cfg->tagcount < 1 || cfg->tagcount > 31)
		luaL_error(lua, "tagcount must be between 1 and 31");
	config_parse_layouts(lua, cfg);
	config_parse_rules(lua, cfg);
	config_parse_monrules(lua, cfg);
	config_parse_keyboard(lua, cfg);
	config_parse_libinput(lua, cfg);
	config_parse_bindings(lua, cfg, "keys", 0);
	config_parse_bindings(lua, cfg, "buttons", 1);
	if (!cfg->layout_count || !cfg->monrule_count || !cfg->key_count
			|| !cfg->button_count)
		luaL_error(lua, "layouts, monitors, keys, and buttons cannot be empty");
	return 0;
}

static int
config_parse_root_call(lua_State *lua)
{
	Config *cfg = lua_touserdata(lua, lua_upvalueindex(1));
	return config_parse_root(lua, cfg);
}

static void
config_set_color(float color[4], unsigned int rgba)
{
	int i;
	for (i = 0; i < 4; i++)
		color[i] = (float)((rgba >> (24 - i * 8)) & 0xff) / 255.0f;
}

static void
config_default_key(Config *cfg, uint32_t mod, xkb_keysym_t keysym,
		ConfigAction action, Arg arg)
{
	config_append_key(cfg, mod, keysym, action, arg);
}

static void
config_defaults(Config *cfg)
{
	static const xkb_keysym_t vt_keys[] = {
		XKB_KEY_XF86Switch_VT_1, XKB_KEY_XF86Switch_VT_2,
		XKB_KEY_XF86Switch_VT_3, XKB_KEY_XF86Switch_VT_4,
		XKB_KEY_XF86Switch_VT_5, XKB_KEY_XF86Switch_VT_6,
		XKB_KEY_XF86Switch_VT_7, XKB_KEY_XF86Switch_VT_8,
		XKB_KEY_XF86Switch_VT_9, XKB_KEY_XF86Switch_VT_10,
		XKB_KEY_XF86Switch_VT_11, XKB_KEY_XF86Switch_VT_12,
	};
	uint32_t mod = WLR_MODIFIER_LOGO;
	int i;

	memset(cfg, 0, sizeof(*cfg));
	cfg->sloppyfocus = 1;
	cfg->bypass_surface_visibility = 0;
	cfg->borderpx = 1;
	config_set_color(cfg->rootcolor, 0x222222ff);
	config_set_color(cfg->bordercolor, 0x444444ff);
	config_set_color(cfg->focuscolor, 0x005577ff);
	config_set_color(cfg->urgentcolor, 0xff0000ff);
	config_set_color(cfg->fullscreen_bg, 0x000000ff);
	cfg->pan_speed = 1.0f;
	cfg->zoom_min = 0.25f;
	cfg->zoom_max = 4.0f;
	cfg->zoom_step = 1.2f;
	cfg->tagcount = 1;
	cfg->log_level = WLR_ERROR;
	cfg->repeat_rate = 25;
	cfg->repeat_delay = 600;
	cfg->tap_to_click = 1;
	cfg->tap_and_drag = 1;
	cfg->drag_lock = 1;
	cfg->natural_scrolling = 1;
	cfg->disable_while_typing = 1;
	cfg->left_handed = 0;
	cfg->middle_button_emulation = 0;
	cfg->scroll_method = LIBINPUT_CONFIG_SCROLL_2FG;
	cfg->click_method = LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS;
	cfg->send_events_mode = LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;
	cfg->accel_profile = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
	cfg->accel_speed = 0.0;
	cfg->button_map = LIBINPUT_CONFIG_TAP_MAP_LRM;

	config_append_layout(cfg, "canvas", "[ ]", NULL);
	config_append_monrule(cfg, NULL, 0.55f, 1, 1.0f,
			&cfg->layouts[0], WL_OUTPUT_TRANSFORM_NORMAL, -1, -1);

	config_append_key(cfg, mod, XKB_KEY_p, spawn,
			(Arg){.v = config_exec("wmenu-run")});
	config_append_key(cfg, mod, XKB_KEY_Return, spawn,
			(Arg){.v = config_exec("foot")});
	config_default_key(cfg, WLR_MODIFIER_ALT, XKB_KEY_Tab, focusstack, (Arg){.i = 1});
	config_default_key(cfg, WLR_MODIFIER_ALT | WLR_MODIFIER_SHIFT, XKB_KEY_ISO_Left_Tab,
			focusstack, (Arg){.i = -1});
	config_default_key(cfg, mod, XKB_KEY_c, centercanvas, (Arg){0});
	config_default_key(cfg, mod, XKB_KEY_0, homecanvas, (Arg){0});
	config_default_key(cfg, mod, XKB_KEY_minus, zoomcanvas, (Arg){.f = -1.0f});
	config_default_key(cfg, mod, XKB_KEY_equal, zoomcanvas, (Arg){.f = 1.0f});
	config_default_key(cfg, mod | WLR_MODIFIER_SHIFT, XKB_KEY_c, killclient, (Arg){0});
	config_default_key(cfg, mod, XKB_KEY_f, togglefullscreen, (Arg){0});
	config_default_key(cfg, mod, XKB_KEY_comma, focusmon,
			(Arg){.i = WLR_DIRECTION_LEFT});
	config_default_key(cfg, mod, XKB_KEY_period, focusmon,
			(Arg){.i = WLR_DIRECTION_RIGHT});
	config_default_key(cfg, mod | WLR_MODIFIER_SHIFT, XKB_KEY_less, tagmon,
			(Arg){.i = WLR_DIRECTION_LEFT});
	config_default_key(cfg, mod | WLR_MODIFIER_SHIFT, XKB_KEY_greater, tagmon,
			(Arg){.i = WLR_DIRECTION_RIGHT});
	config_default_key(cfg, mod | WLR_MODIFIER_SHIFT, XKB_KEY_q, quit, (Arg){0});
	for (i = 0; i < 12; i++)
		config_default_key(cfg, WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT,
				vt_keys[i], chvt, (Arg){.ui = (unsigned int)i + 1});
	config_append_button(cfg, mod, BTN_LEFT, moveresize, (Arg){.ui = CurMove});
	config_append_button(cfg, mod, BTN_MIDDLE, startpan, (Arg){0});
	config_append_button(cfg, mod, BTN_RIGHT, moveresize, (Arg){.ui = CurResize});
}

static int
config_parse_file(const char *path, Config *next)
{
	lua_State *lua;
	int status;
	const char *error;

	lua = luaL_newstate();
	if (!lua)
		die("config: luaL_newstate failed");
	luaL_openlibs(lua);
	config_defaults(next);
	status = luaL_loadfilex(lua, path, NULL);
	if (!status)
		status = lua_pcall(lua, 0, 1, 0);
	if (!status && !lua_istable(lua, -1)) {
		lua_settop(lua, 0);
		lua_pushliteral(lua, "config file must return a table");
		status = LUA_ERRRUN;
	}
	if (!status) {
		lua_pushlightuserdata(lua, next);
		lua_pushcclosure(lua, config_parse_root_call, 1);
		lua_pushvalue(lua, -2);
		status = lua_pcall(lua, 1, 0, 0);
	}
	if (status) {
		error = lua_tostring(lua, -1);
		fprintf(stderr, "dwl: config %s: %s\n", path,
				error ? error : "unknown Lua error");
		lua_close(lua);
		config_free(next);
		return -1;
	}
	lua_close(lua);
	return 0;
}

static char *
config_path_candidate(const char *base)
{
	char *path;
	size_t length = strlen(base) + sizeof("/dwl/config.lua");

	path = ecalloc(length, sizeof(*path));
	snprintf(path, length, "%s/dwl/config.lua", base);
	return path;
}

static char *
config_find_path(const char *path)
{
	char *candidate;
	const char *base;

	if (path)
		return config_strdup(path);
	if ((path = getenv("DWL_CONFIG")))
		return config_strdup(path);
	base = getenv("XDG_CONFIG_HOME");
	if (base && (candidate = config_path_candidate(base))) {
		if (!access(candidate, R_OK))
			return candidate;
		free(candidate);
	}
	if (!access(DWL_SYSTEM_CONFIG, R_OK))
		return config_strdup(DWL_SYSTEM_CONFIG);
	/* The source tree ships a usable example for development and packaging. */
	if (!access("config/config.lua", R_OK))
		return config_strdup("config/config.lua");
	if ((base = getenv("HOME"))) {
		candidate = config_path_candidate(base);
		return candidate;
	}
	return config_strdup("config/config.lua");
}

static void
config_split_path(void)
{
	char *slash;

	config_dir = config_strdup(config_file);
	slash = strrchr(config_dir, '/');
	if (!slash) {
		free(config_dir);
		config_dir = config_strdup(".");
		config_name = config_strdup(config_file);
		return;
	}
	config_name = config_strdup(slash + 1);
	if (slash == config_dir)
		slash[1] = '\0';
	else
		*slash = '\0';
}

static int
config_init(const char *path)
{
	Config next;

	config_file = config_find_path(path);
	config_split_path();
	config_defaults(&config);
	if (!access(config_file, R_OK) && !config_parse_file(config_file, &next)) {
		Config old = config;
		config = next;
		config_free(&old);
	} else if (access(config_file, R_OK)) {
		fprintf(stderr, "dwl: config %s is not readable, using defaults\n", config_file);
	}
	return 0;
}

static int
config_inotify(int fd, uint32_t mask, void *data)
{
	char buffer[4096];
	ssize_t length;
	char *offset;
	struct inotify_event *event;

	(void)mask;
	(void)data;
	while ((length = read(fd, buffer, sizeof(buffer))) > 0) {
		for (offset = buffer; offset < buffer + length;
				offset += sizeof(*event) + event->len) {
			event = (struct inotify_event *)offset;
			if (event->len && !strcmp(event->name, config_name)
					&& !(event->mask & IN_ISDIR))
				config_reload();
		}
	}
	return 0;
}

static void
config_watch_start(void)
{
	if (!event_loop || config_fd >= 0)
		return;
	config_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (config_fd < 0) {
		fprintf(stderr, "dwl: config watcher: inotify_init1: %s\n", strerror(errno));
		return;
	}
	config_watch = inotify_add_watch(config_fd, config_dir,
			IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_DELETE);
	if (config_watch < 0) {
		fprintf(stderr, "dwl: config watcher: %s: %s\n", config_dir, strerror(errno));
		close(config_fd);
		config_fd = -1;
		return;
	}
	config_source = wl_event_loop_add_fd(event_loop, config_fd, WL_EVENT_READABLE,
			config_inotify, NULL);
	if (!config_source) {
		fprintf(stderr, "dwl: config watcher: failed to attach event source\n");
		inotify_rm_watch(config_fd, config_watch);
		close(config_fd);
		config_watch = -1;
		config_fd = -1;
	}
}

static void
config_watch_stop(void)
{
	if (config_source) {
		wl_event_source_remove(config_source);
		config_source = NULL;
	}
	if (config_watch >= 0 && config_fd >= 0)
		inotify_rm_watch(config_fd, config_watch);
	if (config_fd >= 0)
		close(config_fd);
	config_watch = -1;
	config_fd = -1;
	free(config_dir);
	free(config_name);
	free(config_file);
	config_dir = NULL;
	config_name = NULL;
	config_file = NULL;
}

static const MonitorRule *
config_monitor_rule(const Config *cfg, const char *name)
{
	size_t i;
	for (i = 0; i < cfg->monrule_count; i++)
		if (!cfg->monrules[i].name || strstr(name, cfg->monrules[i].name))
			return &cfg->monrules[i];
	return &cfg->monrules[cfg->monrule_count - 1];
}

static void
config_apply_keyboard(void)
{
	struct xkb_context *context;
	struct xkb_keymap *keymap;

	if (!kb_group)
		return;
	context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (!context || !(keymap = xkb_keymap_new_from_names(context, &config.xkb_rules,
			XKB_KEYMAP_COMPILE_NO_FLAGS))) {
		if (context)
			xkb_context_unref(context);
		fprintf(stderr, "dwl: config: failed to reload keyboard map\n");
		return;
	}
	wlr_keyboard_set_keymap(&kb_group->wlr_group->keyboard, keymap);
	wlr_keyboard_set_repeat_info(&kb_group->wlr_group->keyboard,
			config.repeat_rate, config.repeat_delay);
	xkb_keymap_unref(keymap);
	xkb_context_unref(context);
}

static void
config_apply_live(const Config *old)
{
	Monitor *m;
	Client *c;
	const MonitorRule *rule;
	Layout *layout;
	const char *name;
	int i;
	struct wlr_output_state state;

	wlr_log_init(config.log_level, NULL);
	if (root_bg)
		wlr_scene_rect_set_color(root_bg, config.rootcolor);
	wl_list_for_each(m, &mons, link) {
		for (i = 0; i < 2; i++) {
			name = (old && m->lt[i]) ? m->lt[i]->name : NULL;
			layout = config_find_layout(&config, name);
			m->lt[i] = layout ? layout : &config.layouts[0];
		}
		rule = config_monitor_rule(&config, m->wlr_output->name);
		m->mfact = rule->mfact;
		m->nmaster = rule->nmaster;
		m->canvas_zoom = MAX(config.zoom_min,
				MIN(config.zoom_max, m->canvas_zoom));
		if (m->fullscreen_bg)
			wlr_scene_rect_set_color(m->fullscreen_bg, config.fullscreen_bg);
		wlr_output_state_init(&state);
		wlr_output_state_set_scale(&state, rule->scale);
		wlr_output_state_set_transform(&state, rule->rr);
		wlr_output_commit_state(m->wlr_output, &state);
		wlr_output_state_finish(&state);
		arrangelayers(m);
		arrange(m);
	}
	wl_list_for_each(c, &clients, link) {
		if (client_is_unmanaged(c))
			continue;
		c->bw = c->isfullscreen ? 0 : config.borderpx;
		if (c->scene) {
			client_set_border_color(c, c == focustop(c->mon) ? config.focuscolor
					: c->isurgent ? config.urgentcolor : config.bordercolor);
			resize(c, c->geom, 0);
		}
	}
	config_apply_keyboard();
	printstatus();
}

static void
config_reload(void)
{
	Config next;
	Config old;

	if (access(config_file, R_OK) || config_parse_file(config_file, &next))
		return;
	old = config;
	config = next;
	config_apply_live(&old);
	config_free(&old);
	fprintf(stderr, "dwl: reloaded config %s\n", config_file);
}
