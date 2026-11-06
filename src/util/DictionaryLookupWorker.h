#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>

class DictionaryLookupController;

// A single dictionary worker avoids allocating a 4 KB task stack for every
// lookup; the stack is taken at the first lookup and kept. Controllers still own all lookup state and must wait for the
// worker before they are destroyed.
class DictionaryLookupWorker {
 public:
  static DictionaryLookupWorker& instance();

  // Starts a lookup for owner. Returns false when the task could not be
  // created or another controller still owns the worker.
  bool start(DictionaryLookupController& owner);

  // Waits until owner is no longer running on the worker. The owner must set
  // its cancellation flag before calling this method.
  void waitForOwner(const DictionaryLookupController& owner);

 private:
  static constexpr size_t kStackBytes = 4096;

  static void taskEntry(void* context);
  void run();

  StaticTask_t taskStorage_ = {};
  StackType_t* stack_ = nullptr;
  TaskHandle_t taskHandle_ = nullptr;
  std::atomic<DictionaryLookupController*> owner_{nullptr};
};
