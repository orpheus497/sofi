#ifndef SOFI_WAYLAND_INTERNAL_H
#define SOFI_WAYLAND_INTERNAL_H

#include <cairo.h>
#include <glib.h>
#include <libgwater-wayland.h>
#include <nkutils-bindings.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "wayland.h"

typedef enum {
  WAYLAND_GLOBAL_COMPOSITOR,
  WAYLAND_GLOBAL_SHM,
  WAYLAND_GLOBAL_LAYER_SHELL,
  WAYLAND_GLOBAL_XDG_WM_BASE,
  WAYLAND_GLOBAL_KEYBOARD_SHORTCUTS_INHIBITOR,
  WAYLAND_GLOBAL_CURSOR_SHAPE,
  _WAYLAND_GLOBAL_SIZE,
} wayland_global_name;

/**
 * Which shell protocol the surface is driven through. Layer shell is preferred
 * because it can position and size itself; xdg-shell is the fallback for
 * compositors that do not implement zwlr_layer_shell_v1 (Mutter, KWin), where
 * placement is left to the compositor.
 */
typedef enum {
  WAYLAND_SHELL_NONE = 0,
  WAYLAND_SHELL_LAYER,
  WAYLAND_SHELL_XDG,
} wayland_shell_kind;

typedef struct {
  uint32_t button;
  char modifiers;
  gint x, y;
  gboolean pressed;
  guint32 time;
} widget_button_event;

typedef struct {
  gint x, y;
  guint32 time;
} widget_motion_event;

typedef struct _wayland_seat wayland_seat;

typedef struct {
  void *offer;
} clipboard_data;

typedef struct {
  GMainLoop *main_loop;
  GWaterWaylandSource *main_loop_source;
  struct wl_display *display;
  struct wl_registry *registry;
  uint32_t global_names[_WAYLAND_GLOBAL_SIZE];
  struct wl_compositor *compositor;

#ifdef HAVE_WAYLAND_CURSOR_SHAPE
  struct wp_cursor_shape_manager_v1 *cursor_shape_manager;
#endif

  struct wl_data_device_manager *data_device_manager;
  struct zwp_primary_selection_device_manager_v1
      *primary_selection_device_manager;

  struct zwlr_layer_shell_v1 *layer_shell;
  struct xdg_wm_base *xdg_wm_base;

  struct zwp_keyboard_shortcuts_inhibit_manager_v1 *kb_shortcuts_inhibit_manager;

  struct wl_shm *shm;
  size_t buffer_count;
  struct {
    char *theme_name;
    SofiCursorType type;
    struct wl_cursor_theme *theme;
    struct wl_cursor *cursor;
    struct wl_cursor_image *image;
    struct wl_surface *surface;
    struct wl_callback *frame_cb;
    guint scale;
  } cursor;
  GHashTable *seats;
  GHashTable *seats_by_name;
  wayland_seat *last_seat;
  GHashTable *outputs;
  struct wl_surface *surface;
  struct zwlr_layer_surface_v1 *wlr_surface;
  /* xdg-shell fallback surface objects; only set when shell == WAYLAND_SHELL_XDG */
  struct xdg_surface *xdg_surface;
  struct xdg_toplevel *xdg_toplevel;
  wayland_shell_kind shell;
  struct wl_callback *frame_cb;
  size_t scales[3];
  int32_t scale;
  NkBindingsSeat *bindings_seat;

  clipboard_data clipboards[2];

  uint32_t layer_width;
  uint32_t layer_height;

  struct zwp_text_input_manager_v3 *text_input_manager;
} wayland_stuff;

struct _wayland_seat {
  wayland_stuff *context;
  uint32_t global_name;
  struct wl_seat *seat;
  gchar *name;
  struct {
    xkb_keycode_t key;
    /* Action purpose: a GSource id rather than a pointer. GLib destroys and
     * unrefs the source when a timeout callback returns G_SOURCE_REMOVE, so a
     * stored pointer dangles; an id is safe to clear and re-check. */
    guint source_id;
    int32_t rate;
    int32_t delay;
  } repeat;
  uint32_t serial;
  uint32_t pointer_serial;
  struct wl_keyboard *keyboard;
  struct wl_pointer *pointer;

#ifdef HAVE_WAYLAND_CURSOR_SHAPE
  struct wp_cursor_shape_device_v1 *cursor_shape_device;
#endif
  struct wl_data_device *data_device;
  struct zwp_primary_selection_device_v1 *primary_selection_device;

  enum wl_pointer_axis_source axis_source;
  widget_button_event button;
  widget_motion_event motion;
  struct {
    gint vertical;
    gint horizontal;
  } wheel;
  struct {
    double vertical;
    double horizontal;
  } wheel_continuous;

  struct zwp_text_input_v3 *text_input;
};

/* Supported interface versions */
#define WL_COMPOSITOR_INTERFACE_VERSION 3
#define WL_SHM_INTERFACE_VERSION 1
#define WL_SEAT_INTERFACE_MIN_VERSION 5
#define WL_SEAT_INTERFACE_MAX_VERSION 8
#define WL_OUTPUT_INTERFACE_MIN_VERSION 2
#define WL_OUTPUT_INTERFACE_MAX_VERSION 4
/* v4 is where ON_DEMAND keyboard interactivity lives. Below it wlroots
 * silently coerces the argument to !!interactive, so asking for ON_DEMAND
 * on a v1 binding means EXCLUSIVE with no error -- the request appears to
 * work and does the opposite of what a passive surface needs. wl_registry_bind
 * still takes MIN(advertised, this), so a v1-only compositor is unaffected. */
#define WL_LAYER_SHELL_INTERFACE_VERSION 4
#define WL_XDG_WM_BASE_INTERFACE_VERSION 2
#define WL_KEYBOARD_SHORTCUTS_INHIBITOR_INTERFACE_VERSION 1

extern wayland_stuff *wayland;

#endif
