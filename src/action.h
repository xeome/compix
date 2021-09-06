#pragma once

#include "effect.h"
#include "window.h"

void action_cleanup(win *w);

void action_set(win *w, effect *e, Bool reverse, void (*callback)(win *w, Bool gone), Bool gone, Bool exec_callback);

int action_timeout(void);

void action_run(void);
