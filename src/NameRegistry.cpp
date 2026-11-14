#include "NameRegistry.h"

#include <string.h>

namespace {

constexpr uint8_t kMagic0 = 'N';
constexpr uint8_t kMagic1 = '1';
constexpr size_t kRecordBytes = 8 + kMaxNameLen;

static_assert(kMaxNames <= 255, "serialized count is one byte");
constexpr size_t kRawCap = 64;

bool isAlphaNum(unsigned char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

bool equalsToken(const char* line, const char* upper) {
  size_t i = 0;
  for (; upper[i] != '\0'; ++i) {
    unsigned char c = static_cast<unsigned char>(line[i]);
    if (c >= 'a' && c <= 'z') c = static_cast<unsigned char>(c - 'a' + 'A');
    if (c != static_cast<unsigned char>(upper[i])) return false;
  }
  while (line[i] == ' ') ++i;
  return line[i] == '\0';
}

bool matchNameCommand(const char* line, const char** rest) {
  static const char kName[] = "NAME";
  size_t i = 0;
  for (; kName[i] != '\0'; ++i) {
    unsigned char c = static_cast<unsigned char>(line[i]);
    if (c >= 'a' && c <= 'z') c = static_cast<unsigned char>(c - 'a' + 'A');
    if (c != static_cast<unsigned char>(kName[i])) return false;
  }
  if (line[i] != ' ') return false;
  while (line[i] == ' ') ++i;
  *rest = line + i;
  return (*rest)[0] != '\0';
}

void writeHash(uint8_t*& p, uint64_t hash) {
  for (int b = 0; b < 8; ++b) {
    *p++ = static_cast<uint8_t>(hash >> (8 * b));
  }
}

uint64_t readHash(const uint8_t*& p) {
  uint64_t hash = 0;
  for (int b = 0; b < 8; ++b) {
    hash |= static_cast<uint64_t>(*p++) << (8 * b);
  }
  return hash;
}

}  // namespace

bool normalizeName(const char* in, char* out, size_t outCap) {
  if (in == nullptr || out == nullptr || outCap < kMaxNameLen + 1) return false;
  if (strlen(in) > kRawCap) return false;

  char buf[kMaxNameLen];
  size_t n = 0;
  bool gap = false;
  bool started = false;
  bool any = false;

  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(in); *p != 0; ++p) {
    unsigned char c = *p;
    if (c == ' ') {
      if (started) gap = true;
      continue;
    }
    if (c != '-' && !isAlphaNum(c)) return false;
    if (!started && c == '-') return false;
    if (gap) {
      if (n >= kMaxNameLen) return false;
      buf[n++] = ' ';
      gap = false;
    }
    if (n >= kMaxNameLen) return false;
    buf[n++] = static_cast<char>(c);
    started = true;
    if (c != '-') any = true;
  }

  if (!any || n == 0 || buf[n - 1] == '-') return false;
  memcpy(out, buf, n);
  out[n] = '\0';
  return true;
}

LineCommand parseNameLine(const char* line) {
  LineCommand cmd{};
  cmd.kind = LineKind::Invalid;
  if (line == nullptr) return cmd;

  while (*line == ' ') ++line;
  if (*line == '\0') {
    cmd.kind = LineKind::Empty;
    return cmd;
  }
  if (equalsToken(line, "CANCEL")) {
    cmd.kind = LineKind::Cancel;
    return cmd;
  }

  const char* rest = nullptr;
  if (!matchNameCommand(line, &rest)) return cmd;
  if (!normalizeName(rest, cmd.name, sizeof(cmd.name))) return cmd;
  cmd.kind = LineKind::Bind;
  return cmd;
}

NameRegistry::NameRegistry() {
  memset(hashes_, 0, sizeof(hashes_));
  memset(names_, 0, sizeof(names_));
}

int NameRegistry::indexOf(uint64_t hash) const {
  for (size_t i = 0; i < count_; ++i) {
    if (hashes_[i] == hash) return static_cast<int>(i);
  }
  return -1;
}

bool NameRegistry::bind(uint64_t hash, const char* name) {
  char norm[kMaxNameLen + 1];
  if (!normalizeName(name, norm, sizeof(norm))) return false;

  int idx = indexOf(hash);
  if (idx < 0) {
    if (count_ >= kMaxNames) return false;
    idx = static_cast<int>(count_);
    ++count_;
  }
  hashes_[idx] = hash;
  memset(names_[idx], 0, kMaxNameLen + 1);
  memcpy(names_[idx], norm, strlen(norm));
  return true;
}

const char* NameRegistry::find(uint64_t hash) const {
  int idx = indexOf(hash);
  return idx < 0 ? nullptr : names_[idx];
}

size_t NameRegistry::serialize(uint8_t* buf, size_t cap) const {
  size_t need = 3 + count_ * kRecordBytes;
  if (cap < need) return 0;

  buf[0] = kMagic0;
  buf[1] = kMagic1;
  buf[2] = static_cast<uint8_t>(count_);
  uint8_t* p = buf + 3;
  for (size_t i = 0; i < count_; ++i) {
    writeHash(p, hashes_[i]);
    memset(p, 0, kMaxNameLen);
    memcpy(p, names_[i], strlen(names_[i]));
    p += kMaxNameLen;
  }
  return need;
}

bool NameRegistry::deserialize(const uint8_t* buf, size_t len) {
  if (len < 3 || buf[0] != kMagic0 || buf[1] != kMagic1) return false;
  size_t n = buf[2];
  if (n > kMaxNames || len != 3 + n * kRecordBytes) return false;

  uint64_t hashes[kMaxNames];
  char names[kMaxNames][kMaxNameLen + 1];
  memset(hashes, 0, sizeof(hashes));
  memset(names, 0, sizeof(names));

  const uint8_t* p = buf + 3;
  for (size_t i = 0; i < n; ++i) {
    hashes[i] = readHash(p);
    for (size_t k = 0; k < kMaxNameLen; ++k) {
      if (p[k] == 0) {
        for (size_t z = k; z < kMaxNameLen; ++z) {
          if (p[z] != 0) return false;
        }
        break;
      }
    }
    memcpy(names[i], p, kMaxNameLen);
    names[i][kMaxNameLen] = '\0';
    p += kMaxNameLen;

    char check[kMaxNameLen + 1];
    if (!normalizeName(names[i], check, sizeof(check)) || strcmp(names[i], check) != 0) {
      return false;
    }
  }

  memcpy(hashes_, hashes, sizeof(hashes_));
  memcpy(names_, names, sizeof(names_));
  count_ = n;
  return true;
}
