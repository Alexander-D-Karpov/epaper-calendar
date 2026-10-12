#pragma once
#include <stdint.h>

#include "model.h"

enum class FetchResult : uint8_t { Updated, NotModified, Failed };

// One GET /api/v1/display; with conditional set it sends the stored ETag.
FetchResult apiFetchDisplay(Snapshot &s, bool conditional);
