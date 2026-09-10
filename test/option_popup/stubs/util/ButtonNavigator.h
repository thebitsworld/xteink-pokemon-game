#pragma once

#include "MappedInputManager.h"

class ButtonNavigator {
 public:
  template <typename Callback>
  void onPreviousRelease(Callback&&) {}
  template <typename Callback>
  void onNextRelease(Callback&&) {}
  template <typename Callback>
  void onPreviousContinuous(Callback&&) {}
  template <typename Callback>
  void onNextContinuous(Callback&&) {}
  static int previousPageIndex(int index, int, int) { return index; }
  static int nextPageIndex(int index, int, int) { return index; }
  static int previousIndex(int index, int) { return index; }
  static int nextIndex(int index, int) { return index; }
};
