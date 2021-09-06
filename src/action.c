#include "effect.h"
#include "session.h"
#include "util.h"
#include "window.h"
#include <sys/time.h>

typedef struct _action {
    struct _action *next;
    win *w;
    double progress; // in interval [0,1]
    double start;    // either 0 or 1
    double end;      // either 1 or 0
    double step;
    void (*callback)(win *w, Bool gone);
    effect_func effect;
    void *effect_data;
    Bool gone;
} action;

static action *actions;
static int effect_time = 0;

static int get_time_in_milliseconds(void) {
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static action *action_find(win *w) {
    for (action *a = actions; a; a = a->next) {
        if (a->w == w)
            return a;
    }
    return NULL;
}

static void action_dequeue(action *a) {
    for (action **prev = &actions; *prev; prev = &(*prev)->next) {
        if (*prev == a) {
            *prev = a->next;
            if (a->callback)
                (*a->callback)(a->w, a->gone);
            if (a->effect_data)
                free(a->effect_data);
            free(a);
            break;
        }
    }
}

void action_cleanup(win *w) {
    action *a = action_find(w);
    if (a)
        action_dequeue(a);
}

static void action_enqueue(action *a) {
    if (!actions)
        effect_time = get_time_in_milliseconds() + s.effect_delta;
    a->next = actions;
    actions = a;
}

void action_set(win *w, effect *e, Bool reverse, void (*callback)(win *w, Bool gone), Bool gone, Bool exec_callback) {
    double start = reverse ? 1.0 : 0.0;
    double end = reverse ? 0.0 : 1.0;

    action *a = action_find(w);
    if (!a) {
        a = malloc(sizeof(action));
        a->next = NULL;
        a->w = w;
        a->progress = start;
        action_enqueue(a);
    } else if (exec_callback && a->callback) {
        (*a->callback)(a->w, a->gone);
    }

    a->end = end;
    if (a->progress < end)
        a->step = e->step;
    else if (a->progress > end)
        a->step = -e->step;
    a->callback = callback;
    a->effect = e->func;
    a->effect_data = NULL;
    a->gone = gone;

    (*a->effect)(w, a->progress, &a->effect_data);
}

int action_timeout(void) {
    if (!actions)
        return -1;
    int now = get_time_in_milliseconds();
    int delta = effect_time - now;
    if (delta < 0)
        delta = 0;
    return delta;
}

void action_run(void) {
    int now = get_time_in_milliseconds();
    action *next = actions;
    int steps;
    Bool need_dequeue;

    if (effect_time - now > 0)
        return;
    steps = 1 + (now - effect_time) / s.effect_delta;

    while (next) {
        action *a = next;
        win *w = a->w;
        next = a->next;
        a->progress += a->step * steps;
        if (a->progress >= 1)
            a->progress = 1;
        else if (a->progress < 0)
            a->progress = 0;

        (*a->effect)(w, a->progress, &a->effect_data);
        w->action_running = True;
        need_dequeue = False;
        if (a->step > 0) {
            if (a->progress >= a->end) {
                (*a->effect)(w, a->end, &a->effect_data);
                need_dequeue = True;
            }
        } else {
            if (a->progress <= a->end) {
                (*a->effect)(w, a->end, &a->effect_data);
                need_dequeue = True;
            }
        }
        // maybe don't use determine_mode here to avoid the ugly fix below and find a better way
        determine_mode(w);
        // this is ugly : we force the window to never be solid while an action is running
        // this prevents painting glitches
        w->mode = w->mode == WINDOW_SOLID ? WINDOW_ARGB : w->mode;

        // Must do this last as it might destroy a->w in callbacks
        if (need_dequeue) {
            determine_mode(w); // this is ugly (no need to force solid window now so we set its back its true mode)
            action_dequeue(a);
            w->action_running = False;
        }
    }
    effect_time = now + s.effect_delta;
}
