#pragma once
namespace esphome { namespace binary_sensor { class BinarySensor { public: bool state{false}; bool has{false}; void publish_state(bool v) { state = v; has = true; } }; } }
