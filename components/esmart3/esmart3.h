#pragma once

#include <deque>
#include <vector>

#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/core/helpers.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/switch/switch.h"

#include "esmart3_protocol.h"

namespace esphome {
namespace esmart3 {

// Accesseurs générés (indépendants des macros SUB_* d'ESPHome, qui varient selon les versions).
#define ESMART3_SENSOR(name) \
 protected: \
  sensor::Sensor *name##_sensor_{nullptr}; \
\
 public: \
  void set_##name##_sensor(sensor::Sensor *s) { this->name##_sensor_ = s; }

#define ESMART3_TEXT_SENSOR(name) \
 protected: \
  text_sensor::TextSensor *name##_text_sensor_{nullptr}; \
\
 public: \
  void set_##name##_text_sensor(text_sensor::TextSensor *s) { this->name##_text_sensor_ = s; }

#define ESMART3_BINARY_SENSOR(name) \
 protected: \
  binary_sensor::BinarySensor *name##_binary_sensor_{nullptr}; \
\
 public: \
  void set_##name##_binary_sensor(binary_sensor::BinarySensor *s) { this->name##_binary_sensor_ = s; }

class ESmart3LoadSwitch;

class ESmart3Component : public PollingComponent, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_address(uint8_t address) { this->address_ = address; }
  void set_slow_every(uint8_t n) { this->slow_every_ = n == 0 ? 1 : n; }
  void set_u32_high_word_first(bool v) { this->u32_high_word_first_ = v; }
  // Broche DE/RE d'un module RS485 sans direction automatique : HIGH pendant l'émission, LOW sinon.
  void set_flow_control_pin(GPIOPin *pin) { this->flow_control_pin_ = pin; }
  void set_load_switch(ESmart3LoadSwitch *s) { this->load_switch_ = s; }

  // Écrit wLoadModuleSelect1 = 5117 (toujours ON) ou 5118 (toujours OFF).
  void set_load(bool on);

  // --- Mesures temps réel (item 0) ---
  ESMART3_SENSOR(pv_voltage)
  ESMART3_SENSOR(battery_voltage)
  ESMART3_SENSOR(charging_current)
  ESMART3_SENSOR(load_voltage)
  ESMART3_SENSOR(load_current)
  ESMART3_SENSOR(charging_power)
  ESMART3_SENSOR(load_power)
  ESMART3_SENSOR(battery_temperature)
  ESMART3_SENSOR(internal_temperature)
  ESMART3_SENSOR(battery_level)
  ESMART3_SENSOR(charge_mode)
  ESMART3_SENSOR(fault_code)
  // --- Énergies (item 2), en kWh ---
  ESMART3_SENSOR(energy_today)
  ESMART3_SENSOR(energy_month)
  ESMART3_SENSOR(energy_total)
  ESMART3_SENSOR(load_energy_today)
  ESMART3_SENSOR(load_energy_month)
  ESMART3_SENSOR(load_energy_total)
  // --- Textes et binaires ---
  ESMART3_TEXT_SENSOR(charge_mode)
  ESMART3_TEXT_SENSOR(faults)
  ESMART3_TEXT_SENSOR(load_mode)
  ESMART3_BINARY_SENSOR(load_on)
  ESMART3_BINARY_SENSOR(fault)

 protected:
  enum class Kind : uint8_t { STATE, ENERGY, LOAD, WRITE_LOAD };

  struct Request {
    Kind kind{Kind::STATE};
    uint8_t item{0};
    std::vector<uint8_t> frame;
  };

  void enqueue_(Kind kind);
  void send_next_(uint32_t now);
  void on_timeout_();
  void handle_frame_(const proto::Frame &f);
  void handle_data_(const proto::Frame &f);
  void handle_ack_(const proto::Frame &f);
  void finish_pending_(uint32_t now);
  void note_good_frame_();
  void publish_unavailable_();

  void publish_state_(const proto::State &s);
  void publish_energy_(const proto::Energy &e);
  void publish_load_(const proto::LoadInfo &l);

  bool has_energy_entities_() const;
  bool has_load_entities_() const;

  static void publish_text_(text_sensor::TextSensor *s, const std::string &value);

  GPIOPin *flow_control_pin_{nullptr};
  uint8_t address_{0};
  uint8_t slow_every_{6};
  uint8_t slow_counter_{0};
  bool u32_high_word_first_{true};

  proto::FrameReader reader_;
  std::deque<Request> queue_;
  Request pending_req_;
  bool pending_{false};
  uint32_t sent_ms_{0};
  uint32_t last_rx_ms_{0};
  uint32_t last_activity_ms_{0};

  uint8_t consecutive_timeouts_{0};
  bool unavailable_published_{false};
  uint8_t state_read_bytes_{proto::STATE_READ_BYTES};
  uint8_t refusals_[4]{0, 0, 0, 0};  // NACK/ERR consécutifs par Kind
  bool disabled_[4]{false, false, false, false};

  ESmart3LoadSwitch *load_switch_{nullptr};
  bool load_on_{false};
  bool load_known_{false};
};

// Interrupteur de la sortie charge. Il n'est jamais restauré au démarrage :
// aucune écriture n'est envoyée au régulateur tant que l'utilisateur n'agit pas.
class ESmart3LoadSwitch : public switch_::Switch, public Parented<ESmart3Component> {
 protected:
  void write_state(bool state) override;
};

}  // namespace esmart3
}  // namespace esphome
