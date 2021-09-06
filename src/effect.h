#pragma once

#include "window.h"

// events that can trigger effects
typedef enum _event_effect {
    EVENT_WINDOW_MAP,
    EVENT_WINDOW_UNMAP,
    EVENT_WINDOW_CREATE,
    EVENT_WINDOW_DESTROY,
    EVENT_WINDOW_MAXIMIZE,
    EVENT_WINDOW_MOVE,
    EVENT_DESKTOP_CHANGE,
    NUM_EVENT_EFFECTS,
    EVENT_UNKOWN
} event_effect;

typedef void (*effect_func)(win *w, double progress, void **effect_data);

typedef struct _effect {
    struct _effect *next;
    const char *name;
    effect_func func;
    double step;
} effect;

effect_func get_effect_func_from_name(const char *name);

const char *get_event_effect_name(event_effect effect);

effect *effect_find(const char *name);

void effect_new(const char *name, const char *function_name, double step);

void effect_set(wintype window_type, event_effect event, effect *e);

effect *effect_get(wintype window_type, event_effect event);
