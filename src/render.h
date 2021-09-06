#pragma once

#include <X11/extensions/Xdamage.h>

void add_damage(XserverRegion damage);

void paint_all(XserverRegion region);
