#pragma once
#include "model.h"

bool storeBegin();
bool storeLoad(Snapshot &s);
bool storeSave(const Snapshot &s);