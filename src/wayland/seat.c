#include <unistd.h>

#include <wlr/backend/libinput.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_primary_selection_v1.h>

#include "seat.h"
#include "xdg_shell.h"
#include "client.h"

static void
deiconify_view (Client *view)
{
    if (view->xdg_toplevel->requested.minimized) {
        view->xdg_toplevel->requested.minimized = false;
        wl_signal_emit(&view->xdg_toplevel->events.request_minimize, NULL);
    }
}

static void
keyboard_handle_destroy (struct wl_listener *listener, void *data)
{
    /* This event is raised by the keyboard base wlr_input_device to signal
     * the destruction of the wlr_keyboard. It will no longer receive events
     * and should be destroyed.
     */
    Keyboard *keyboard =
        wl_container_of(listener, keyboard, destroy);
    wl_list_remove(&keyboard->modifiers.link);
    wl_list_remove(&keyboard->key.link);
    wl_list_remove(&keyboard->destroy.link);
    wl_list_remove(&keyboard->link);
    free(keyboard);
}

static void
keyboard_handle_modifiers (struct wl_listener *listener, void *data)
{
    /* This event is raised when a modifier key, such as shift or alt, is
     * pressed. We simply communicate this to the client. */
    Keyboard *keyboard =
        wl_container_of(listener, keyboard, modifiers);
    /*
     * A seat can only have one keyboard, but this is a limitation of the
     * Wayland protocol - not wlroots. We assign all connected keyboards to the
     * same seat. You can swap out the underlying wlr_keyboard like this and
     * wlr_seat handles this transparently.
     */
    wlr_seat_set_keyboard(keyboard->server->seat->seat, keyboard->keyboard);
    /* Send modifiers to the client. */
    wlr_seat_keyboard_notify_modifiers(keyboard->server->seat->seat,
        &keyboard->keyboard->modifiers);
}

static bool
handle_keybinding (xfwmWaylandCompositor *server, xkb_keysym_t sym, uint32_t modifiers)
{
    /*
     * Here we handle compositor keybindings. This is when the compositor is
     * processing keys, rather than passing them on to the client for its own
     * processing.
     */
    
    struct wlr_session *session = wlr_backend_get_session (server->backend);
    
    if (modifiers & (WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT) &&
        sym >= XKB_KEY_XF86Switch_VT_1 &&
        sym <= XKB_KEY_XF86Switch_VT_12) {
        unsigned int vt = sym - XKB_KEY_XF86Switch_VT_1 + 1;
        wlr_session_change_vt (session, vt);
            
        return true;
    }	
        
        if (modifiers & WLR_MODIFIER_ALT && sym == XKB_KEY_Tab)
        {
            /* Cycle to the next view */
            if (wl_list_length(&server->views) >= 1) {
            Client *current_view = wl_container_of(
                server->views.prev, current_view, link);
            deiconify_view(current_view);
            focus_view(current_view, current_view->xdg_toplevel->base->surface);
            }
        }		
        else if (sym == XKB_KEY_Escape && modifiers & WLR_MODIFIER_CTRL)
            gtk_main_quit ();
        else
            return false;
        return true;
}

static void
keyboard_handle_key (struct wl_listener *listener, void *data)
{
    /* This event is raised when a key is pressed or released. */
    Keyboard *keyboard =
        wl_container_of(listener, keyboard, key);
    xfwmWaylandCompositor *server = keyboard->server;
    struct wlr_keyboard_key_event *event = data;
    struct wlr_seat *seat = server->seat->seat;

    /* Translate libinput keycode -> xkbcommon */
    uint32_t keycode = event->keycode + 8;
    /* Get a list of keysyms based on the keymap for this keyboard */
    const xkb_keysym_t *syms;
    int nsyms = xkb_state_key_get_syms(
            keyboard->keyboard->xkb_state, keycode, &syms);

    bool handled = false;
    uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->keyboard);
    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        /* If this button was _pressed_, we attempt to
         * process it as a compositor keybinding. */
        for (int i = 0; i < nsyms; i++) {
            handled = handle_keybinding(server, syms[i], modifiers);
        }
    }

    if (!handled) {
        /* Otherwise, we pass it along to the client. */
        wlr_seat_set_keyboard(seat, keyboard->keyboard);
        wlr_seat_keyboard_notify_key(seat, event->time_msec,
            event->keycode, event->state);
    }

    wlr_idle_notifier_v1_notify_activity(server->idle_notifier, seat);
}

static void
handle_new_keyboard (xfwmWaylandCompositor *server,
                     struct wlr_input_device *device)
{
    struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);

    Keyboard *keyboard =
        calloc(1, sizeof(Keyboard));
    keyboard->server = server;
    keyboard->keyboard = wlr_keyboard;

    /* We need to prepare an XKB keymap and assign it to the keyboard. This
     * assumes the defaults (e.g. layout = "us"). */
    struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    struct xkb_keymap *keymap = xkb_keymap_new_from_names(context, NULL,
        XKB_KEYMAP_COMPILE_NO_FLAGS);

    wlr_keyboard_set_keymap(wlr_keyboard, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);
    wlr_keyboard_set_repeat_info(wlr_keyboard, 25, 600);

    /* Here we set up listeners for keyboard events. */
    keyboard->modifiers.notify = keyboard_handle_modifiers;
    wl_signal_add(&keyboard->keyboard->events.modifiers, &keyboard->modifiers);
    keyboard->key.notify = keyboard_handle_key;
    wl_signal_add(&keyboard->keyboard->events.key, &keyboard->key);
    keyboard->destroy.notify = keyboard_handle_destroy;
    wl_signal_add(&device->events.destroy, &keyboard->destroy);

    wlr_seat_set_keyboard(server->seat->seat, keyboard->keyboard);

    /* And add the keyboard to our list of keyboards */
    wl_list_insert(&server->seat->keyboards, &keyboard->link);
}

static void
handle_new_pointer (xfwmWaylandCompositor *server,
                    struct wlr_input_device *device)
{
    /* We don't do anything special with pointers. All of our pointer handling
     * is proxied through wlr_cursor. On another compositor, you might take this
     * opportunity to do libinput configuration on the device to set
     * acceleration, etc. */
    wlr_cursor_attach_input_device(server->cursor->cursor, device);
}

static void
new_input_notify (struct wl_listener *listener, void *data)
{
    /* This event is raised by the backend when a new input device becomes
     * available. */
    xfwmWaylandCompositor *server =
        wl_container_of(listener, server, new_input);
    struct wlr_input_device *device = data;
    switch (device->type) {
    case WLR_INPUT_DEVICE_KEYBOARD:
        handle_new_keyboard(server, device);
        break;
    case WLR_INPUT_DEVICE_POINTER:
        handle_new_pointer(server, device);
        break;
    default:
        break;
    }
    /* We need to let the wlr_seat know what our capabilities are, which is
     * communiciated to the client. In xfwm4-wayland we always have a cursor, even if
     * there are no pointer devices, so we always include that capability. */
    uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
    if (!wl_list_empty(&server->keyboards)) {
        caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    }
    wlr_seat_set_capabilities(server->seat->seat, caps);
}

void
seat_focus_surface (Seat *seat, struct wlr_surface *surface)
{
    if (!surface) {
        wlr_seat_keyboard_notify_clear_focus(seat->seat);
        return;
    }

    struct wlr_keyboard *kb = wlr_seat_get_keyboard(seat->seat);
    if (kb != NULL) {
        wlr_seat_keyboard_notify_enter(seat->seat, surface, kb->keycodes,
            kb->num_keycodes, &kb->modifiers);
    }
}

void
seat_set_focus_layer (Seat *seat, struct wlr_layer_surface_v1 *layer)
{
    if (!layer) {
        seat->focused_layer = NULL;
        return;
    }
    seat_focus_surface(seat, layer->surface);
    if (layer->current.layer >= ZWLR_LAYER_SHELL_V1_LAYER_TOP) {
        seat->focused_layer = layer;
    }
}

static void
handle_request_set_primary_selection (struct wl_listener *listener,
                                      void *data)
{
    Seat *seat =
        wl_container_of(listener, seat, request_set_primary_selection);
    struct wlr_seat_request_set_primary_selection_event *event = data;
    wlr_seat_set_primary_selection(seat->seat, event->source, event->serial);
}

static void
handle_request_set_selection (struct wl_listener *listener, void *data)
{
    /* This event is raised by the seat when a client wants to set the selection,
     * usually when the user copies something. wlroots allows compositors to
     * ignore such requests if they so choose, but in tinywl we always honor
     */
    Seat *seat = wl_container_of(
            listener, seat, request_set_selection);
    struct wlr_seat_request_set_selection_event *event = data;
    wlr_seat_set_selection(seat->seat, event->source, event->serial);
}

Seat *
seatCreate (xfwmWaylandCompositor *server)
{
    Seat *seat = malloc (sizeof (Seat));

    wl_list_init(&seat->keyboards);
    server->new_input.notify = new_input_notify;
    wl_signal_add(&server->backend->events.new_input, &server->new_input);
    seat->seat = wlr_seat_create(server->wl_display, "seat0");

    seat->request_set_selection.notify = handle_request_set_selection;
    wl_signal_add(&seat->seat->events.request_set_selection,
            &seat->request_set_selection);
    seat->request_set_primary_selection.notify =
        handle_request_set_primary_selection;
    wl_signal_add (&seat->seat->events.request_set_primary_selection,
            &seat->request_set_primary_selection);

    return seat;
}

void
seatDestroy (Seat *seat)
{
    wl_list_remove(&seat->keyboards);
    wl_list_remove(&seat->request_set_primary_selection.link);
    wl_list_remove(&seat->request_set_selection.link);
    free(seat);
}