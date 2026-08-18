/* Inca! internal compositor state and module interfaces. */
#ifndef INCA_H
#define INCA_H

#include "build-config.h"
#include <errno.h>
#include <cairo.h>
#include <drm_fourcc.h>
#include <fcntl.h>
#include <fontconfig/fontconfig.h>
#include <getopt.h>
#include <limits.h>
#include <libinput.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <pixman.h>
#include <scenefx/render/fx_renderer/fx_renderer.h>
#include <scenefx/types/fx/clipped_region.h>
#include <scenefx/types/fx/corner_location.h>
#include <scenefx/types/wlr_scene.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/wait.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/libinput.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_alpha_modifier_v1.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_drm.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_ext_data_control_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_keyboard_group.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/types/wlr_linux_drm_syncobj_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_pointer_gestures_v1.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/types/wlr_single_pixel_buffer_v1.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>
#include <xkbcommon/xkbcommon.h>
#include "canvas.h"
#include "util.h"

/* macros */
#define MAX(A, B)               ((A) > (B) ? (A) : (B))
#define MIN(A, B)               ((A) < (B) ? (A) : (B))
#define CLEANMASK(mask)         (mask & ~WLR_MODIFIER_CAPS)
#define LENGTH(X)               (sizeof X / sizeof X[0])
#define END(A)                  ((A) + LENGTH(A))
#define CLIENTON(C, M)          ((M) && (C)->mon == (M))
#define LISTEN(E, L, H)         wl_signal_add((E), ((L)->notify = (H), (L)))
#define LISTEN_STATIC(E, H)     do { struct wl_listener *_l = ecalloc(1, sizeof(*_l)); _l->notify = (H); wl_signal_add((E), _l); } while (0)

/* enums */
enum { CurNormal, CurPressed, CurMove, CurResize, CurPan, CurConsumed }; /* cursor */
enum { SceneClient, SceneLayer }; /* scene node owners */
enum { LyrBg, LyrBottom, LyrClients, LyrTop, LyrFullscreen, LyrOverlay,
	LyrBlock, NUM_LAYERS }; /* scene layers */

typedef union {
	int i;
	uint32_t ui;
	float f;
	const void *v;
} Arg;

typedef struct {
	unsigned int mod;
	unsigned int button;
	void (*func)(const Arg *);
	Arg arg;
} Button;

typedef struct Monitor Monitor;
typedef struct {
	/* Must stay first: scene hit testing uses this owner discriminator. */
	unsigned int type; /* SceneClient */
	Monitor *mon;
	struct wlr_scene_tree *scene;
	struct wlr_scene_rect *border;
	struct wlr_scene_tree *scene_surface;
	struct wlr_scene_shadow *shadow;
	struct wlr_scene_rect *collapsed_scrim;
	struct wlr_scene_buffer *collapsed_label;
	int collapsed_label_width, collapsed_label_height;
	struct wl_list link;
	struct wl_list flink;
	struct wlr_box geom; /* canvas-relative, includes border */
	struct wlr_box prev; /* canvas-relative, includes border */
	struct wlr_box bounds; /* only width and height are used */
	struct wlr_xdg_surface *surface;
	struct wlr_xdg_toplevel_decoration_v1 *decoration;
	struct wl_listener commit;
	struct wl_listener map;
	struct wl_listener maximize;
	struct wl_listener unmap;
	struct wl_listener destroy;
	struct wl_listener set_title;
	struct wl_listener fullscreen;
	struct wl_listener set_decoration_mode;
	struct wl_listener destroy_decoration;
	unsigned int bw;
	int isurgent, isfullscreen, iscollapsed;
	uint32_t resize; /* configure serial of a pending resize */
} Client;

typedef struct {
	uint32_t mod;
	xkb_keysym_t keysym;
	int on_release;
	void (*func)(const Arg *);
	Arg arg;
} Key;

typedef struct {
	struct wlr_keyboard_group *wlr_group;

	int nsyms;
	const xkb_keysym_t *keysyms; /* invalid if nsyms == 0 */
	uint32_t mods; /* invalid if nsyms == 0 */
	uint32_t release_keycode;
	int release_armed;
	struct wl_event_source *key_repeat_source;

	struct wl_listener modifiers;
	struct wl_listener key;
	struct wl_listener destroy;
} KeyboardGroup;

typedef struct {
	/* Must stay first: scene hit testing uses this owner discriminator. */
	unsigned int type; /* SceneLayer */
	Monitor *mon;
	struct wlr_scene_tree *scene;
	struct wlr_scene_tree *popups;
	struct wlr_scene_tree *effects;
	struct wlr_scene_layer_surface_v1 *scene_layer;
	struct wl_list effect_rects;
	struct wl_list link;
	int mapped;
	struct wlr_layer_surface_v1 *layer_surface;

	struct wl_listener destroy;
	struct wl_listener unmap;
	struct wl_listener surface_commit;
} LayerSurface;

struct Monitor {
	struct wl_list link;
	struct wlr_output *wlr_output;
	struct wlr_scene_output *scene_output;
	struct wlr_scene_rect *fullscreen_bg; /* See createmon() for info */
	struct wl_listener frame;
	struct wl_listener destroy;
	struct wl_listener request_state;
	struct wl_listener destroy_lock_surface;
	struct wlr_session_lock_surface_v1 *lock_surface;
	struct wlr_box m; /* output area in global logical coordinates */
	struct wlr_box w; /* usable output area in global logical coordinates */
	double canvas_x, canvas_y; /* screen-space translation */
	double canvas_x_target, canvas_y_target;
	double canvas_zoom, canvas_zoom_target;
	int canvas_dirty;
	struct timespec canvas_camera_frame;
	struct timespec canvas_zoom_frame;
	struct timespec canvas_pan_frame;
	uint32_t spawn_serial;
	struct wl_list layers[4]; /* LayerSurface.link */
	int gamma_lut_changed;
	int asleep;
};

typedef struct {
	const char *name;
	float scale;
	enum wl_output_transform rr;
	int x, y;
} MonitorRule;

typedef struct {
	struct wlr_pointer_constraint_v1 *constraint;
	struct wl_listener destroy;
} PointerConstraint;

typedef struct {
	const char *app_id;
	const char *title;
	int monitor;
} Rule;

typedef struct {
	struct wlr_scene_tree *scene;

	struct wlr_session_lock_v1 *lock;
	struct wl_listener new_surface;
	struct wl_listener unlock;
	struct wl_listener destroy;
} SessionLock;

typedef struct {
	int sloppyfocus;
	int bypass_surface_visibility;
	unsigned int borderpx;
	float rootcolor[4];
	float bordercolor[4];
	float focuscolor[4];
	float urgentcolor[4];
	float fullscreen_bg[4];
	int corner_radius;
	int opacity_enabled;
	float opacity_active;
	float opacity_inactive;
	int shadow_enabled;
	float shadow_sigma;
	int shadow_offset_x;
	int shadow_offset_y;
	float shadow_color[4];
	int blur_enabled;
	int blur_passes;
	int blur_radius;
	float blur_noise;
	float blur_brightness;
	float blur_contrast;
	float blur_saturation;
	int blur_ignore_transparent;
	int layer_effects_enabled;
	float layer_opacity;
	float pan_speed;
	float zoom_min;
	float zoom_max;
	float zoom_step;
	int window_gap;
	int snap_distance;
	int edge_pan_zone;
	float edge_pan_min_speed;
	float edge_pan_max_speed;
	int collapsed_font_size;
	float collapsed_scrim[4];
	float collapsed_title_color[4];
	float collapsed_detail_color[4];
	enum wlr_log_importance log_level;

	Rule *rules;
	size_t rule_count;
	MonitorRule *monrules;
	size_t monrule_count;

	struct xkb_rule_names xkb_rules;
	int repeat_rate;
	int repeat_delay;

	int tap_to_click;
	int tap_and_drag;
	int drag_lock;
	int natural_scrolling;
	int disable_while_typing;
	int left_handed;
	int middle_button_emulation;
	enum libinput_config_scroll_method scroll_method;
	enum libinput_config_click_method click_method;
	uint32_t send_events_mode;
	enum libinput_config_accel_profile accel_profile;
	double accel_speed;
	enum libinput_config_tap_button_map button_map;

	Key *keys;
	size_t key_count;
	Button *buttons;
	size_t button_count;
} Config;

extern Config config;

/* Shared compositor state. It lives in runtime.c; modules only depend on it
 * through this header, so their implementation files compile independently. */
extern pid_t child_pid;
extern int locked;
extern void *exclusive_focus;
extern struct wl_display *dpy;
extern struct wl_event_loop *event_loop;
extern struct wlr_backend *backend;
extern struct wlr_scene *scene;
extern struct wlr_scene_tree *layers[NUM_LAYERS];
extern struct wlr_scene_tree *drag_icon;
extern const int layermap[];
extern struct wlr_renderer *drw;
extern struct wlr_allocator *alloc;
extern struct wlr_compositor *compositor;
extern struct wlr_session *session;
extern struct wlr_xdg_shell *xdg_shell;
extern struct wlr_xdg_activation_v1 *activation;
extern struct wlr_xdg_decoration_manager_v1 *xdg_decoration_mgr;
extern struct wl_list clients;
extern struct wl_list fstack;
extern struct wlr_idle_notifier_v1 *idle_notifier;
extern struct wlr_idle_inhibit_manager_v1 *idle_inhibit_mgr;
extern struct wlr_layer_shell_v1 *layer_shell;
extern struct wlr_output_manager_v1 *output_mgr;
extern struct wlr_virtual_keyboard_manager_v1 *virtual_keyboard_mgr;
extern struct wlr_virtual_pointer_manager_v1 *virtual_pointer_mgr;
extern struct wlr_cursor_shape_manager_v1 *cursor_shape_mgr;
extern struct wlr_output_power_manager_v1 *power_mgr;
extern struct wlr_pointer_constraints_v1 *pointer_constraints;
extern struct wlr_pointer_gestures_v1 *pointer_gestures;
extern struct wlr_relative_pointer_manager_v1 *relative_pointer_mgr;
extern struct wlr_pointer_constraint_v1 *active_constraint;
extern struct wlr_cursor *cursor;
extern struct wlr_xcursor_manager *cursor_mgr;
extern Monitor *pinchmon;
extern double pinchzoom;
extern struct wlr_scene_rect *root_bg;
extern struct wlr_session_lock_manager_v1 *session_lock_mgr;
extern struct wlr_scene_rect *locked_bg;
extern struct wlr_session_lock_v1 *cur_lock;
extern struct wlr_seat *seat;
extern KeyboardGroup *kb_group;
extern unsigned int cursor_mode;
extern Client *grabc;
extern double grabcx, grabcy;
extern struct wlr_output_layout *output_layout;
extern struct wlr_box sgeom;
extern struct wl_list mons;
extern Monitor *selmon;

#include "client.h"

extern struct wl_listener cursor_axis;
extern struct wl_listener cursor_button;
extern struct wl_listener cursor_frame;
extern struct wl_listener cursor_motion;
extern struct wl_listener cursor_motion_absolute;
extern struct wl_listener cursor_pinch_begin;
extern struct wl_listener cursor_pinch_end;
extern struct wl_listener cursor_pinch_update;
extern struct wl_listener cursor_swipe_update;
extern struct wl_listener gpu_reset;
extern struct wl_listener layout_change;
extern struct wl_listener new_idle_inhibitor;
extern struct wl_listener new_input_device;
extern struct wl_listener new_virtual_keyboard;
extern struct wl_listener new_virtual_pointer;
extern struct wl_listener new_pointer_constraint;
extern struct wl_listener new_output;
extern struct wl_listener new_xdg_toplevel;
extern struct wl_listener new_xdg_popup;
extern struct wl_listener new_xdg_decoration;
extern struct wl_listener new_layer_surface;
extern struct wl_listener output_mgr_apply;
extern struct wl_listener output_mgr_test;
extern struct wl_listener output_power_mgr_set_mode;
extern struct wl_listener request_activate;
extern struct wl_listener request_cursor;
extern struct wl_listener request_set_psel;
extern struct wl_listener request_set_sel;
extern struct wl_listener request_set_cursor_shape;
extern struct wl_listener request_start_drag;
extern struct wl_listener start_drag;
extern struct wl_listener new_session_lock;

/* Module interfaces. */
void applybounds(Client *c, struct wlr_box *bbox);
void applyrules(Client *c);
void arrange(Monitor *m);
void arrangelayer(Monitor *m, struct wl_list *list,
		struct wlr_box *usable_area, int exclusive);
void arrangelayers(Monitor *m);
void axisnotify(struct wl_listener *listener, void *data);
void buttonpress(struct wl_listener *listener, void *data);
const pixman_region32_t *backgroundeffectregion(struct wlr_surface *surface);
void backgroundeffectsinit(void);
double clientcanvasscale(Client *c);
void clientcollapsedupdate(Client *c, int redraw);
void clientbufferfxupdate(Client *c, struct wlr_scene_buffer *buffer,
		double scale);
void clienteffectsupdate(Client *c);
void clientshadowgeometry(Client *c, double scale);
void clientsettle(Client *c);
void clientsceneposition(Client *c);
void clientsceneupdate(Client *c);
void clientsnap(Client *c, struct wlr_box *geo);
void canvaspointtoscreen(Monitor *m, double x, double y,
		double *screen_x, double *screen_y);
void canvaspointtoworld(Monitor *m, double x, double y,
		double *world_x, double *world_y);
void canvasvisiblebox(Monitor *m, struct wlr_box *box);
void centercanvas(const Arg *arg);
void chvt(const Arg *arg);
void checkidleinhibitor(struct wlr_surface *exclude);
void cleanup(void);
void cleanupmon(struct wl_listener *listener, void *data);
void cleanuplisteners(void);
void closemon(Monitor *m);
void commitlayersurfacenotify(struct wl_listener *listener, void *data);
void commitnotify(struct wl_listener *listener, void *data);
void commitpopup(struct wl_listener *listener, void *data);
void createdecoration(struct wl_listener *listener, void *data);
void createidleinhibitor(struct wl_listener *listener, void *data);
void createkeyboard(struct wlr_keyboard *keyboard);
KeyboardGroup *createkeyboardgroup(void);
void createlayersurface(struct wl_listener *listener, void *data);
void createlocksurface(struct wl_listener *listener, void *data);
void createmon(struct wl_listener *listener, void *data);
void createnotify(struct wl_listener *listener, void *data);
void createpointer(struct wlr_pointer *pointer);
void createpointerconstraint(struct wl_listener *listener, void *data);
void createpopup(struct wl_listener *listener, void *data);
void cursorconstrain(struct wlr_pointer_constraint_v1 *constraint);
void cursorframe(struct wl_listener *listener, void *data);
void cursorwarptohint(void);
void destroydecoration(struct wl_listener *listener, void *data);
void destroydragicon(struct wl_listener *listener, void *data);
void destroyidleinhibitor(struct wl_listener *listener, void *data);
void destroylayersurfacenotify(struct wl_listener *listener, void *data);
void destroylock(SessionLock *lock, int unlocked);
void destroylocksurface(struct wl_listener *listener, void *data);
void destroynotify(struct wl_listener *listener, void *data);
void destroypointerconstraint(struct wl_listener *listener, void *data);
void destroysessionlock(struct wl_listener *listener, void *data);
void destroykeyboardgroup(struct wl_listener *listener, void *data);
Monitor *dirtomon(enum wlr_direction dir);
void focusclient(Client *c, int lift);
void focusmon(const Arg *arg);
void focusstack(const Arg *arg);
Client *focustop(Monitor *m);
void fullscreennotify(struct wl_listener *listener, void *data);
void gpureset(struct wl_listener *listener, void *data);
void handlesig(int signo);
void homecanvas(const Arg *arg);
void inputdevice(struct wl_listener *listener, void *data);
int keybinding(uint32_t mods, xkb_keysym_t sym, int on_release, int run);
uint32_t keymod(xkb_keysym_t sym);
void keypress(struct wl_listener *listener, void *data);
void keypressmod(struct wl_listener *listener, void *data);
int keyrepeat(void *data);
void killclient(const Arg *arg);
void layereffectsclear(LayerSurface *l);
void layereffectsupdate(LayerSurface *l);
void layereffectsupdateall(void);
void locksession(struct wl_listener *listener, void *data);
void mapnotify(struct wl_listener *listener, void *data);
void maximizenotify(struct wl_listener *listener, void *data);
void motionabsolute(struct wl_listener *listener, void *data);
void motionnotify(uint32_t time, struct wlr_input_device *device, double sx,
		double sy, double sx_unaccel, double sy_unaccel);
void motionrelative(struct wl_listener *listener, void *data);
void moveresize(const Arg *arg);
void outputmgrapply(struct wl_listener *listener, void *data);
void outputmgrapplyortest(struct wlr_output_configuration_v1 *config, int test);
void outputmgrtest(struct wl_listener *listener, void *data);
void pancanvas(Monitor *m, double dx, double dy);
void pinchbegin(struct wl_listener *listener, void *data);
void pinchend(struct wl_listener *listener, void *data);
void pinchupdate(struct wl_listener *listener, void *data);
void pointerfocus(Client *c, struct wlr_surface *surface,
		double sx, double sy, uint32_t time);
void printstatus(void);
void powermgrsetmode(struct wl_listener *listener, void *data);
void quit(const Arg *arg);
void rendermon(struct wl_listener *listener, void *data);
void requestdecorationmode(struct wl_listener *listener, void *data);
void requeststartdrag(struct wl_listener *listener, void *data);
void requestmonstate(struct wl_listener *listener, void *data);
void resize(Client *c, struct wlr_box geo, int interact);
void run(char *startup_cmd);
void setcursor(struct wl_listener *listener, void *data);
void setcanvaszoom(Monitor *m, double zoom);
void setcursorshape(struct wl_listener *listener, void *data);
void setfullscreen(Client *c, int fullscreen);
void setclientmonitor(Client *c, Monitor *m);
void setpsel(struct wl_listener *listener, void *data);
void setsel(struct wl_listener *listener, void *data);
void setup(void);
void spawn(const Arg *arg);
void startpan(const Arg *arg);
void startdrag(struct wl_listener *listener, void *data);
void swipeupdate(struct wl_listener *listener, void *data);
void sendtomonitor(const Arg *arg);
int tickcanvaszoom(Monitor *m, const struct timespec *now);
int tickcanvasedgepan(Monitor *m, const struct timespec *now);
int tickcanvascamera(Monitor *m, const struct timespec *now);
void togglefullscreen(const Arg *arg);
void setcollapsed(Client *c, int collapsed);
void togglecollapse(const Arg *arg);
void updatecanvas(Monitor *m, int rescale);
void unlocksession(struct wl_listener *listener, void *data);
void unmaplayersurfacenotify(struct wl_listener *listener, void *data);
void unmapnotify(struct wl_listener *listener, void *data);
void updatemons(struct wl_listener *listener, void *data);
void updatetitle(struct wl_listener *listener, void *data);
void urgent(struct wl_listener *listener, void *data);
void virtualkeyboard(struct wl_listener *listener, void *data);
void virtualpointer(struct wl_listener *listener, void *data);
Monitor *xytomon(double x, double y);
void xytonode(double x, double y, struct wlr_surface **psurface,
		Client **pc, LayerSurface **pl, double *nx, double *ny);
void zoomcanvas(const Arg *arg);
void zoomcanvasby(Monitor *m, double factor);

int config_init(const char *path);
void config_watch_start(void);
void config_watch_stop(void);
void config_free(Config *cfg);
const MonitorRule *config_monitor_rule(const Config *cfg, const char *name);

#endif
