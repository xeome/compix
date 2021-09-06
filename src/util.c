#include "util.h"
#include "session.h"
#include <X11/Xlib.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xrender.h>
#include <stdio.h>
#include <string.h>

#ifdef DEBUG
static const char *event_names[] = {
    "", "", "KeyPress", "KeyRelease", "ButtonPress", "ButtonRelease",
    "MotionNotify", "EnterNotify", "LeaveNotify", "FocusIn", "FocusOut",
    "KeymapNotify", "Expose", "GraphicsExpose", "NoExpose", "VisibilityNotify",
    "CreateNotify", "DestroyNotify", "UnmapNotify", "MapNotify", "MapRequest",
    "ReparentNotify", "ConfigureNotify", "ConfigureRequest", "GravityNotify",
    "ResizeRequest", "CirculateNotify", "CirculateRequest", "PropertyNotify",
    "SelectionClear", "SelectionRequest", "SelectionNotify", "ColormapNotify",
    "ClientMessage", "MappingNotify"};

int ev_serial(XEvent *ev) {
    if ((ev->type & 0x7f) != KeymapNotify)
        return ev->xany.serial;
    return NextRequest(ev->xany.display);
}

const char *ev_name(XEvent *ev) {
    static char buf[128];
    switch (ev->type & 0x7f) {
    case Expose:
        return "Expose";
    case MapNotify:
        return "Map";
    case UnmapNotify:
        return "Unmap";
    case ReparentNotify:
        return "Reparent";
    case CirculateNotify:
        return "Circulate";
    default:
        if (ev->type == s.damage_event + XDamageNotify) {
            return "Damage";
        } else {
            return event_names[ev->type];
        }
        return buf;
    }
}

Window ev_window(XEvent *ev) {
    switch (ev->type) {
    case Expose:
        return ev->xexpose.window;
    case MapNotify:
        return ev->xmap.window;
    case UnmapNotify:
        return ev->xunmap.window;
    case ReparentNotify:
        return ev->xreparent.window;
    case CirculateNotify:
        return ev->xcirculate.window;
    default:
        if (ev->type == s.damage_event + XDamageNotify) {
            return ((XDamageNotifyEvent *) ev)->drawable;
        }
        return 0;
    }
}
#endif

static unsigned long int *ignores = NULL;
static size_t n_ignores = 0, size_ignores = 0;

void discard_ignore(unsigned long int sequence) {
    size_t i;
    for (i = 0; i < n_ignores && sequence > ignores[i]; i++)
        ;
    memmove(ignores, &ignores[i], (n_ignores -= i) * sizeof(*ignores));
}

void set_ignore(unsigned long int sequence) {
    if (n_ignores == size_ignores)
        ignores = realloc(ignores, (size_ignores += 64) * sizeof(*ignores));
    ignores[n_ignores++] = sequence;
}

int should_ignore(unsigned long int sequence) {
    discard_ignore(sequence);
    return n_ignores && ignores[0] == sequence;
}

int handle_error(Display *display, XErrorEvent *ev) {
    const char *name = NULL;
    static char buffer[256];

    if (should_ignore(ev->serial))
        return 0;

    if (ev->request_code == s.composite_opcode && ev->minor_code == X_CompositeRedirectSubwindows)
        eprintf("another composite manager is already running\n");

    if (ev->error_code - s.xfixes_error == BadRegion) {
        name = "BadRegion";
    } else if (ev->error_code - s.damage_error == BadDamage) {
        name = "BadDamage";
    } else {
        switch (ev->error_code - s.render_error) {
        case BadPictFormat:
            name = "BadPictFormat";
            break;
        case BadPicture:
            name = "BadPicture";
            break;
        case BadPictOp:
            name = "BadPictOp";
            break;
        case BadGlyphSet:
            name = "BadGlyphSet";
            break;
        case BadGlyph:
            name = "BadGlyph";
            break;
        default:
            buffer[0] = '\0';
            XGetErrorText(display, ev->error_code, buffer, sizeof(buffer));
            name = buffer;
            break;
        }
    }

    fprintf(stderr, "error %i: %s request %i minor %i serial %lu\n",
            ev->error_code, *name ? name : "unknown",
            ev->request_code, ev->minor_code, ev->serial);

    return 0;
}
