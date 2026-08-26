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

/**
 * @file
 * @brief One org.kde.StatusNotifierItem, proxied.
 *
 * Properties are re-fetched wholesale whenever the item says anything changed,
 * rather than being cached per property. That is deliberate and is explained at
 * on_item_signal().
 */

/** The log domain of this file. */
#define G_LOG_DOMAIN "TrayItem"

#include "config.h"

#ifdef SYSTEM_TRAY

#include <gio/gio.h>

#include "tray-item.h"

#define ITEM_INTERFACE "org.kde.StatusNotifierItem"
#define PROPERTIES_INTERFACE "org.freedesktop.DBus.Properties"

/**
 * Action purpose: a call to a third-party application must not be able to pin
 * resources indefinitely. The default D-Bus timeout is 25 seconds, which is a
 * very long time to hold a fetch open for an item that has stopped answering;
 * a tray icon that is three seconds stale is not a problem, and one that never
 * resolves is.
 */
#define ITEM_CALL_TIMEOUT_MS 3000

/**
 * Action purpose: collapse a burst of change signals into one fetch.
 *
 * Chatty items are the norm rather than the exception -- a network applet or a
 * volume indicator can emit several times a second, and every signal here means
 * a full property batch because the signals carry no payload. Without this,
 * N items times their own update rate times a whole GetAll each is a steady
 * background load for a strip that is usually not even on screen.
 *
 * 100ms is chosen to be below the threshold at which a changing tray icon reads
 * as laggy, while being long enough to swallow the bursts that matter: an item
 * changing status almost always emits NewStatus and NewIcon together.
 */
#define ITEM_REFETCH_DEBOUNCE_MS 100

/**
 * Action purpose: the largest icon this will decode, and the reason the decode
 * does not need a thread.
 *
 * A StatusNotifierItem pixmap is a tray icon. Real ones are 16 to 64 pixels
 * square; 512 is already far beyond anything a tray can display and is chosen to
 * leave room for a HiDPI asset rather than to accommodate a plausible case.
 *
 * The number matters because the sender chooses it. At 4096 -- the cap
 * `image_from_hint()` uses for notification images, where a large image IS the
 * point -- the worst case is 16 million pixels of byte-swap and premultiply, and
 * that is real work to do on an event loop. At 512 the worst case is 262144
 * pixels, which is roughly a millisecond, and the question of moving it off the
 * loop stops being interesting. Bounding the work is a better answer than
 * scheduling it somewhere else.
 *
 * Oversized pixmaps are REFUSED rather than scaled: an application sending a
 * 4096px tray icon has misunderstood something, and quietly resizing it would
 * hide that while still paying to read every pixel.
 */
#define ITEM_ICON_MAX_DIM 512

/**
 * The size a tray icon is decoded toward when an item offers several. Items
 * commonly ship 16/22/24/32; the smallest that is at least this is preferred,
 * because the widget scales down cleanly and up badly.
 */
#define ITEM_ICON_PREFERRED_DIM 32

struct _SofiTrayItemProxy {
  gchar *service;
  gchar *bus_name;
  gchar *object_path;

  GDBusConnection *connection;
  guint signal_sub;
  /** Pending debounced re-fetch, or 0. See ITEM_REFETCH_DEBOUNCE_MS. */
  guint refetch_source;
  /** Cancels anything in flight when the item goes away mid-call. */
  GCancellable *cancellable;

  gchar *id;
  gchar *title;
  gchar *icon_name;
  gchar *attention_icon_name;
  gchar *icon_theme_path;
  gchar *menu_path;

  GVariant *icon_pixmap;
  GVariant *attention_icon_pixmap;

  SofiTrayStatus status;
  gboolean is_menu;

  /** Decoded ARGB32, or NULL. Owned here. */
  guint8 *icon_argb;
  gint icon_argb_width;
  gint icon_argb_height;
  /** Set whenever the EFFECTIVE pixmap could have changed; drives lazy decode. */
  gboolean icon_dirty;
};

static SofiTrayItemChangedFunc changed_callback = NULL;
static gpointer changed_user_data = NULL;

void sofi_tray_item_set_changed_callback(SofiTrayItemChangedFunc callback,
                                         gpointer user_data) {
  changed_callback = callback;
  changed_user_data = user_data;
}

static void notify_changed(void) {
  if (changed_callback != NULL) {
    changed_callback(changed_user_data);
  }
}

/* ------------------------------------------------------------------ */
/* property application                                                */
/* ------------------------------------------------------------------ */

static void set_string(gchar **dest, GVariant *value) {
  if (!g_variant_is_of_type(value, G_VARIANT_TYPE_STRING) &&
      !g_variant_is_of_type(value, G_VARIANT_TYPE_OBJECT_PATH)) {
    return;
  }
  g_free(*dest);
  *dest = g_variant_dup_string(value, NULL);
}

static void set_pixmap(SofiTrayItemProxy *item, GVariant **dest,
                       GVariant *value) {
  /* Action purpose: type-check before storing rather than before decoding. The
   * decoder is the audited place for the pixel geometry, but it should never be
   * handed something that is not even the right shape, and a sender can put any
   * type it likes behind a variant. */
  if (!g_variant_is_of_type(value, G_VARIANT_TYPE("a(iiay)"))) {
    return;
  }
  if (*dest != NULL) {
    g_variant_unref(*dest);
  }
  *dest = g_variant_ref(value);
  item->icon_dirty = TRUE;
}

static SofiTrayStatus parse_status(const gchar *status) {
  if (g_strcmp0(status, "NeedsAttention") == 0) {
    return SOFI_TRAY_STATUS_NEEDS_ATTENTION;
  }
  if (g_strcmp0(status, "Active") == 0) {
    return SOFI_TRAY_STATUS_ACTIVE;
  }
  /* Passive, and anything unrecognised. An item that reports a status this
   * version has never heard of is more usefully drawn than hidden. */
  return SOFI_TRAY_STATUS_PASSIVE;
}

static void apply_property(SofiTrayItemProxy *item, const gchar *name,
                           GVariant *value) {
  if (g_strcmp0(name, "Id") == 0) {
    set_string(&item->id, value);
  } else if (g_strcmp0(name, "Title") == 0) {
    set_string(&item->title, value);
  } else if (g_strcmp0(name, "Status") == 0) {
    if (g_variant_is_of_type(value, G_VARIANT_TYPE_STRING)) {
      SofiTrayStatus was = item->status;
      item->status = parse_status(g_variant_get_string(value, NULL));
      /* Status selects WHICH pixmap is effective, so changing it invalidates a
       * decode just as surely as receiving new pixels does. */
      if (was != item->status) {
        item->icon_dirty = TRUE;
      }
    }
  } else if (g_strcmp0(name, "IconName") == 0) {
    set_string(&item->icon_name, value);
  } else if (g_strcmp0(name, "AttentionIconName") == 0) {
    set_string(&item->attention_icon_name, value);
  } else if (g_strcmp0(name, "IconThemePath") == 0) {
    set_string(&item->icon_theme_path, value);
  } else if (g_strcmp0(name, "Menu") == 0) {
    set_string(&item->menu_path, value);
  } else if (g_strcmp0(name, "IconPixmap") == 0) {
    set_pixmap(item, &item->icon_pixmap, value);
  } else if (g_strcmp0(name, "AttentionIconPixmap") == 0) {
    set_pixmap(item, &item->attention_icon_pixmap, value);
  } else if (g_strcmp0(name, "ItemIsMenu") == 0) {
    if (g_variant_is_of_type(value, G_VARIANT_TYPE_BOOLEAN)) {
      item->is_menu = g_variant_get_boolean(value);
    }
  }
  /* Everything else -- Category, WindowId, ToolTip, OverlayIcon*,
   * AttentionMovieName -- is read and ignored on purpose. Storing a property
   * nothing draws is how a proxy layer turns into a second copy of the spec. */
}

/* ------------------------------------------------------------------ */
/* fetching                                                            */
/* ------------------------------------------------------------------ */

static void fetch_all(SofiTrayItemProxy *item);

/** Context for one single-property fallback fetch. */
typedef struct {
  SofiTrayItemProxy *item;
  gchar *name;
} PropFetch;

static void on_get_property(GObject *source, GAsyncResult *res,
                            gpointer user_data) {
  PropFetch *ctx = (PropFetch *)user_data;
  GError *error = NULL;
  GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), res,
                                                  &error);

  if (reply == NULL) {
    /* A missing property is ordinary: the specification marks most of them
     * optional and plenty of items implement only a handful. */
    g_debug("%s.%s unavailable: %s", ctx->item->service, ctx->name,
            error != NULL ? error->message : "unknown error");
    g_clear_error(&error);
    g_free(ctx->name);
    g_free(ctx);
    return;
  }

  GVariant *value = NULL;
  g_variant_get(reply, "(v)", &value);
  apply_property(ctx->item, ctx->name, value);
  g_variant_unref(value);
  g_variant_unref(reply);

  notify_changed();

  g_free(ctx->name);
  g_free(ctx);
}

/**
 * Function purpose: fetch the properties one at a time.
 *
 * The fallback for an item whose GetAll fails. That happens: GetAll is only as
 * reliable as an item's own property implementation, and a single property that
 * throws takes the whole batch down with it, so an item can be perfectly usable
 * and still answer nothing to a bulk request. Asking individually costs more
 * round trips and returns whatever the item can actually supply.
 */
static void fetch_individually(SofiTrayItemProxy *item) {
  static const gchar *const wanted[] = {
      "Id",       "Title",         "Status",     "IconName",
      "IconPixmap", "AttentionIconName", "AttentionIconPixmap",
      "IconThemePath", "ItemIsMenu", "Menu",     NULL};

  for (gsize i = 0; wanted[i] != NULL; i++) {
    PropFetch *ctx = g_malloc0(sizeof(PropFetch));
    ctx->item = item;
    ctx->name = g_strdup(wanted[i]);

    g_dbus_connection_call(
        item->connection, item->bus_name, item->object_path,
        PROPERTIES_INTERFACE, "Get",
        g_variant_new("(ss)", ITEM_INTERFACE, wanted[i]), G_VARIANT_TYPE("(v)"),
        G_DBUS_CALL_FLAGS_NO_AUTO_START, ITEM_CALL_TIMEOUT_MS,
        item->cancellable, on_get_property, ctx);
  }
}

static void on_get_all(GObject *source, GAsyncResult *res, gpointer user_data) {
  SofiTrayItemProxy *item = (SofiTrayItemProxy *)user_data;
  GError *error = NULL;
  GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), res,
                                                  &error);

  if (reply == NULL) {
    /* Action purpose: a cancelled call means the item is being freed and this
     * pointer is already gone -- it must not be touched, and there is nothing
     * to retry for an item that no longer exists. */
    if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
      g_clear_error(&error);
      return;
    }
    g_debug("GetAll failed for %s (%s); falling back to per-property reads",
            item->service, error != NULL ? error->message : "unknown error");
    g_clear_error(&error);
    fetch_individually(item);
    return;
  }

  GVariantIter *iter = NULL;
  const gchar *name = NULL;
  GVariant *value = NULL;

  g_variant_get(reply, "(a{sv})", &iter);
  while (g_variant_iter_next(iter, "{&sv}", &name, &value)) {
    apply_property(item, name, value);
    g_variant_unref(value);
  }
  g_variant_iter_free(iter);
  g_variant_unref(reply);

  /* Logs the EFFECTIVE icon, not the raw property: the attention override is
   * the part of this worth being able to see went right. */
  g_debug("Tray item %s: id='%s' title='%s' icon='%s' status=%d menu='%s' "
          "is-menu=%d theme-path='%s' pixmap=%s",
          item->service, item->id, sofi_tray_item_title(item),
          sofi_tray_item_icon_name(item), (int)item->status, item->menu_path,
          (int)item->is_menu, item->icon_theme_path,
          sofi_tray_item_icon_pixmap(item) != NULL ? "yes" : "no");

  /* Action purpose: decode here in the debug path ONLY so the gate can observe
   * the result. Real callers reach it lazily through
   * sofi_tray_item_icon_argb32(), which is the point of the dirty flag -- an
   * applet that repaints constantly and is never looked at costs nothing. */
  if (G_UNLIKELY(g_getenv("SOFI_TRAY_EAGER_DECODE") != NULL)) {
    const guint8 *px = NULL;
    gint w = 0, h = 0;
    if (sofi_tray_item_icon_argb32(item, &px, NULL, &w, &h)) {
      g_debug("Eager decode %s: %dx%d first-pixel=0x%08X", item->service, w, h,
              *(const guint32 *)px);
    } else {
      g_debug("Eager decode %s: no usable pixmap", item->service);
    }
  }

  notify_changed();
}

static void fetch_all(SofiTrayItemProxy *item) {
  g_dbus_connection_call(item->connection, item->bus_name, item->object_path,
                         PROPERTIES_INTERFACE, "GetAll",
                         g_variant_new("(s)", ITEM_INTERFACE),
                         G_VARIANT_TYPE("(a{sv})"),
                         G_DBUS_CALL_FLAGS_NO_AUTO_START, ITEM_CALL_TIMEOUT_MS,
                         item->cancellable, on_get_all, item);
}

/**
 * Function purpose: react to an item saying something about it changed.
 *
 * Every signal is handled the same way -- re-fetch everything -- and that is a
 * decision rather than laziness. StatusNotifierItem's change signals carry no
 * payload: NewIcon says an icon changed and nothing more, so a fetch is required
 * whatever arrives. Mapping each signal to its own property would mean encoding
 * a table of which signal implies which properties, and items disagree about
 * that in practice -- NewStatus routinely accompanies a changed icon, because
 * the attention icon IS a different property.
 *
 * Notably these are NOT PropertiesChanged. Most items never emit that at all,
 * which is why a GDBusProxy's property cache is useless here and this file
 * keeps its own state.
 */
static gboolean on_refetch_due(gpointer user_data) {
  SofiTrayItemProxy *item = (SofiTrayItemProxy *)user_data;

  item->refetch_source = 0;
  fetch_all(item);
  return G_SOURCE_REMOVE;
}

static void on_item_signal(G_GNUC_UNUSED GDBusConnection *connection,
                           G_GNUC_UNUSED const gchar *sender,
                           G_GNUC_UNUSED const gchar *object_path,
                           G_GNUC_UNUSED const gchar *interface_name,
                           const gchar *signal_name,
                           G_GNUC_UNUSED GVariant *parameters,
                           gpointer user_data) {
  SofiTrayItemProxy *item = (SofiTrayItemProxy *)user_data;

  g_debug("Tray item %s emitted %s", item->service, signal_name);

  /* Action purpose: coalesce. A fetch already scheduled will read whatever is
   * current when it runs, so a second signal arriving first needs no second
   * fetch -- this re-reads every property regardless of which one changed. */
  if (item->refetch_source > 0) {
    return;
  }
  item->refetch_source =
      g_timeout_add(ITEM_REFETCH_DEBOUNCE_MS, on_refetch_due, item);
}

/* ------------------------------------------------------------------ */
/* lifetime                                                            */
/* ------------------------------------------------------------------ */

SofiTrayItemProxy *sofi_tray_item_new(GDBusConnection *connection,
                                      const gchar *service,
                                      const gchar *bus_name,
                                      const gchar *object_path) {
  /* Action purpose: the connection is handed in rather than fetched. This was
   * g_bus_get_sync(), which is effectively free once GLib has cached the
   * session bus -- but it is still a synchronous call, and it sat in the path a
   * registering application drives. The caller already holds the connection it
   * received the registration on; there was never a reason to ask again. */
  if (connection == NULL) {
    g_warning("No session bus connection for tray item %s.", service);
    return NULL;
  }

  SofiTrayItemProxy *item = g_malloc0(sizeof(SofiTrayItemProxy));
  item->service = g_strdup(service);
  item->bus_name = g_strdup(bus_name);
  item->object_path = g_strdup(object_path);
  item->connection = g_object_ref(connection);
  item->cancellable = g_cancellable_new();
  item->status = SOFI_TRAY_STATUS_PASSIVE;

  /* Every string accessor promises non-NULL, so they start empty rather than
   * unset and no caller has to test. */
  item->id = g_strdup("");
  item->title = g_strdup("");
  item->icon_name = g_strdup("");
  item->attention_icon_name = g_strdup("");
  item->icon_theme_path = g_strdup("");
  item->menu_path = g_strdup("");

  /* Action purpose: subscribe BEFORE the first fetch. An item that changes
   * between the two would otherwise be missed entirely -- the fetch would carry
   * the old value and the signal announcing the new one would have arrived
   * while nothing was listening. */
  item->signal_sub = g_dbus_connection_signal_subscribe(
      item->connection, item->bus_name, ITEM_INTERFACE, NULL, item->object_path,
      NULL, G_DBUS_SIGNAL_FLAGS_NONE, on_item_signal, item, NULL);

  fetch_all(item);

  return item;
}

void sofi_tray_item_free(SofiTrayItemProxy *item) {
  if (item == NULL) {
    return;
  }

  /* Action purpose: cancel first. A fetch in flight holds this pointer as its
   * user_data, and completing after the free would be a use-after-free driven
   * by whichever application happened to be slow. */
  g_cancellable_cancel(item->cancellable);

  /* A debounced fetch still pending holds this pointer just as a call in flight
   * does, and a GSource is not cancelled by the GCancellable. */
  if (item->refetch_source > 0) {
    g_source_remove(item->refetch_source);
    item->refetch_source = 0;
  }

  if (item->signal_sub > 0) {
    g_dbus_connection_signal_unsubscribe(item->connection, item->signal_sub);
    item->signal_sub = 0;
  }

  g_object_unref(item->cancellable);
  g_object_unref(item->connection);

  g_free(item->service);
  g_free(item->bus_name);
  g_free(item->object_path);
  g_free(item->id);
  g_free(item->title);
  g_free(item->icon_name);
  g_free(item->attention_icon_name);
  g_free(item->icon_theme_path);
  g_free(item->menu_path);

  if (item->icon_pixmap != NULL) {
    g_variant_unref(item->icon_pixmap);
  }
  if (item->attention_icon_pixmap != NULL) {
    g_variant_unref(item->attention_icon_pixmap);
  }
  g_free(item->icon_argb);

  g_free(item);
}

/* ------------------------------------------------------------------ */
/* accessors                                                           */
/* ------------------------------------------------------------------ */

const gchar *sofi_tray_item_service(const SofiTrayItemProxy *item) {
  return item == NULL ? "" : item->service;
}

const gchar *sofi_tray_item_id(const SofiTrayItemProxy *item) {
  return item == NULL ? "" : item->id;
}

const gchar *sofi_tray_item_title(const SofiTrayItemProxy *item) {
  if (item == NULL) {
    return "";
  }
  /* Action purpose: Title is optional and a great many items never set it,
   * while Id is mandatory. Falling back here rather than at the drawing site
   * keeps every caller from reimplementing the same guess. */
  if (item->title != NULL && item->title[0] != '\0') {
    return item->title;
  }
  return item->id;
}

SofiTrayStatus sofi_tray_item_status(const SofiTrayItemProxy *item) {
  return item == NULL ? SOFI_TRAY_STATUS_PASSIVE : item->status;
}

const gchar *sofi_tray_item_icon_name(const SofiTrayItemProxy *item) {
  if (item == NULL) {
    return "";
  }
  if (item->status == SOFI_TRAY_STATUS_NEEDS_ATTENTION &&
      item->attention_icon_name != NULL &&
      item->attention_icon_name[0] != '\0') {
    return item->attention_icon_name;
  }
  return item->icon_name;
}

const gchar *sofi_tray_item_icon_theme_path(const SofiTrayItemProxy *item) {
  return item == NULL ? "" : item->icon_theme_path;
}

GVariant *sofi_tray_item_icon_pixmap(const SofiTrayItemProxy *item) {
  if (item == NULL) {
    return NULL;
  }
  if (item->status == SOFI_TRAY_STATUS_NEEDS_ATTENTION &&
      item->attention_icon_pixmap != NULL) {
    return item->attention_icon_pixmap;
  }
  return item->icon_pixmap;
}

/* ------------------------------------------------------------------ */
/* icon decode -- the one place here taking hostile input               */
/* ------------------------------------------------------------------ */

/**
 * Function purpose: choose which of an item's offered sizes to decode.
 *
 * Items commonly ship several -- 16, 22, 24, 32 -- and the array is in no
 * defined order. The smallest that is at least the preferred size wins, because
 * a widget scales down cleanly and up badly; if every entry is smaller, the
 * largest of them is the best available.
 *
 * Entries that fail validation are skipped rather than aborting the search: one
 * malformed size in an array must not cost the item an icon it also supplied
 * correctly.
 */
static gboolean choose_pixmap(GVariant *pixmap, gint *out_w, gint *out_h,
                              GVariant **out_bytes) {
  GVariantIter iter;
  gint32 w = 0, h = 0;
  GVariant *bytes = NULL;
  gint best_w = 0, best_h = 0;
  GVariant *best = NULL;

  g_variant_iter_init(&iter, pixmap);

  while (g_variant_iter_next(&iter, "(ii@ay)", &w, &h, &bytes)) {
    gsize length = 0;
    const guchar *data = g_variant_get_fixed_array(bytes, &length, 1);

    /* Action purpose: every one of these is a value the SENDER chose, and this
     * runs before a single pixel is read. The multiplication cannot overflow
     * because the dimension cap is applied first. */
    gboolean sane = w > 0 && h > 0 && w <= ITEM_ICON_MAX_DIM &&
                    h <= ITEM_ICON_MAX_DIM && data != NULL &&
                    length >= (gsize)w * (gsize)h * 4u;

    if (!sane) {
      g_debug("Refusing a tray pixmap: %dx%d, %" G_GSIZE_FORMAT " bytes", w, h,
              length);
      g_variant_unref(bytes);
      continue;
    }

    gboolean better;
    if (best == NULL) {
      better = TRUE;
    } else if (best_w < ITEM_ICON_PREFERRED_DIM) {
      /* Nothing big enough yet: anything larger is an improvement. */
      better = w > best_w;
    } else {
      /* Already have one big enough: only a smaller one that is still big
       * enough improves on it. */
      better = w >= ITEM_ICON_PREFERRED_DIM && w < best_w;
    }

    if (better) {
      if (best != NULL) {
        g_variant_unref(best);
      }
      best = bytes;
      best_w = w;
      best_h = h;
    } else {
      g_variant_unref(bytes);
    }
  }

  if (best == NULL) {
    return FALSE;
  }

  *out_w = best_w;
  *out_h = best_h;
  *out_bytes = best;
  return TRUE;
}

/**
 * Function purpose: turn one validated pixmap into what cairo wants.
 *
 * Two conversions, both mandatory and neither obvious from the type:
 *
 * The wire format is ARGB32 in **network byte order** -- the bytes arrive
 * A, R, G, B in that order regardless of the machine. cairo's ARGB32 is a
 * native-endian 32-bit word, so on a little-endian host the bytes must be
 * reassembled rather than copied.
 *
 * And the wire format is **straight** alpha while cairo's is **premultiplied**.
 * Copying without multiplying produces icons with bright halos wherever they are
 * translucent, which is most anti-aliased icon edges.
 */
static void decode_icon(SofiTrayItemProxy *item) {
  item->icon_dirty = FALSE;

  g_free(item->icon_argb);
  item->icon_argb = NULL;
  item->icon_argb_width = 0;
  item->icon_argb_height = 0;

  GVariant *pixmap = sofi_tray_item_icon_pixmap(item);
  if (pixmap == NULL) {
    return;
  }

  gint w = 0, h = 0;
  GVariant *bytes = NULL;
  if (!choose_pixmap(pixmap, &w, &h, &bytes)) {
    return;
  }

  gsize length = 0;
  const guchar *src = g_variant_get_fixed_array(bytes, &length, 1);
  guint32 *dst = g_malloc((gsize)w * (gsize)h * 4u);

  for (gsize i = 0, n = (gsize)w * (gsize)h; i < n; i++) {
    const guchar *p = src + i * 4u;
    guint a = p[0], r = p[1], g = p[2], b = p[3];

    if (a == 0) {
      dst[i] = 0;
      continue;
    }
    if (a != 0xFF) {
      r = (r * a) / 0xFF;
      g = (g * a) / 0xFF;
      b = (b * a) / 0xFF;
    }
    dst[i] = ((guint32)a << 24) | ((guint32)r << 16) | ((guint32)g << 8) |
             (guint32)b;
  }

  item->icon_argb = (guint8 *)dst;
  item->icon_argb_width = w;
  item->icon_argb_height = h;

  g_variant_unref(bytes);

  g_debug("Decoded tray icon for %s: %dx%d", item->service, w, h);
}

gboolean sofi_tray_item_icon_argb32(SofiTrayItemProxy *item,
                                    const guint8 **data, gsize *length,
                                    gint *width, gint *height) {
  if (item == NULL) {
    return FALSE;
  }
  if (item->icon_dirty) {
    decode_icon(item);
  }
  if (item->icon_argb == NULL) {
    return FALSE;
  }

  if (data != NULL) {
    *data = item->icon_argb;
  }
  if (length != NULL) {
    *length = (gsize)item->icon_argb_width * (gsize)item->icon_argb_height * 4u;
  }
  if (width != NULL) {
    *width = item->icon_argb_width;
  }
  if (height != NULL) {
    *height = item->icon_argb_height;
  }
  return TRUE;
}

gboolean sofi_tray_item_is_menu(const SofiTrayItemProxy *item) {
  return item == NULL ? FALSE : item->is_menu;
}

const gchar *sofi_tray_item_menu_path(const SofiTrayItemProxy *item) {
  return item == NULL ? "" : item->menu_path;
}

/* ------------------------------------------------------------------ */
/* activation                                                          */
/* ------------------------------------------------------------------ */

static void call_activation(SofiTrayItemProxy *item, const gchar *method,
                            gint x, gint y) {
  if (item == NULL) {
    return;
  }
  /* Fire and forget: NULL reply type and NULL callback. An application that
   * declines to be activated is not something the user can act on, and raising
   * a dialog for somebody else's bug is worse than the click doing nothing. */
  g_dbus_connection_call(item->connection, item->bus_name, item->object_path,
                         ITEM_INTERFACE, method, g_variant_new("(ii)", x, y),
                         NULL, G_DBUS_CALL_FLAGS_NO_AUTO_START,
                         ITEM_CALL_TIMEOUT_MS, item->cancellable, NULL, NULL);
}

void sofi_tray_item_activate(SofiTrayItemProxy *item, gint x, gint y) {
  call_activation(item, "Activate", x, y);
}

void sofi_tray_item_secondary_activate(SofiTrayItemProxy *item, gint x,
                                       gint y) {
  call_activation(item, "SecondaryActivate", x, y);
}

#endif // SYSTEM_TRAY
