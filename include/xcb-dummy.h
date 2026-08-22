#ifndef SOFI_XCB_DUMMY_H
#define SOFI_XCB_DUMMY_H

// Dummy types for xcb values shared in view interface (not used in wayland
// mode) This header exists so that sofi can be built without xcb headers
// present

typedef int xcb_configure_notify_event_t;
typedef int xcb_window_t;
typedef int xcb_timestamp_t;

#define XCB_WINDOW_NONE 0

#endif
