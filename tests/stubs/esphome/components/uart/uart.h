#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>
namespace sim { extern std::string events; }
namespace esphome { namespace uart {
// UART simulée : le test place des octets dans `rx` et lit ce que le composant a émis dans `tx`.
class UARTDevice {
 public:
  std::deque<uint8_t> rx;
  std::vector<std::vector<uint8_t>> tx;
  int available() { return static_cast<int>(rx.size()); }
  bool read_byte(uint8_t *b) { if (rx.empty()) return false; *b = rx.front(); rx.pop_front(); return true; }
  void write_array(const uint8_t *d, size_t n) { sim::events += 'W'; tx.emplace_back(d, d + n); }
  void flush() { sim::events += 'F'; }
  void check_uart_settings(uint32_t) {}
};
} }
