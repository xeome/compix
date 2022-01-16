#include "window.h"
#include "action.h"
#include "effect.h"
#include "render.h"
#include "session.h"
void *tmp_;
#include "util.h"
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/extensions/shape.h>
#include <stdlib.h>
#include <string.h>

static const char *wintypes_names[] = {"desktop", "dock", "toolbar", "menu", "utility",
                                       "splash", "dialog", "dropdown-menu", "popup-menu",
                                       "tooltip", "notification", "combo", "dnd", "normal"};

wintype get_wintype_from_name(const char *name) {
    for (int i = 0; i < NUM_WINTYPES; i++)
        if (strcmp(name, wintypes_names[i]) == 0)
            return i;
    return WINTYPE_UNKNOWN;
}

/*
 * returns the window that truly holds the window properties or 'None' if it could not find it
 */
static Window get_prop_window(Window id) {
    
    int n;
    Atom *props = XListProperties(s.dpy, id, &n);
    if (props)
        return id;

    unsigned int num_top_level_windows;
    Window returned_root, returned_parent, *top_level_windows;
    XQueryTree(s.dpy, id, &returned_root, &returned_parent, &top_level_windows, &num_top_level_windows);
    /*
    for (unsigned int i = 0; i < num_top_level_windows; i++) {
        Window prop_window = get_prop_window(top_level_windows[i]);
        if (prop_window)
            return prop_window;
    }*/

    return None;
}

win *find_win(Window id, Bool include_prop_window) {
    for (win *w = s.managed_windows; w; w = w->next)
        if (w->id == id || (include_prop_window && w->props_window_id == id))
            return w;
    return NULL;
}

XserverRegion win_extents(win *w) {
    XRectangle r;

    COPY_AREA(&r, &w->attr);
    r.width += w->attr.border_width * 2;
    r.height += w->attr.border_width * 2;

    return XFixesCreateRegion(s.dpy, &r, 1);
}

XserverRegion border_size(win *w) {
    XserverRegion border;
    /*
     * if window doesn't exist anymore,  this will generate an error
     * as well as not generate a region.  Perhaps a better XFixes
     * architecture would be to have a request that copies instead
     * of creates, that way you'd just end up with an empty region
     * instead of an invalid XID.
     */
    set_ignore(NextRequest(s.dpy));
    border = XFixesCreateRegionFromWindow(s.dpy, w->id, WindowRegionBounding);
    /* translate this */
    set_ignore(NextRequest(s.dpy));
    XFixesTranslateRegion(s.dpy, border,
                          w->attr.x + w->attr.border_width,
                          w->attr.y + w->attr.border_width);
    return border;
}

static wintype determine_wintype(win *w);

void map_win(Window id) {
    win *w = find_win(id, False);
    if (!w)
        return;

    w->attr.map_state = IsViewable;

    Bool is_being_created = w->window_type == WINTYPE_UNKNOWN;

    // we do window properties related stuff here and not at creation because at creation there are not always set
    if (is_being_created) {
        //if(w->attr.class != InputOnly && w->attr.map_state == 2 && w->attr.override_redirect == 0)
            w->props_window_id = get_prop_window(w->id);
        w->window_type = determine_wintype(w);
    }

    // This needs to be here or else we lose transparency messages
    XSelectInput(s.dpy, w->props_window_id, PropertyChangeMask);

    // This needs to be here since we don't get PropertyNotify when unmapped
    w->opacity = get_opacity_prop(w, 1.0);
    determine_mode(w);

    w->damaged = False;

    effect *e;
    if ((e = effect_get(w->window_type, is_being_created ? EVENT_WINDOW_CREATE : EVENT_WINDOW_MAP)))
        action_set(w, e, False, NULL, False, True);
}

void finish_unmap_win(win *w) {
    w->damaged = False;

    if (w->extents != None) {
        add_damage(w->extents); // destroys region
        w->extents = None;
    }

    if (w->pixmap) {
        XFreePixmap(s.dpy, w->pixmap);
        w->pixmap = None;
    }

    if (w->picture) {
        set_ignore(NextRequest(s.dpy));
        XRenderFreePicture(s.dpy, w->picture);
        w->picture = None;
    }

    // don't care about properties anymore
    set_ignore(NextRequest(s.dpy));
    XSelectInput(s.dpy, w->id, 0);

    if (w->border_size) {
        set_ignore(NextRequest(s.dpy));
        XFixesDestroyRegion(s.dpy, w->border_size);
        w->border_size = None;
    }
    if (w->border_clip) {
        XFixesDestroyRegion(s.dpy, w->border_clip);
        w->border_clip = None;
    }

    s.clip_changed = True;
}

static void unmap_callback(win *w, Bool gone) {
    finish_unmap_win(w);
}

void unmap_win(Window id) {
    win *w = find_win(id, False);
    if (!w)
        return;
    w->attr.map_state = IsUnmapped;
    effect *e;
    if ((e = effect_get(w->window_type, EVENT_WINDOW_UNMAP)) && w->pixmap)
        action_set(w, e, True, unmap_callback, False, False);
    else
        finish_unmap_win(w);
}

/* Get the opacity prop from window
   not found: def
   otherwise the value
 */
double get_opacity_prop(win *w, double def) {
    Atom actual;
    int format;
    unsigned long n, left;

    unsigned char *data;
    int result = XGetWindowProperty(s.dpy, w->props_window_id, s.opacity_atom, 0L, 1L, False,
                                    XA_CARDINAL, &actual, &format,
                                    &n, &left, &data);
    if (result == Success && data != NULL) {
        unsigned int i = *(unsigned int *) data;
        XFree((void *) data);
        return (double) i / OPAQUE;
    }
    return def;
}

void determine_mode(win *w) {
    int mode;
    XRenderPictFormat *format;

    if (w->alpha_picture) {
        XRenderFreePicture(s.dpy, w->alpha_picture);
        w->alpha_picture = None;
    }

    if (w->attr.class == InputOnly) {
        format = NULL;
    } else {
        format = XRenderFindVisualFormat(s.dpy, w->attr.visual);
    }

    if (format && format->type == PictTypeDirect && format->direct.alphaMask) {
        mode = WINDOW_ARGB;
    } else if (w->opacity < 1.0) {
        mode = WINDOW_TRANS;
    } else {
        mode = WINDOW_SOLID;
    }
    w->mode = mode;
    if (w->extents) {
        XserverRegion damage;
        damage = XFixesCreateRegion(s.dpy, NULL, 0);
        XFixesCopyRegion(s.dpy, damage, w->extents);
        add_damage(damage);
    }
}

static wintype determine_wintype(win *w) {
    Atom actual;
    int format;
    unsigned long n, left;

    unsigned char *data;
    // s.wintype_atoms[NUM_WINTYPES] is the _NET_WM_WINDOW_TYPE atom used to query a window type
    int result = XGetWindowProperty(s.dpy, w->props_window_id, s.wintype_atoms[NUM_WINTYPES], 0L, 1L, False,
                                    XA_ATOM, &actual, &format,
                                    &n, &left, &data);

    if (result == Success && data != (unsigned char *) None) {
        Atom a = *(Atom *) data;
        XFree((void *) data);

        for (int i = 0; i < NUM_WINTYPES; i++)
            if (s.wintype_atoms[i] == a)
                return i;
    }

    Window transient_for;
    result = XGetTransientForHint(s.dpy, w->id, &transient_for);
    if (!result)
        return WINTYPE_NORMAL;
    return WINTYPE_DIALOG;
}

void determine_winstate(win *w) {
    Atom actual;
    int format;
    unsigned long n, left;

    unsigned char *data;
    int result = XGetWindowProperty(s.dpy, w->props_window_id, s.winstate_atoms[NUM_WINSTATES], 0L, 12L, False,
                                    XA_ATOM, &actual, &format,
                                    &n, &left, &data);
    if (result == Success && data != NULL) {
        Bool prev_state = w->state;
        w->state = 0;
        Atom *a = (Atom *) data;
        for (int i = 0; i < n; i++) {
            if (a[i] == s.winstate_atoms[WINSTATE_MAXIMIZED_VERT]) {
                WIN_SET_STATE(w, WINSTATE_MAXIMIZED_VERT);
            } else if (a[i] == s.winstate_atoms[WINSTATE_MAXIMIZED_HORZ]) {
                WIN_SET_STATE(w, WINSTATE_MAXIMIZED_HORZ);
            } else if (a[i] == s.winstate_atoms[WINSTATE_FULLSCREEN]) {
                WIN_SET_STATE(w, WINSTATE_FULLSCREEN);
            }
        }
        if (prev_state != w->state)
            w->maximize_state_changed = True;

        XFree((void *) data);
    }
}

void add_win(Window id) {
    win *w = calloc(1, sizeof(win));
    w->id = id;
    set_ignore(NextRequest(s.dpy));
    if (!XGetWindowAttributes(s.dpy, id, &w->attr)) {
        free(w);
        return;
    }

    w->shaped = False;
    w->shape_bounds.x = w->attr.x;
    w->shape_bounds.y = w->attr.y;
    w->shape_bounds.width = w->attr.width;
    w->shape_bounds.height = w->attr.height;

    w->damaged = False;
    w->pixmap = None;
    w->picture = None;

    w->damage = w->attr.class == InputOnly ? None : XDamageCreate(s.dpy, id, XDamageReportNonEmpty);

    w->alpha_picture = None;
    w->border_size = None;
    w->extents = None;
    w->opacity = 1.0;
    w->border_clip = None;

    w->scale = 1.0;
    w->offset_x = 0;
    w->offset_y = 0;
    w->need_effect = False;
    w->action_running = False;

    w->maximize_state_changed = False;
    w->state = 0;

    w->prev_trans = NULL;

    w->window_type = WINTYPE_UNKNOWN;

    w->next = s.managed_windows;
    s.managed_windows = w;

    if (w->attr.map_state == IsViewable)
        map_win(id);
}

void restack_win(win *w, Window new_above) {
    Window old_above = w->next ? w->next->id : None;

    if (old_above != new_above) {
        win **prev;

        /* unhook */
        for (prev = &s.managed_windows; *prev; prev = &(*prev)->next)
            if ((*prev) == w)
                break;
        *prev = w->next;

        /* rehook */
        for (prev = &s.managed_windows; *prev; prev = &(*prev)->next) {
            if ((*prev)->id == new_above)
                break;
        }
        w->next = *prev;
        *prev = w;
    }
}

void configure_win(XConfigureEvent *ce) {
    win *w = find_win(ce->window, False);
    XserverRegion damage = None;

    if (!w) {
        if (ce->window == s.root) {
            if (s.root_buffer) {
                XRenderFreePicture(s.dpy, s.root_buffer);
                s.root_buffer = None;
            }
            s.root_width = ce->width;
            s.root_height = ce->height;
        }
        return;
    }

    // if maximize_state_changed and we are in a configure event, this is a real maximize/fullscreen state change
    // maybe add a check if the configure event if really for a window resize/move
    if (w->maximize_state_changed) {
        effect *e;
        if ((e = effect_get(w->window_type, EVENT_WINDOW_MAXIMIZE)) && w->pixmap)
            action_set(w, e, False, NULL, False, True);
        w->maximize_state_changed = False;
    }

    damage = XFixesCreateRegion(s.dpy, NULL, 0);
    if (w->extents != None)
        XFixesCopyRegion(s.dpy, damage, w->extents);

    w->shape_bounds.x -= w->attr.x;
    w->shape_bounds.y -= w->attr.y;

    if (w->attr.width != ce->width || w->attr.height != ce->height) {
        if (w->pixmap) {
            XFreePixmap(s.dpy, w->pixmap);
            w->pixmap = None;
            if (w->picture) {
                XRenderFreePicture(s.dpy, w->picture);
                w->picture = None;
            }
        }
    }

    COPY_AREA(&w->attr, ce);
    w->attr.border_width = ce->border_width;
    w->attr.override_redirect = ce->override_redirect;
    restack_win(w, ce->above);
    if (damage) {
        XserverRegion extents = win_extents(w);
        XFixesUnionRegion(s.dpy, damage, damage, extents);
        XFixesDestroyRegion(s.dpy, extents);
        add_damage(damage);
    }
    w->shape_bounds.x += w->attr.x;
    w->shape_bounds.y += w->attr.y;
    if (!w->shaped) {
        w->shape_bounds.width = w->attr.width;
        w->shape_bounds.height = w->attr.height;
    }

    s.clip_changed = True;
}

void circulate_win(XCirculateEvent *ce) {
    win *w = find_win(ce->window, False);
    Window new_above;

    if (!w)
        return;

    if (ce->place == PlaceOnTop)
        new_above = s.managed_windows->id;
    else
        new_above = None;
    restack_win(w, new_above);
    s.clip_changed = True;
}

static void finish_destroy_win(Window id, Bool gone) {
    win **prev, *w;
    for (prev = &s.managed_windows; (w = *prev); prev = &w->next) {
        if (w->id == id) {
            if (gone)
                finish_unmap_win(w);
            *prev = w->next;
            if (w->picture) {
                set_ignore(NextRequest(s.dpy));
                XRenderFreePicture(s.dpy, w->picture);
                w->picture = None;
            }
            if (w->alpha_picture) {
                XRenderFreePicture(s.dpy, w->alpha_picture);
                w->alpha_picture = None;
            }
            if (w->damage != None) {
                set_ignore(NextRequest(s.dpy));
                XDamageDestroy(s.dpy, w->damage);
                w->damage = None;
            }
            action_cleanup(w);
            free(w);
            break;
        }
    }
}

static void destroy_callback(win *w, Bool gone) {
    finish_destroy_win(w->id, gone);
}

void destroy_win(Window id, Bool gone) {
    win *w = find_win(id, False);
    effect *e;
    if (w && (e = effect_get(w->window_type, EVENT_WINDOW_DESTROY)) && w->pixmap)
        action_set(w, e, True, destroy_callback, gone, False);
    else
        finish_destroy_win(id, gone);
}

void damage_win(XDamageNotifyEvent *de) {
    XserverRegion parts;
    win *w = find_win(de->drawable, False);
    if (!w)
        return;

    if (!w->damaged) {
        parts = win_extents(w);
        set_ignore(NextRequest(s.dpy));
        XDamageSubtract(s.dpy, w->damage, None, None);
    } else {
        parts = XFixesCreateRegion(s.dpy, NULL, 0);
        set_ignore(NextRequest(s.dpy));
        XDamageSubtract(s.dpy, w->damage, None, parts);
        XFixesTranslateRegion(s.dpy, parts,
                              w->attr.x + w->attr.border_width,
                              w->attr.y + w->attr.border_width);
    }
    add_damage(parts);
    w->damaged = True;
}

void shape_win(XShapeEvent *se) {
    win *w = find_win(se->window, False);

    if (!w)
        return;

    if (se->kind == ShapeClip || se->kind == ShapeBounding) {
        XserverRegion region0;
        XserverRegion region1;

        s.clip_changed = True;

        region0 = XFixesCreateRegion(s.dpy, &w->shape_bounds, 1);

        if (se->shaped == True) {
            w->shaped = True;
            w->shape_bounds.x = w->attr.x + se->x;
            w->shape_bounds.y = w->attr.y + se->y;
            w->shape_bounds.width = se->width;
            w->shape_bounds.height = se->height;
        } else {
            w->shaped = False;
            w->shape_bounds.x = w->attr.x;
            w->shape_bounds.y = w->attr.y;
            w->shape_bounds.width = w->attr.width;
            w->shape_bounds.height = w->attr.height;
        }

        region1 = XFixesCreateRegion(s.dpy, &w->shape_bounds, 1);
        XFixesUnionRegion(s.dpy, region0, region0, region1);
        XFixesDestroyRegion(s.dpy, region1);

        /* ask for repaint of the old and new region */
        paint_all(region0);
    }
}
