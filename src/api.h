#pragma once
#include <stdint.h>

#include "model.h"

bool apiLatestSeq(int64_t& latest);
bool apiFetchAll(Snapshot& s);