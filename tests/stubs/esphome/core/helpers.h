#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace esphome {
template<typename T> class Parented { public: void set_parent(T *p) { parent_ = p; } protected: T *parent_{nullptr}; };
inline std::string format_hex_pretty(const std::vector<uint8_t> &v) { return std::string(v.size(), 'x'); }
}  // namespace esphome
