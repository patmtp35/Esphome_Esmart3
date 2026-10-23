#pragma once
#include <string>
namespace sim { extern std::string events; }
namespace esphome {
// Broche simulée : chaque écriture est consignée dans sim::events ('1' / '0'), dans l'ordre avec l'UART.
class GPIOPin {
 public:
  void setup() { sim::events += 'S'; }
  void digital_write(bool v) { sim::events += v ? '1' : '0'; }
};
}  // namespace esphome
