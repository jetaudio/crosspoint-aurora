#pragma once

#include <cstdint>

class Activity;  // forward declaration

// RAII helper to lock rendering mutex for the duration of a scope.
class RenderLock {
  bool isLocked = false;

 public:
  explicit RenderLock();
  explicit RenderLock(Activity&);  // unused for now, but keep for compatibility
  // Bounded wait for a task that must not block forever on the render task (the reader's
  // background prefetch: the activity's destructor runs with this lock held and waits for it).
  struct TryFor {
    uint32_t ms;
  };
  explicit RenderLock(TryFor timeout);
  bool locked() const { return isLocked; }
  RenderLock(const RenderLock&) = delete;
  RenderLock& operator=(const RenderLock&) = delete;
  ~RenderLock();
  void unlock();
  static bool peek();
};
