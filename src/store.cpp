#include "store.h"

#include <LittleFS.h>

namespace {
constexpr const char *kPath = "/snapshot.bin";
constexpr const char *kTmp = "/snapshot.tmp";
} // namespace

bool storeBegin() { return LittleFS.begin(); }

bool storeLoad(Snapshot &s) {
  File f = LittleFS.open(kPath, "r");
  if (!f)
    return false;
  const bool ok =
      f.size() == sizeof(Snapshot) &&
      f.read(reinterpret_cast<uint8_t *>(&s), sizeof(s)) == sizeof(s);
  f.close();
  return ok && s.magic == kSnapshotMagic && s.version == kSnapshotVersion &&
         s.size == sizeof(Snapshot);
}

bool storeSave(const Snapshot &s) {
  File f = LittleFS.open(kTmp, "w");
  if (!f)
    return false;
  const bool ok =
      f.write(reinterpret_cast<const uint8_t *>(&s), sizeof(s)) == sizeof(s);
  f.close();
  if (!ok)
    return false;
  LittleFS.remove(kPath);
  return LittleFS.rename(kTmp, kPath);
}