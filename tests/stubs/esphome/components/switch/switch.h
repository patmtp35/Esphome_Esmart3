#pragma once
namespace esphome { namespace switch_ { class Switch { public: bool state{false}; int published{0}; void publish_state(bool v) { state = v; published++; } void turn_on() { write_state(true); } void turn_off() { write_state(false); } protected: virtual void write_state(bool s) = 0; }; } }
