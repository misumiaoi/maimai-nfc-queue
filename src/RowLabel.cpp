#include "RowLabel.h"

#include <string.h>

namespace {

void copyText(char* out, const char* name, const char* tail) {
  size_t n = strlen(name);
  memcpy(out, name, n);
  if (tail == nullptr) {
    out[n] = '\0';
    return;
  }
  out[n++] = ' ';
  size_t t = strlen(tail);
  memcpy(out + n, tail, t + 1);
}

}  // namespace

void formatRowLabel(const QueueManager& queue, const NameRegistry& names, size_t i, char* out) {
  const char* name = names.find(queue.at(i).hash);
  if (name == nullptr) {
    queue.label(i, out);
    return;
  }

  bool duplicated = false;
  for (size_t j = 0; j < queue.size(); ++j) {
    if (j == i) continue;
    const char* other = names.find(queue.at(j).hash);
    if (other != nullptr && strcmp(other, name) == 0) {
      duplicated = true;
      break;
    }
  }
  if (!duplicated) {
    copyText(out, name, nullptr);
    return;
  }

  char tail[kTailDigits + 1];
  queue.label(i, tail);
  copyText(out, name, tail);
}
