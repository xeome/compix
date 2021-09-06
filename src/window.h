#pragma once

#include <X11/Xlib.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/shape.h>

#define WINDOW_SOLID 0
#define WINDOW_TRANS 1
#define WINDOW_ARGB 2

typedef enum _winstate {
    WINSTATE_MAXIMIZED_VERT = 1,
    WINSTATE_MAXIMIZED_HORZ = 2,
    WINSTATE_FULLSCREEN = 4,
    NUM_WINSTATES = 5
} winstate;

typedef enum _wintype {
    WINTYPE_DESKTOP,
    WINTYPE_DOCK,
    WINTYPE_TOOLBAR, // toolbar detached from main window
    WINTYPE_MENU,    // pinnable menu detached from main window
    WINTYPE_UTILITY, // small persistent utility window (e.g. palette or toolbox)
    WINTYPE_SPLASH,  // splash window when an app is starting up
    WINTYPE_DIALOG,
    WINTYPE_DROPDOWN_MENU, // menu spawned from a menubar
    WINTYPE_POPUP_MENU,    // menu spawned from a right click
    WINTYPE_TOOLTIP,
    WINTYPE_NOTIFICATION,
    WINTYPE_COMBO, // window popped up by combo boxes
    WINTYPE_DND,   // drag ans drop window
    WINTYPE_NORMAL,
    NUM_WINTYPES,
    WINTYPE_UNKNOWN
} wintype;

typedef struct _win {
    struct _win *next;
    Window id;
    Pixmap pixmap;
    XWindowAttributes attr;

    // some programs do not put their properties their window but in a child window (see xterm)
    // so we store the window that holds these properties in this variable
    Window props_window_id;

    int mode;
    Bool maximize_state_changed;
    unsigned int state;
    Bool damaged;
    Damage damage;
    Picture picture;
    Picture alpha_picture;
    XserverRegion border_size;
    XserverRegion extents;
    wintype window_type;
    Bool shaped;
    XRectangle shape_bounds;

    double opacity;
    double scale;
    int offset_x;
    int offset_y;
    Bool need_effect; // used to apply effects when painting a window
    Bool action_running;

    /* for drawing translucent windows */
    XserverRegion border_clip;
    struct _win *prev_trans;
} win;

#define WIN_SET_STATE(w, wstate) w->state |= 1U << wstate
#define WIN_GET_STATE(w, wstate) (w->state >> wstate) & 1U

wintype get_wintype_from_name(const char *name);

win *find_win(Window id, Bool include_prop_window);

XserverRegion win_extents(win *w);

XserverRegion border_size(win *w);

void map_win(Window id);

void finish_unmap_win(win *w);

void unmap_win(Window id);

/* Get the opacity prop from window
   not found: default
   otherwise the value
 */
double get_opacity_prop(win *w, double def);

void determine_mode(win *w);

void determine_winstate(win *w);

void add_win(Window id);

void restack_win(win *w, Window new_above);

void configure_win(XConfigureEvent *ce);

void circulate_win(XCirculateEvent *ce);

void destroy_win(Window id, Bool gone);

void damage_win(XDamageNotifyEvent *de);

void shape_win(XShapeEvent *se);
