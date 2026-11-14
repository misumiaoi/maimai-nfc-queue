#include "Registrar.h"

#include <string.h>

Registrar::Registrar(uint32_t windowMs) : windowMs_(windowMs) { pending_[0] = '\0'; }

bool Registrar::arm(const char* name, uint32_t nowMs) {
  char norm[kMaxNameLen + 1];
  if (!normalizeName(name, norm, sizeof(norm))) return false;
  memcpy(pending_, norm, strlen(norm) + 1);
  sinceMs_ = nowMs;
  armed_ = true;
  return true;
}

void Registrar::cancel() { armed_ = false; }

bool Registrar::timedOut(uint32_t nowMs) const {
  return armed_ && (nowMs - sinceMs_) > windowMs_;
}

bool Registrar::expire(uint32_t nowMs) {
  if (!timedOut(nowMs)) return false;
  armed_ = false;
  return true;
}

Registrar::BindTap Registrar::consume(uint64_t hash, NameRegistry& names, uint32_t nowMs) {
  if (!armed_) return BindTap::None;
  if (timedOut(nowMs)) {
    armed_ = false;
    return BindTap::Expired;
  }
  if (!names.bind(hash, pending_)) {
    armed_ = false;
    return BindTap::Full;
  }
  armed_ = false;
  return BindTap::Bound;
}
