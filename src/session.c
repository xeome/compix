#include "session.h"
#include "action.h"
#include "config.h"
#include "effect.h"
#include "render.h"
#include "util.h"
#include "window.h"
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/shape.h>
#include <stdio.h>
#include <stdlib.h>

struct session s;

static XRectangle *expose_rects = NULL;
static int size_expose = 0;
static int n_expose = 0;

static void handle_event(XEvent ev) {
    if ((ev.type & 0x7f) != KeymapNotify)
        discard_ignore(ev.xany.serial);

#ifdef DEBUG
    print_event(ev);
#endif

    switch (ev.type) {
    case CreateNotify:
        add_win(ev.xcreatewindow.window);
        break;
    case ConfigureNotify:
        configure_win(&ev.xconfigure);
        break;
    case DestroyNotify:
        destroy_win(ev.xdestroywindow.window, True);
        break;
    case MapNotify:
        map_win(ev.xmap.window);
        break;
    case UnmapNotify:
        unmap_win(ev.xunmap.window);
        break;
    case ReparentNotify:
        if (ev.xreparent.parent == s.root)
            add_win(ev.xreparent.window);
        else
            destroy_win(ev.xreparent.window, False);
        break;
    case CirculateNotify:
        circulate_win(&ev.xcirculate);
        break;
    case Expose:
        if (ev.xexpose.window == s.root) {
            int more = ev.xexpose.count + 1;
            if (n_expose == size_expose)
                expose_rects = realloc(expose_rects, (size_expose += more) * sizeof(XRectangle));
            COPY_AREA(&expose_rects[n_expose], &ev.xexpose);
            n_expose++;
            if (ev.xexpose.count == 0) {
                add_damage(XFixesCreateRegion(s.dpy, expose_rects, n_expose));
                n_expose = 0;
            }
        }
        break;
    case PropertyNotify:
        // check if Trans property was changed
        if (ev.xproperty.atom == s.opacity_atom) {
            // reset mode and redraw window
            win *w = find_win(ev.xproperty.window, True);
            if (w) {
                w->opacity = get_opacity_prop(w, 1.0);
                determine_mode(w);
            }
        } else if (ev.xproperty.atom == s.winstate_atoms[NUM_WINSTATES]) {
            win *w = find_win(ev.xproperty.window, True);
            if (w)
                determine_winstate(w);
        } else if (s.root_tile && (ev.xproperty.atom == s.background_atoms[0] ||
                                   ev.xproperty.atom == s.background_atoms[1])) {
            XClearArea(s.dpy, s.root, 0, 0, 0, 0, True);
            XRenderFreePicture(s.dpy, s.root_tile);
            s.root_tile = None;
        }
        break;
    default:
        if (ev.type == s.damage_event + XDamageNotify) {
            damage_win((XDamageNotifyEvent *) &ev);
        } else if (ev.type == s.xshape_event + ShapeNotify) {
            shape_win((XShapeEvent *) &ev);
        }
        break;
    }
}

void session_loop(void) {
    for (;;) {
        do {
            // if no event in queue we run animations
            if (!QLength(s.dpy)) {
                if (poll(&s.ufd, 1, action_timeout()) == 0) {
                    action_run();
                    break;
                }
            }

            XEvent ev;
            XNextEvent(s.dpy, &ev);
            handle_event(ev);
        } while (QLength(s.dpy));
        if (s.all_damage) {
            paint_all(s.all_damage);
            XSync(s.dpy, False);
            s.all_damage = None;
            s.clip_changed = False;
        }
    }
}

static void register_composite_manager(void) {
    static char net_wm_cm[sizeof("_NET_WM_CM_S") + 3 * sizeof(s.screen)];
    Window w;
    Atom a, winNameAtom;
    XTextProperty tp;
    char **strs;
    int count;

    sprintf(net_wm_cm, "_NET_WM_CM_S%i", s.screen);
    a = XInternAtom(s.dpy, net_wm_cm, False);
    w = XGetSelectionOwner(s.dpy, a);

    if (w) {
        winNameAtom = XInternAtom(s.dpy, "_NET_WM_NAME", False);
        if (!XGetTextProperty(s.dpy, w, &tp, winNameAtom) && !XGetTextProperty(s.dpy, w, &tp, XA_WM_NAME))
            eprintf("another composite manager is already running (0x%lx)\n", (unsigned long int) w);
        if (!XmbTextPropertyToTextList(s.dpy, &tp, &strs, &count)) {
            fprintf(stderr, "another composite manager is already running (%s)\n", strs[0]);
            XFreeStringList(strs);
        }
        XFree(tp.value);
        exit(EXIT_FAILURE);
    }

    w = XCreateSimpleWindow(s.dpy, s.root, 0, 0, 1, 1, 0, None, None);
    Xutf8SetWMProperties(s.dpy, w, "axcomp", "axcomp", NULL, 0, NULL, NULL, NULL);
    XSetSelectionOwner(s.dpy, a, w, 0);
}

void session_init(const char *display, const char *config_path) {
    Window root_return, parent_return;
    Window *children;
    unsigned int nchildren;
    XRenderPictureAttributes pa;
    int composite_major, composite_minor;

    s.dpy = XOpenDisplay(display);
    if (!s.dpy)
        eprintf("cannot open display\n");
    XSetErrorHandler(handle_error);
    s.screen = DefaultScreen(s.dpy);
    s.root = RootWindow(s.dpy, s.screen);
    s.ufd.fd = XConnectionNumber(s.dpy);
    s.ufd.events = POLLIN;

    if (!XRenderQueryExtension(s.dpy, &s.render_event, &s.render_error))
        eprintf("No render extension\n");
    if (!XQueryExtension(s.dpy, COMPOSITE_NAME, &s.composite_opcode,
                         &s.composite_event, &s.composite_error))
        eprintf("No composite extension\n");
    XCompositeQueryVersion(s.dpy, &composite_major, &composite_minor);
    if (!(composite_major > 0 || composite_minor >= 2))
        eprintf("Requires composite extension version 0.2 or higher\n");

    if (!XDamageQueryExtension(s.dpy, &s.damage_event, &s.damage_error))
        eprintf("No damage extension\n");
    if (!XFixesQueryExtension(s.dpy, &s.xfixes_event, &s.xfixes_error))
        eprintf("No XFixes extension\n");
    if (!XShapeQueryExtension(s.dpy, &s.xshape_event, &s.xshape_error))
        eprintf("No XShape extension\n");

    register_composite_manager();

    // get atoms
    s.opacity_atom = XInternAtom(s.dpy, "_NET_WM_WINDOW_OPACITY", False);
    s.background_atoms[0] = XInternAtom(s.dpy, "_XROOTPMAP_ID", False);
    s.background_atoms[1] = XInternAtom(s.dpy, "_XSETROOT_ID", False);
    s.winstate_atoms[WINSTATE_MAXIMIZED_VERT] = XInternAtom(s.dpy, "_NET_WM_STATE_MAXIMIZED_VERT", False);
    s.winstate_atoms[WINSTATE_MAXIMIZED_HORZ] = XInternAtom(s.dpy, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
    s.winstate_atoms[WINSTATE_FULLSCREEN] = XInternAtom(s.dpy, "_NET_WM_STATE_FULLSCREEN", False);
    s.winstate_atoms[NUM_WINSTATES] = XInternAtom(s.dpy, "_NET_WM_STATE", False);
    s.wintype_atoms[WINTYPE_DESKTOP] = XInternAtom(s.dpy, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
    s.wintype_atoms[WINTYPE_DOCK] = XInternAtom(s.dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
    s.wintype_atoms[WINTYPE_TOOLBAR] = XInternAtom(s.dpy, "_NET_WM_WINDOW_TYPE_TOOLBAR", False);
    s.wintype_atoms[WINTYPE_MENU] = XInternAtom(s.dpy, "_NET_WM_WINDOW_TYPE_MENU", False);
    s.wintype_atoms[WINTYPE_UTILITY] = XInternAtom(s.dpy, "_NET_WM_WINDOW_TYPE_UTILITY", False);
    s.wintype_atoms[WINTYPE_SPLASH] = XInternAtom(s.dpy, "_NET_WM_WINDOW_TYPE_SPLASH", False);
    s.wintype_atoms[WINTYPE_DIALOG] = XInternAtom(s.dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
    s.wintype_atoms[WINTYPE_DROPDOWN_MENU] = XInternAtom(s.dpy, "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU", False);
    s.wintype_atoms[WINTYPE_POPUP_MENU] = XInternAtom(s.dpy, "_NET_WM_WINDOW_TYPE_POPUP_MENU", False);
    s.wintype_atoms[WINTYPE_TOOLTIP] = XInternAtom(s.dpy, "_NET_WM_WINDOW_TYPE_TOOLTIP", False);
    s.wintype_atoms[WINTYPE_NOTIFICATION] = XInternAtom(s.dpy, "_NET_WM_WINDOW_TYPE_NOTIFICATION", False);
    s.wintype_atoms[WINTYPE_COMBO] = XInternAtom(s.dpy, "_NET_WM_WINDOW_TYPE_COMBO", False);
    s.wintype_atoms[WINTYPE_DND] = XInternAtom(s.dpy, "_NET_WM_WINDOW_TYPE_DND", False);
    s.wintype_atoms[WINTYPE_NORMAL] = XInternAtom(s.dpy, "_NET_WM_WINDOW_TYPE_NORMAL", False);
    s.wintype_atoms[NUM_WINTYPES] = XInternAtom(s.dpy, "_NET_WM_WINDOW_TYPE", False);

    config_get(config_path);

    pa.subwindow_mode = IncludeInferiors;
    s.root_width = DisplayWidth(s.dpy, s.screen);
    s.root_height = DisplayHeight(s.dpy, s.screen);

    s.root_picture = XRenderCreatePicture(s.dpy, s.root,
                                          XRenderFindVisualFormat(s.dpy,
                                                                  DefaultVisual(s.dpy, s.screen)),
                                          CPSubwindowMode,
                                          &pa);
    s.all_damage = None;
    s.clip_changed = True;
    XGrabServer(s.dpy);
    XCompositeRedirectSubwindows(s.dpy, s.root, CompositeRedirectManual);
    XSelectInput(s.dpy, s.root,
                 SubstructureNotifyMask |
                     ExposureMask |
                     StructureNotifyMask |
                     PropertyChangeMask);
    XShapeSelectInput(s.dpy, s.root, ShapeNotifyMask);
    XQueryTree(s.dpy, s.root, &root_return, &parent_return, &children, &nchildren);
    for (int i = 0; i < nchildren; i++)
        add_win(children[i]);
    XFree(children);
    XUngrabServer(s.dpy);

    paint_all(None);
}
