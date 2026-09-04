#pragma once

namespace esphome {

class ESPPreferenceObject {
 public:
  template<typename T> bool save(const T *) { return true; }
  template<typename T> bool load(T *) { return false; }
};

}
