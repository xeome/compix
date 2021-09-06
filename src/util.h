#pragma once

#include <X11/Xlib.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#define OPAQUE 0xFFFFFFFF

#define COPY_AREA(DEST, SRC)       \
    ((DEST)->x = (SRC)->x,         \
     (DEST)->y = (SRC)->y,         \
     (DEST)->width = (SRC)->width, \
     (DEST)->height = (SRC)->height)

#define eprintf(...) (fprintf(stderr, __VA_ARGS__), exit(EXIT_FAILURE))

#ifdef DEBUG
#define print_event(ev) printf("[XEvent] %17.17s - serial: 0x%08x, window: 0x%08lx\n", ev_name(&ev), ev_serial(&ev), ev_window(&ev))

int ev_serial(XEvent *ev);
const char *ev_name(XEvent *ev);
Window ev_window(XEvent *ev);
#endif

void discard_ignore(unsigned long int sequence);
void set_ignore(unsigned long int sequence);
int should_ignore(unsigned long int sequence);

int handle_error(Display *display, XErrorEvent *ev);
