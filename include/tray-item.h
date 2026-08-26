/*
 * sofi
 *
 * MIT/X11 License
 * Copyright © 2026 orpheus497
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

#ifndef SOFI_TRAY_ITEM_H
#define SOFI_TRAY_ITEM_H

#include <gio/gio.h>
#include <glib.h>

/**
 * @defgroup TRAYITEM StatusNotifierItem proxy
 *
 * What one tray item currently looks like, and how to act on it.
 *
 * The watcher (`tray-watcher.h`) knows which items exist. This knows what they
 * are. The split matters because the two fail differently: an item that
 * registered and then stopped answering is still registered, and its icon simply
 * stops changing.
 *
 * **Everything here is asynchronous, and that is not a style choice.** These are
 * calls to arbitrary third-party applications running in the daemon that also
 * serves notifications. A synchronous `GetAll` against a wedged application
 * would stall the whole daemon for the D-Bus timeout, which is the exact failure
 * hikari-sakura's own `src/topbar.c` was built as a separate process to avoid
 * (`DECISIONS_LOG.md` F12). Nothing in this file blocks.
 *
 * @{
 */

/** The spec's Status property. */
typedef enum {
  /** The application does not consider the item worth showing. */
  SOFI_TRAY_STATUS_PASSIVE,
  /** Normal. */
  SOFI_TRAY_STATUS_ACTIVE,
  /** Wants the user; swaps the icon for the attention one. */
  SOFI_TRAY_STATUS_NEEDS_ATTENTION,
} SofiTrayStatus;

/** Opaque handle to one item's proxied state. */
typedef struct _SofiTrayItemProxy SofiTrayItemProxy;

/** Called whenever any item's properties have changed. */
typedef void (*SofiTrayItemChangedFunc)(gpointer user_data);

/**
 * Begin proxying an item.
 *
 * Returns immediately with everything empty and fetches in the background; the
 * changed callback fires when the first answer lands. A caller must therefore
 * tolerate an item with no title and no icon, which is also what a badly
 * behaved application leaves it as permanently.
 */
SofiTrayItemProxy *sofi_tray_item_new(GDBusConnection *connection,
                                      const gchar *service,
                                      const gchar *bus_name,
                                      const gchar *object_path);

/** Stop proxying, cancel anything in flight, and free. */
void sofi_tray_item_free(SofiTrayItemProxy *item);

/** The watcher's canonical service string. Never NULL. */
const gchar *sofi_tray_item_service(const SofiTrayItemProxy *item);

/** The application's own Id, e.g. "nm-applet". Never NULL; "" until known. */
const gchar *sofi_tray_item_id(const SofiTrayItemProxy *item);

/**
 * Human-readable title, falling back to Id when the application set none --
 * which many do. Never NULL.
 */
const gchar *sofi_tray_item_title(const SofiTrayItemProxy *item);

/** Current status. PASSIVE until the first fetch lands. */
SofiTrayStatus sofi_tray_item_status(const SofiTrayItemProxy *item);

/**
 * Icon name **with the attention override already applied**: an item in
 * NeedsAttention reports its attention icon here, not its ordinary one.
 *
 * The precedence is a property of the specification rather than of any caller,
 * so it is resolved once, here, instead of at every drawing site.
 *
 * @returns "" when the application supplies no name, in which case the caller
 *          should try sofi_tray_item_icon_pixmap().
 */
const gchar *sofi_tray_item_icon_name(const SofiTrayItemProxy *item);

/**
 * Private icon directory the application ships its own icons in.
 *
 * Not in the specification, and used by a great many real items regardless --
 * Electron and Qt applications commonly ship icons no system theme contains. An
 * icon name that resolves nowhere should be retried against this path before
 * being given up on. Never NULL; "" when unset.
 */
const gchar *sofi_tray_item_icon_theme_path(const SofiTrayItemProxy *item);

/**
 * Raw `a(iiay)` pixmap, attention override applied, or NULL.
 *
 * Deliberately still a GVariant: turning sender-chosen pixels into a surface is
 * the one place in the tray that takes hostile input, and it is done in one
 * audited place rather than wherever an icon happens to be needed.
 *
 * @returns a borrowed reference, valid until this item next changes or is freed.
 */
GVariant *sofi_tray_item_icon_pixmap(const SofiTrayItemProxy *item);

/**
 * The item's icon as validated, premultiplied, native-endian ARGB32 -- the
 * layout cairo consumes directly.
 *
 * Decoded lazily and cached: the conversion happens the first time an icon is
 * actually wanted after it changed, not every time an application announces a
 * change. A chatty applet that never has its icon asked for therefore costs
 * nothing at all.
 *
 * The wire format this comes from is the one piece of the tray that takes
 * genuinely hostile input -- the sender chooses the dimensions and supplies the
 * byte array -- so the geometry is checked against the actual byte count before
 * a single pixel is read, and dimensions beyond anything a tray icon can
 * legitimately be are refused outright rather than clamped.
 *
 * @param item   the item to decode the icon of.
 * @param data   [out] borrowed, valid until this item's icon next changes.
 * @param length [out] bytes, always `width * height * 4`.
 * @param width  [out]
 * @param height [out]
 *
 * @returns FALSE when the item has no usable pixmap, in which case the caller
 *          should fall back to sofi_tray_item_icon_name().
 */
gboolean sofi_tray_item_icon_argb32(SofiTrayItemProxy *item,
                                    const guint8 **data, gsize *length,
                                    gint *width, gint *height);

/**
 * Whether the application says left-click should open its menu rather than
 * activate it.
 *
 * Worth honouring: an item that sets this often implements `Activate` as a
 * no-op, so ignoring it makes the icon look dead.
 */
gboolean sofi_tray_item_is_menu(const SofiTrayItemProxy *item);

/** The item's `com.canonical.dbusmenu` object path, or "" when it has none. */
const gchar *sofi_tray_item_menu_path(const SofiTrayItemProxy *item);

/**
 * Primary activation -- what a left click means.
 *
 * Fire and forget: the reply is discarded and a failure is not reported to the
 * user, because there is nothing they could do about an application that
 * declines to be activated, and the alternative is an error dialog raised by
 * somebody else's bug.
 *
 * @param item the item to activate.
 * @param x,y screen coordinates, which the specification passes through for
 *            items that want to position something. Most ignore them.
 */
void sofi_tray_item_activate(SofiTrayItemProxy *item, gint x, gint y);

/** Secondary activation -- what a middle click means. */
void sofi_tray_item_secondary_activate(SofiTrayItemProxy *item, gint x, gint y);

/**
 * Set the one callback fired whenever any item's properties change.
 *
 * Module-wide rather than per item, because every consumer so far wants the same
 * thing from it -- redraw the tray zone -- and per-item plumbing would only be
 * unpacked back into that.
 */
void sofi_tray_item_set_changed_callback(SofiTrayItemChangedFunc callback,
                                         gpointer user_data);

/**@}*/
#endif // SOFI_TRAY_ITEM_H
