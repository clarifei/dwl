#!/bin/sh
set -eu

runtime_dir=$(mktemp -d)
sentinel_pid=
cleanup() {
	kill "${inca_pid:-}" 2>/dev/null || true
	if [ -n "$sentinel_pid" ]; then
		kill "$sentinel_pid" 2>/dev/null || true
	fi
	rm -rf "$runtime_dir"
}
trap cleanup EXIT

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

startup_pid_file="$runtime_dir/startup-child.pid"
# Once the startup leader exits, shutdown must not signal its stale PGID.
startup_cmd="sleep 30 & printf '%s\\n' \"\$!\" > '$startup_pid_file'"
XDG_RUNTIME_DIR="$runtime_dir" WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 \
WLR_RENDERER=pixman "$1" -c "$2" -s "$startup_cmd" &
inca_pid=$!

for _ in $(seq 1 50); do
	if [ -S "$runtime_dir/wayland-0" ]; then
		for _ in $(seq 1 50); do
			[ ! -s "$startup_pid_file" ] || break
			sleep 0.01
		done
		[ -s "$startup_pid_file" ] || exit 1
		IFS= read -r sentinel_pid < "$startup_pid_file"
		sleep 0.1
		client_status=0
		if [ "$#" -ge 3 ]; then
			XDG_RUNTIME_DIR="$runtime_dir" WAYLAND_DISPLAY=wayland-0 "$3" || client_status=$?
		fi
		kill -TERM "$inca_pid" 2>/dev/null || true
		compositor_status=0
		wait "$inca_pid" || compositor_status=$?
		[ "$client_status" -eq 0 ] || exit "$client_status"
		[ "$compositor_status" -eq 0 ] || exit "$compositor_status"
		kill -0 "$sentinel_pid" 2>/dev/null || exit 1
		exit 0
	fi
	if ! kill -0 "$inca_pid" 2>/dev/null; then
		wait "$inca_pid"
	fi
	sleep 0.1
done

exit 1
