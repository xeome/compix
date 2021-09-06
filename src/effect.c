#include "effect.h"
#include "session.h"
#include "util.h"
#include "window.h"
#include <string.h>

// TODO when an effect is replaced by another, we should clean all effect related variables

static effect *effect_dispatch_table[NUM_WINTYPES][NUM_EVENT_EFFECTS] = {{NULL}};
static effect *effects;

static void fade(win *w, double progress, void **effect_data) {
    if (*effect_data == NULL) {
        *effect_data = calloc(1, sizeof(double));
        *((double *) *effect_data) = w->opacity;
    }

    double lo = 0.0;
    double hi = *((double *) *effect_data);
    w->opacity = (progress * (hi - lo)) + lo;
}

static void pop(win *w, double progress, void **effect_data) {
    fade(w, progress, effect_data);
    double lo = 0.75;
    double hi = 1.0;
    w->scale = (progress * (hi - lo)) + lo;
}

static void slide_up(win *w, double progress, void **effect_data) {
    w->offset_y = -((w->attr.height * progress) - w->attr.height);
}

static void slide_down(win *w, double progress, void **effect_data) {
    w->offset_y = (w->attr.height * progress) - w->attr.height;
}

static void slide_left(win *w, double progress, void **effect_data) {
    w->offset_x = -((w->attr.width * progress) - w->attr.width);
}

static void slide_right(win *w, double progress, void **effect_data) {
    w->offset_x = (w->attr.width * progress) - w->attr.width;
}

// FIXME there is a bug where for a frame slide_right is used instead of slide_down for my awesomewm dock panel
// happens only at window creation (find a way to correct this and keep it compatible for all cases)
static void slide_auto(win *w, double progress, void **effect_data) {
    if (w->attr.width < w->attr.height) { // west or east
        int center_x = (w->attr.x + w->attr.width) / 2;
        if (center_x < s.root_width / 2) { // west
            slide_right(w, progress, effect_data);
        } else { // east
            slide_left(w, progress, effect_data);
        }
    } else { // north or south
        int center_y = (w->attr.y + w->attr.height) / 2;
        if (center_y < s.root_height / 2) { // north
            slide_down(w, progress, effect_data);
        } else { // south
            slide_up(w, progress, effect_data);
        }
    }
}

effect *effect_get(wintype window_type, event_effect event) {
    return effect_dispatch_table[window_type][event];
}

void effect_set(wintype window_type, event_effect event, effect *e) {
    effect_dispatch_table[window_type][event] = e;
}

static const char *event_effect_names[] = {"map-effect", "unmap-effect", "create-effect", "destroy-effect",
                                           "maximize-effect", "move-effect", "desktop-change-effect"};
const char *get_event_effect_name(event_effect effect) {
    if (effect >= NUM_EVENT_EFFECTS)
        return NULL;
    return event_effect_names[effect];
}

static const effect_func effect_funcs[] = {fade, pop, slide_auto, slide_up, slide_down, slide_left, slide_right};
static const char *effect_funcs_names[] = {"fade", "pop", "slide-auto", "slide-up", "slide-down", "slide-left", "slide-right"};
effect_func get_effect_func_from_name(const char *name) {
    unsigned int size = sizeof(effect_funcs_names) / sizeof(effect_funcs_names[0]);
    for (unsigned int i = 0; i < size; i++)
        if (strcmp(name, effect_funcs_names[i]) == 0)
            return effect_funcs[i];
    return NULL;
}

effect *effect_find(const char *name) {
    for (effect *e = effects; e; e = e->next) {
        if (strcmp(e->name, name) == 0)
            return e;
    }
    return NULL;
}

void effect_new(const char *name, const char *function_name, double step) {
    if (effect_find(name))
        return;
    effect *e = calloc(1, sizeof(effect));

    e->func = get_effect_func_from_name(function_name);
    if (!e->func) {
        free(e);
        return;
    }
    e->name = name;
    e->step = step;

    e->next = effects;
    effects = e;

    return;
}
