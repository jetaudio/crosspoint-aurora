#include "BuildScratch.h"

#include <Logging.h>

#include <atomic>

// Host unit tests build this without FreeRTOS; there is only one thread to lend to.
#if __has_include(<freertos/FreeRTOS.h>)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#define BUILDSCRATCH_TASK_AWARE 1
#endif

namespace buildscratch {
namespace {
uint8_t* block = nullptr;
size_t blockLen = 0;
// atomic exchange so an opportunistic claim from another task can never
// double-hand-out the block (single core, but FreeRTOS preempts).
std::atomic<bool> claimed{false};
#if BUILDSCRATCH_TASK_AWARE
// The loan is lent for the lender's own build. Another task (the reader's background
// chapter prefetch) inflating at the same time must not take it: the lender reclaims and
// redraws the framebuffer when its loan ends, which would corrupt that inflate's window.
TaskHandle_t lender = nullptr;
#endif
}  // namespace

void lend(uint8_t* buf, const size_t len) {
  if (block) {
    LOG_ERR("SCR", "Build scratch lent twice; ignoring second lend");
    return;
  }
  block = buf;
  blockLen = len;
#if BUILDSCRATCH_TASK_AWARE
  lender = xTaskGetCurrentTaskHandle();
#endif
  claimed.store(false);
}

void reclaim() {
  if (claimed.load()) {
    // A consumer still holds the block. The storage stays valid (it is the
    // framebuffer allocation, never freed) but its contents are about to be
    // clobbered; the consumer's output will be garbage. Loud log so a
    // lifetime bug is visible instead of a silent corrupt decode.
    LOG_ERR("SCR", "Build scratch reclaimed while still claimed");
  }
  block = nullptr;
  blockLen = 0;
  claimed.store(false);
}

uint8_t* claim(const size_t minLen, size_t* lenOut) {
  if (!block || blockLen < minLen) return nullptr;
#if BUILDSCRATCH_TASK_AWARE
  if (xTaskGetCurrentTaskHandle() != lender) return nullptr;
#endif
  bool expected = false;
  if (!claimed.compare_exchange_strong(expected, true)) return nullptr;
  if (lenOut) *lenOut = blockLen;
  return block;
}

void release(const uint8_t* p) {
  if (p && p == block) claimed.store(false);
}

}  // namespace buildscratch
