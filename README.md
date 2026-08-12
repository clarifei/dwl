# Inca!

Inca! is a pointer-first Wayland compositor built around an infinite canvas.
Windows keep their own place on one continuous surface; the viewport pans and
zooms instead of arranging them into a traditional tiling layout.

This is a personal fork of [dwl], reshaped for mouse and touchpad use. It keeps
the small C codebase and wlroots foundation, adds runtime Lua configuration,
and uses [SceneFX] for compositor effects.

## What it does

- Smooth pan and zoom driven by pointer gestures and the output refresh cycle.
- Native-resolution rendering at the default zoom, with zoom capped at `1.0`.
- Window snapping, collision-free placement, edge panning, and in-place collapse.
- Configurable opacity, blur, shadows, corners, and layer-shell effects.
- Hot-reloaded Lua settings for input, keys, windows, monitors, and appearance.

## Build

Inca! needs a C compiler, `pkg-config`, Wayland protocols, wlroots 0.19,
SceneFX 0.4, libinput, xkbcommon, Lua 5.4, Cairo, Fontconfig, libdrm, and pixman.

```sh
meson setup build
meson compile -C build
meson install -C build
```

Run it from a display manager session or directly:

```sh
inca
inca -c ~/.config/inca/config.lua
```

## Configuration

The example is [config/config.lua](config/config.lua). Inca! loads the first
available configuration from:

1. `-c path`
2. `$INCA_CONFIG`
3. `$XDG_CONFIG_HOME/inca/config.lua`
4. the installed `share/inca/config.lua`
5. `./config/config.lua`
6. `$HOME/.config/inca/config.lua`

Valid saves are applied live. Invalid files are reported and the last valid
configuration stays active.

The defaults use empty-space drag or touchpad scroll to pan, pinch to zoom,
`Super` + pointer buttons to manipulate windows, `Alt` + `Tab` to switch,
`Super` + `m` to collapse, and `Super` + `0` to reset the canvas. The Lua file
is the source of truth for every binding.

## Credits

Inca! is forked from [dwl] and retains its license and attribution. Its canvas
interaction also draws inspiration from [vxwm] and [driftwm].

[dwl]: https://codeberg.org/dwl/dwl
[SceneFX]: https://github.com/wlrfx/scenefx
[vxwm]: https://codeberg.org/wh1tepearl/vxwm
[driftwm]: https://github.com/malbiruk/driftwm
