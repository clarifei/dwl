#!/bin/sh
set -eu

runtime_dir=$(mktemp -d)
trap 'kill "${inca_pid:-}" 2>/dev/null || true; rm -rf "$runtime_dir"' EXIT

has_render_node=false
for render_node in /dev/dri/renderD*; do
	if [ -e "$render_node" ]; then
		has_render_node=true
		break
	fi
done
if [ "$has_render_node" = false ]; then
	# Scenefx uses EGL/GLES; a sandbox without a DRM render node cannot start it.
	exit 77
fi

XDG_RUNTIME_DIR="$runtime_dir" WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 \
WLR_RENDERER=pixman "$1" -c "$2" &
inca_pid=$!

for _ in $(seq 1 50); do
	if [ -S "$runtime_dir/wayland-0" ]; then
		client_status=0
		if [ "$#" -ge 3 ]; then
			XDG_RUNTIME_DIR="$runtime_dir" WAYLAND_DISPLAY=wayland-0 "$3" || client_status=$?
		fi
		kill -TERM "$inca_pid" 2>/dev/null || true
		compositor_status=0
		wait "$inca_pid" || compositor_status=$?
		[ "$client_status" -eq 0 ] || exit "$client_status"
		[ "$compositor_status" -eq 0 ] || exit "$compositor_status"
		exit 0
	fi
	if ! kill -0 "$inca_pid" 2>/dev/null; then
		wait "$inca_pid"
	fi
	sleep 0.1
done

exit 1
