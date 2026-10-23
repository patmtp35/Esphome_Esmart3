#pragma once
#include <cstdint>
namespace esphome {
namespace setup_priority { constexpr float DATA = 600.0f; }
class Component {
 public:
  virtual void setup() {}
  virtual void loop() {}
  virtual void dump_config() {}
  virtual float get_setup_priority() const { return 0; }
  void status_set_warning(const char *m = nullptr) { (void) m; warning = true; }
  void status_clear_warning() { warning = false; }
  bool warning{false};
  virtual ~Component() = default;
};
class PollingComponent : public Component { public: virtual void update() = 0; };
}  // namespace esphome
