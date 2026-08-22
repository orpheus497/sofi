/*
 * sofi
 *
 * MIT/X11 License
 * Copyright © 2013-2023 Qball Cow <qball@gmpclient.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef SOFI_VIEW_H
#define SOFI_VIEW_H

#include "mode.h"
#include "widgets/textbox.h"
#include "widgets/widget.h"
#include <pango/pango.h>
#ifdef ENABLE_XCB
#include <xcb/xcb.h>
#else
#include "xcb-dummy.h"
#endif
/**
 * @defgroup View View
 *
 * The sofi Menu view.
 * @{
 * @}
 */

/**
 * @defgroup ViewHandle ViewHandle
 * @ingroup View
 *
 * @{
 */
typedef struct SofiViewState SofiViewState;
typedef enum {
  /** Create a menu for entering text */
  MENU_NORMAL = 0,
  /** Create a menu for entering passwords */
  MENU_PASSWORD = 1,
  /** Create amanaged window. */
  MENU_NORMAL_WINDOW = 2,
  /** ERROR dialog */
  MENU_ERROR_DIALOG = 4,
  /** Create transient window. */
  MENU_TRANSIENT_WINDOW = 8,
} MenuFlags;

/**
 * @param sw the Mode to show.
 * @param input A pointer to a string where the inputted data is placed.
 * @param menu_flags   Flags indicating state of the menu.
 * @param finalize the finalize callback
 *
 * Main menu callback.
 *
 * @returns The command issued (see MenuReturn)
 */
SofiViewState *sofi_view_create(Mode *sw, const char *input,
                                MenuFlags menu_flags,
                                void (*finalize)(SofiViewState *));

/**
 * @param state The Menu Handle
 *
 * Check if a finalize function is set, and if sets executes it.
 */
void sofi_view_finalize(SofiViewState *state);

/**
 * @param state the Menu handle
 *
 * Get the return value associated to the users action.
 *
 * @returns the return value
 */
MenuReturn sofi_view_get_return_value(const SofiViewState *state);
/**
 * @param state the Menu handle
 *
 * Returns the index of the next visible position.
 *
 * @return the next position.
 */
unsigned int sofi_view_get_next_position(const SofiViewState *state);
/**
 * @param state the Menu handle
 * @param text The text to add to the input box
 *
 * Update the state if needed.
 */
void sofi_view_handle_text(SofiViewState *state, const char *text);
/**
 * @param state the Menu handle
 * @param x The X coordinates of the motion
 * @param y The Y coordinates of the motion
 * @param find_mouse_target if we should handle pure mouse motion
 *
 * Update the state if needed.
 */
void sofi_view_handle_mouse_motion(SofiViewState *state, gint x, gint y,
                                   gboolean find_mouse_target);
/**
 * @param state the Menu handle
 *
 * Update the state if needed.
 */
void sofi_view_maybe_update(SofiViewState *state);
void sofi_view_temp_configure_notify(SofiViewState *state,
                                     xcb_configure_notify_event_t *xce);
void sofi_view_temp_click_to_exit(SofiViewState *state, xcb_window_t target);
/**
 * Update the state if needed.
 */
void sofi_view_frame_callback(void);
/**
 * @param state the Menu handle
 *
 * @returns returns if this state is completed.
 */
unsigned int sofi_view_get_completed(const SofiViewState *state);
/**
 * @param state the Menu handle
 *
 * @returns the raw user input.
 */
const char *sofi_view_get_user_input(const SofiViewState *state);

/**
 * @param state The Menu Handle
 * @param selected_line The line to select.
 *
 * Select a line.
 */
void sofi_view_set_selected_line(SofiViewState *state,
                                 unsigned int selected_line);

/**
 * @param state The Menu Handle
 *
 * Get the selected line.
 *
 * @returns the selected line or UINT32_MAX if none selected.
 */
unsigned int sofi_view_get_selected_line(const SofiViewState *state);
/**
 * @param state The Menu Handle
 *
 * Restart the menu so it can be displayed again.
 * Resets SofiViewState::quit and SofiViewState::retv.
 */
void sofi_view_restart(SofiViewState *state);

/**
 * @param state The handle to the view
 * @param scope The scope of the action
 * @param action The action
 *
 * @returns TRUE if action was handled.
 */
gboolean sofi_view_check_action(SofiViewState *state, BindingsScope scope,
                                guint action);

/**
 * @param state The handle to the view
 * @param scope The scope of the action
 * @param action The action
 */
void sofi_view_trigger_action(SofiViewState *state, BindingsScope scope,
                              guint action);

/**
 * @param state The handle to the view
 *
 * Free's the memory allocated for this handle.
 * After a call to this function, state is invalid and can no longer be used.
 */
void sofi_view_free(SofiViewState *state);
/** @} */
/**
 * @defgroup ViewGlobal ViewGlobal
 * @ingroup View
 *
 * Global menu view functions.
 * These do not work on the view itself but modifies the global state.
 * @{
 */

/**
 * Get the current active view Handle.
 *
 * @returns the active view handle or NULL
 */
SofiViewState *sofi_view_get_active(void);

/**
 * Get the current active textbox with the user input.
 *
 * @returns the active textbox or NULL
 */
textbox *sofi_view_get_active_text(void);

/**
 * @param state the new active view handle.
 *
 * Set the current active view Handle, If NULL passed a queued  view is popped
 * from stack.
 *
 */

void sofi_view_set_active(SofiViewState *state);

/**
 * @param state remove view handle.
 *
 * remove state handle from queue, if current view, pop view from
 * stack.
 *
 */
void sofi_view_remove_active(SofiViewState *state);
/**
 * @param msg The error message to show.
 * @param markup The error message uses pango markup.
 *
 * The error message to show.
 */
int sofi_view_error_dialog(const char *msg, int markup);

/**
 * Queue a redraw.
 * This triggers a X11 Expose Event.
 */
void sofi_view_queue_redraw(void);

void sofi_view_calculate_window_position(SofiViewState *state);

/**
 * Cleanup internal data of the view.
 */
void sofi_view_cleanup(void);

/**
 * @param state The handle to the view
 *
 * Get the mode currently displayed by the view.
 *
 * @returns the mode currently displayed by the view
 */
Mode *sofi_view_get_mode(SofiViewState *state);

/**
 * Unmap the current view.
 */
void sofi_view_hide(void);

/**
 * Indicate the current view needs to reload its data.
 * This can only be done when *more* information is available.
 *
 * The reloading happens 'lazy', multiple calls might be handled at once.
 */
void sofi_view_reload(void);

/**
 * @param state The handle to the view
 * @param mode The new mode to display
 *
 * Change the current view to show a different mode.
 */
void sofi_view_switch_mode(SofiViewState *state, Mode *mode);

/**
 * @param state The handle to the view
 * @param text An UTF-8 encoded character array with the text to overlay.
 *
 * Overlays text over the current view. Passing NULL for text hides the overlay.
 */
void sofi_view_set_overlay(SofiViewState *state, const char *text);
/**
 * @param state The handle to the view
 * @param text An UTF-8 encoded character array with the text to overlay.
 *
 * Overlays text over the current view. Passing NULL for text hides the overlay.
 * This message is automatically removed after X seconds.
 */
void sofi_view_set_overlay_timeout(SofiViewState *state, const char *text);

/**
 * @param state The handle to the view.
 *
 * Clears the user entry box, set selected to 0.
 */
void sofi_view_clear_input(SofiViewState *state);

/**
 * @param menu_flags The state of the new window.
 *
 * Creates the internal 'Cached' window that gets reused between views.
 * TODO: Internal call to view exposed.
 */
void __create_window(MenuFlags menu_flags);

/**
 * Get the handle of the main window.
 *
 * @returns the xcb_window_t for sofi's view or XCB_WINDOW_NONE.
 */
xcb_window_t sofi_view_get_window(void);
/** @} */

/**
 * @defgroup ViewThreadPool ViewThreadPool
 * @ingroup View
 *
 * The view can (optionally) keep a set of worker threads around to parallize
 * work. This includes filtering and sorting.
 *
 * @{
 */
/**
 * Initialize the threadpool
 */
void sofi_view_workers_initialize(void);
/**
 * Stop all threads and free the resources used by the threadpool
 */
void sofi_view_workers_finalize(void);

/**
 * @param width the width of the monitor.
 * @param height the height of the monitor.
 *
 * Return the current monitor workarea.
 *
 */
void sofi_view_get_current_monitor(int *width, int *height);

/**
 * Takes a screenshot.
 */
void sofi_capture_screenshot(void);
/**
 * Set the window title.
 */
void sofi_view_set_window_title(const char *title);

void sofi_view_set_size(SofiViewState *state, gint width, gint height);

void sofi_view_get_size(SofiViewState *state, gint *width, gint *height);

void sofi_view_ping_mouse(SofiViewState *state);

void sofi_view_set_window_title(const char *title);
void sofi_view_pool_refresh(void);

void sofi_view_get_menu_rect(int *x, int *y, int *w, int *h);

void sofi_view_set_cursor(SofiCursorType type);


/**
 * Input history
 */

void input_history_save(void);
void input_history_initialize(void);

struct _view_proxy;

/* Implementations */
extern const struct _view_proxy *xcb_view_proxy;
#ifdef ENABLE_WAYLAND
extern const struct _view_proxy *wayland_view_proxy;
#endif

void view_init(const struct _view_proxy *view_in);

/**
  @param state The window state handle
  @param mode The pango ellipsize mode to user
 * set ellipsize mode to start.
 */
void sofi_view_ellipsize_listview(SofiViewState *state,
                                  PangoEllipsizeMode mode);

/**
 * @param new_x New XIM window x pos
 * @param new_y New XIM window y pos
 *
 * Updates the XIM window position to new_x and new_y, relative to the
 * main_window
 */
gboolean sofi_set_im_window_pos(int new_x, int new_y);

// Cancel the current view
void sofi_view_cancel(SofiViewState *state);

/**
 * @param wid to test.
 * @param action the action done.
 * @param x [unused]
 * @param y [unsued]
 * @param user_data
 *
 * textbux widget trigger action function.
 *
 * @return the result.
 */
WidgetTriggerActionResult textbox_button_trigger_action(
    widget *wid, MouseBindingMouseDefaultAction action, G_GNUC_UNUSED gint x,
    G_GNUC_UNUSED gint y, G_GNUC_UNUSED void *user_data);

/** @} */
#endif
