#pragma once
#include "astar.h"
#include "types.h"

void initMap();
bool hasLineOfSight(int x0, int y0, int x1, int y1);
void updateVisibility();
