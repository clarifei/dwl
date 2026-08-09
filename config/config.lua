-- dwl runtime configuration. Save this file and dwl reloads it automatically.
return {
  appearance = {
    sloppyfocus = true,
    bypass_surface_visibility = false,
    borderpx = 1,
    rootcolor = "#222222ff",
    bordercolor = "#444444ff",
    focuscolor = "#005577ff",
    urgentcolor = "#ff0000ff",
    fullscreen_bg = "#000000ff",
  },

  tagcount = 9,

  logging = "error", -- silent, error, info, debug

  layouts = {
    { name = "tile", symbol = "[]=", arrange = "tile" },
    { name = "floating", symbol = "><>", arrange = "floating" },
    { name = "monocle", symbol = "[M]", arrange = "monocle" },
  },

  rules = {
    { app_id = "Gimp_EXAMPLE", floating = true },
    { app_id = "firefox_EXAMPLE", tags = 1 << 8 },
  },

  monitors = {
    { name = nil, mfact = 0.55, nmaster = 1, scale = 1,
      layout = "tile", transform = "normal", x = -1, y = -1 },
  },

  keyboard = {
    repeat_rate = 25,
    repeat_delay = 600,
    rules = {
      layout = nil,
      model = nil,
      variant = nil,
      options = nil,
    },
  },

  libinput = {
    tap_to_click = true,
    tap_and_drag = true,
    drag_lock = true,
    natural_scrolling = false,
    disable_while_typing = true,
    left_handed = false,
    middle_button_emulation = false,
    scroll_method = "2fg", -- none, 2fg, edge, button
    click_method = "button_areas", -- none, button_areas, clickfinger
    send_events_mode = "enabled", -- enabled, disabled, disabled_on_external_mouse
    accel_profile = "adaptive", -- flat, adaptive
    accel_speed = 0,
    button_map = "lrm", -- lrm, lmr
  },

  keys = {
    { mods = {"ALT"}, key = "p", action = "spawn", command = {"wmenu-run"} },
    { mods = {"ALT", "SHIFT"}, key = "Return", action = "spawn", command = {"foot"} },
    { mods = {"ALT"}, key = "j", action = "focusstack", value = 1 },
    { mods = {"ALT"}, key = "k", action = "focusstack", value = -1 },
    { mods = {"ALT"}, key = "i", action = "incnmaster", value = 1 },
    { mods = {"ALT"}, key = "d", action = "incnmaster", value = -1 },
    { mods = {"ALT"}, key = "h", action = "setmfact", value = -0.05 },
    { mods = {"ALT"}, key = "l", action = "setmfact", value = 0.05 },
    { mods = {"ALT"}, key = "Return", action = "zoom" },
    { mods = {"ALT"}, key = "Tab", action = "view", value = 0 },
    { mods = {"ALT", "SHIFT"}, key = "c", action = "killclient" },
    { mods = {"ALT"}, key = "t", action = "setlayout", layout = "tile" },
    { mods = {"ALT"}, key = "f", action = "setlayout", layout = "floating" },
    { mods = {"ALT"}, key = "m", action = "setlayout", layout = "monocle" },
    { mods = {"ALT"}, key = "space", action = "setlayout" },
    { mods = {"ALT", "SHIFT"}, key = "space", action = "togglefloating" },
    { mods = {"ALT"}, key = "e", action = "togglefullscreen" },
    { mods = {"ALT"}, key = "0", action = "view", value = 0xffffffff },
    { mods = {"ALT", "SHIFT"}, key = "parenright", action = "tag", value = 0xffffffff },
    { mods = {"ALT"}, key = "comma", action = "focusmon", value = "left" },
    { mods = {"ALT"}, key = "period", action = "focusmon", value = "right" },
    { mods = {"ALT", "SHIFT"}, key = "less", action = "tagmon", value = "left" },
    { mods = {"ALT", "SHIFT"}, key = "greater", action = "tagmon", value = "right" },
    { mods = {"ALT", "SHIFT"}, key = "q", action = "quit" },
    { mods = {"CTRL", "ALT"}, key = "XF86Switch_VT_1", action = "chvt", value = 1 },
    { mods = {"CTRL", "ALT"}, key = "XF86Switch_VT_2", action = "chvt", value = 2 },
    { mods = {"CTRL", "ALT"}, key = "XF86Switch_VT_3", action = "chvt", value = 3 },
    { mods = {"CTRL", "ALT"}, key = "XF86Switch_VT_4", action = "chvt", value = 4 },
    { mods = {"CTRL", "ALT"}, key = "XF86Switch_VT_5", action = "chvt", value = 5 },
    { mods = {"CTRL", "ALT"}, key = "XF86Switch_VT_6", action = "chvt", value = 6 },
    { mods = {"CTRL", "ALT"}, key = "XF86Switch_VT_7", action = "chvt", value = 7 },
    { mods = {"CTRL", "ALT"}, key = "XF86Switch_VT_8", action = "chvt", value = 8 },
    { mods = {"CTRL", "ALT"}, key = "XF86Switch_VT_9", action = "chvt", value = 9 },
    { mods = {"CTRL", "ALT"}, key = "XF86Switch_VT_10", action = "chvt", value = 10 },
    { mods = {"CTRL", "ALT"}, key = "XF86Switch_VT_11", action = "chvt", value = 11 },
    { mods = {"CTRL", "ALT"}, key = "XF86Switch_VT_12", action = "chvt", value = 12 },
  },

  buttons = {
    { mods = {"ALT"}, button = "left", action = "moveresize", value = "move" },
    { mods = {"ALT"}, button = "middle", action = "togglefloating" },
    { mods = {"ALT"}, button = "right", action = "moveresize", value = "resize" },
  },
}
