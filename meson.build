project(
  'inca',
  'c',
  version: '0.9.0',
  default_options: [
    'c_std=c11',
    'buildtype=release',
    'warning_level=3',
    'werror=true',
  ],
)

cc = meson.get_compiler('c')

if not meson.is_subproject()
  add_test_setup('headless', env: [
    'WLR_BACKENDS=headless',
    'WLR_HEADLESS_OUTPUTS=1',
    'WLR_RENDERER=pixman',
  ])
endif

build_config = configuration_data()
build_config.set_quoted('INCA_SYSTEM_CONFIG', join_paths(get_option('prefix'), get_option('datadir'), 'inca', 'config.lua'))
build_config.set_quoted('VERSION', meson.project_version())
configure_file(output: 'build-config.h', configuration: build_config)

add_project_arguments(
  '-D_POSIX_C_SOURCE=200809L',
  '-DWLR_USE_UNSTABLE',
  '-Wpedantic',
  '-Wshadow',
  '-Wstrict-prototypes',
  '-Wfloat-conversion',
  '-Wno-unused-parameter',
  language: 'c',
)

if get_option('native_optimizations') and not meson.is_cross_build()
  add_project_arguments(
    cc.get_supported_arguments('-march=native', '-mtune=native'),
    language: 'c',
  )
endif

wayland_server = dependency('wayland-server')
wayland_client = dependency('wayland-client')
wayland_protocols = dependency('wayland-protocols', native: true)
wlroots = dependency('wlroots-0.19')
scenefx = dependency('scenefx-0.4')
xkbcommon = dependency('xkbcommon')
libinput = dependency('libinput')
lua = dependency('lua5.4')
cairo = dependency('cairo')
fontconfig = dependency('fontconfig')
libdrm = dependency('libdrm')
pixman = dependency('pixman-1')
glesv2 = dependency('glesv2')
math = cc.find_library('m', required: true)

wayland_scanner = find_program('wayland-scanner', native: true)
protocols_dir = wayland_protocols.get_variable(pkgconfig: 'pkgdatadir')
xdg_shell_xml = join_paths(protocols_dir, 'stable', 'xdg-shell', 'xdg-shell.xml')

protocol_headers = [
  custom_target(
    'cursor-shape-v1-protocol',
    input: join_paths(protocols_dir, 'staging', 'cursor-shape', 'cursor-shape-v1.xml'),
    output: 'cursor-shape-v1-protocol.h',
    command: [wayland_scanner, 'enum-header', '@INPUT@', '@OUTPUT@'],
  ),
  custom_target(
    'pointer-constraints-protocol',
    input: join_paths(protocols_dir, 'unstable', 'pointer-constraints', 'pointer-constraints-unstable-v1.xml'),
    output: 'pointer-constraints-unstable-v1-protocol.h',
    command: [wayland_scanner, 'enum-header', '@INPUT@', '@OUTPUT@'],
  ),
  custom_target(
    'layer-shell-protocol',
    input: 'protocols/wlr-layer-shell-unstable-v1.xml',
    output: 'wlr-layer-shell-unstable-v1-protocol.h',
    command: [wayland_scanner, 'enum-header', '@INPUT@', '@OUTPUT@'],
  ),
  custom_target(
    'output-power-protocol',
    input: 'protocols/wlr-output-power-management-unstable-v1.xml',
    output: 'wlr-output-power-management-unstable-v1-protocol.h',
    command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
  ),
  custom_target(
    'xdg-shell-protocol',
    input: xdg_shell_xml,
    output: 'xdg-shell-protocol.h',
    command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
  ),
]

xdg_shell_client_header = custom_target(
  'xdg-shell-client-protocol',
  input: xdg_shell_xml,
  output: 'xdg-shell-client-protocol.h',
  command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
)

xdg_shell_client_code = custom_target(
  'xdg-shell-client-protocol-code',
  input: xdg_shell_xml,
  output: 'xdg-shell-client-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)

background_effect_header = custom_target(
  'background-effect-header',
  input: join_paths(protocols_dir, 'staging', 'ext-background-effect', 'ext-background-effect-v1.xml'),
  output: 'ext-background-effect-v1-protocol.h',
  command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
)

background_effect_code = custom_target(
  'background-effect-code',
  input: join_paths(protocols_dir, 'staging', 'ext-background-effect', 'ext-background-effect-v1.xml'),
  output: 'ext-background-effect-v1-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)

inca_sources = files(
  'src/background-effect.c',
  'src/client.c',
  'src/config.c',
  'src/inca.c',
  'src/input.c',
  'src/canvas.c',
  'src/output.c',
  'src/runtime.c',
  'src/server.c',
  'src/session.c',
  'src/util.c',
)

inca = executable(
  'inca',
  inca_sources,
  protocol_headers,
  background_effect_header,
  background_effect_code,
  include_directories: include_directories('src'),
  dependencies: [
    wayland_server,
    scenefx,
    wlroots,
    xkbcommon,
    libinput,
    lua,
    cairo,
    fontconfig,
    libdrm,
    pixman,
    glesv2,
    math,
  ],
  install: true,
)

canvas_test = executable(
	'canvas-test',
	'tests/canvas_test.c',
	include_directories: include_directories('src'),
	dependencies: [wayland_server, math],
)
test('canvas', canvas_test)

wayland_smoke = executable(
  'wayland-smoke',
  'tests/wayland_smoke.c',
  xdg_shell_client_header,
  xdg_shell_client_code,
  include_directories: include_directories('.'),
  dependencies: wayland_client,
)

headless_smoke = find_program('tests/headless-smoke.sh')
test('headless-smoke', headless_smoke,
  args: [inca, join_paths(meson.project_source_root(), 'config/config.lua'), wayland_smoke],
  suite: 'wayland',
  timeout: 30)

install_data('config/config.lua', install_dir: join_paths(get_option('datadir'), 'inca'))
install_data('inca.desktop', install_dir: join_paths(get_option('datadir'), 'wayland-sessions'))
install_man('inca.1')
