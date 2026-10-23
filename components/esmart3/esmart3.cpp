#include "esmart3.h"

#include <cmath>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace esmart3 {

static const char *const TAG = "esmart3";

static const uint32_t RESPONSE_TIMEOUT_MS = 1000;      // délai max d'une réponse
static const uint32_t PARTIAL_FRAME_TIMEOUT_MS = 300;  // trame coupée : on jette le tampon
static const uint32_t MIN_GAP_MS = 60;                 // silence mini entre deux requêtes
static const size_t MAX_QUEUE = 8;
static const uint8_t TIMEOUTS_BEFORE_WARNING = 3;
static const uint8_t TIMEOUTS_BEFORE_UNAVAILABLE = 6;
static const uint8_t REFUSALS_BEFORE_DISABLE = 3;
static const uint8_t FALLBACK_STATE_READ_BYTES = 0x18;  // lecture de ssieb : jusqu'au SoC, sans wFault

void ESmart3Component::setup() {
  this->reader_.reset();
  if (this->flow_control_pin_ != nullptr) {
    this->flow_control_pin_->setup();
    this->flow_control_pin_->digital_write(false);  // repos = réception
  }
}

void ESmart3Component::dump_config() {
  ESP_LOGCONFIG(TAG, "eSmart3 MPPT:");
  LOG_PIN("  Flow control pin: ", this->flow_control_pin_);
  ESP_LOGCONFIG(TAG, "  Device address: %u", this->address_);
  ESP_LOGCONFIG(TAG, "  Energy/load data every %u update(s)", this->slow_every_);
  ESP_LOGCONFIG(TAG, "  32-bit values: %s word first", this->u32_high_word_first_ ? "high" : "low");
  ESP_LOGCONFIG(TAG, "  Energy sensors: %s", this->has_energy_entities_() ? "yes" : "no");
  ESP_LOGCONFIG(TAG, "  Load state entities: %s", this->has_load_entities_() ? "yes" : "no");
  ESP_LOGCONFIG(TAG, "  Load switch: %s", this->load_switch_ != nullptr ? "yes (writes wLoadModuleSelect1)" : "no");
  LOG_UPDATE_INTERVAL(this);
  this->check_uart_settings(9600);
}

bool ESmart3Component::has_energy_entities_() const {
  return this->energy_today_sensor_ != nullptr || this->energy_month_sensor_ != nullptr ||
         this->energy_total_sensor_ != nullptr || this->load_energy_today_sensor_ != nullptr ||
         this->load_energy_month_sensor_ != nullptr || this->load_energy_total_sensor_ != nullptr;
}

bool ESmart3Component::has_load_entities_() const {
  return this->load_on_binary_sensor_ != nullptr || this->load_mode_text_sensor_ != nullptr ||
         this->load_switch_ != nullptr;
}

// ---------------------------------------------------------------------------
// Planification
// ---------------------------------------------------------------------------
void ESmart3Component::update() {
  this->enqueue_(Kind::STATE);
  if (this->slow_counter_ == 0) {
    if (this->has_energy_entities_())
      this->enqueue_(Kind::ENERGY);
    if (this->has_load_entities_())
      this->enqueue_(Kind::LOAD);
  }
  this->slow_counter_ = static_cast<uint8_t>((this->slow_counter_ + 1) % this->slow_every_);
}

void ESmart3Component::enqueue_(Kind kind) {
  if (this->disabled_[static_cast<uint8_t>(kind)])
    return;
  for (const Request &r : this->queue_) {
    if (r.kind == kind)
      return;  // déjà en attente : on n'empile pas si le bus est lent
  }
  if (this->pending_ && this->pending_req_.kind == kind)
    return;
  if (this->queue_.size() >= MAX_QUEUE)
    return;

  Request r;
  r.kind = kind;
  switch (kind) {
    case Kind::STATE:
      r.item = proto::ITEM_STATE;
      r.frame = proto::build_get(proto::ITEM_STATE, proto::STATE_OFFSET, this->state_read_bytes_, this->address_);
      break;
    case Kind::ENERGY:
      r.item = proto::ITEM_LOG;
      r.frame = proto::build_get(proto::ITEM_LOG, proto::LOG_OFFSET, proto::LOG_READ_BYTES, this->address_);
      break;
    case Kind::LOAD:
      r.item = proto::ITEM_LOAD;
      r.frame = proto::build_get(proto::ITEM_LOAD, proto::LOAD_OFFSET, proto::LOAD_READ_BYTES, this->address_);
      break;
    case Kind::WRITE_LOAD:
      return;  // se construit dans set_load()
  }
  this->queue_.push_back(std::move(r));
}

void ESmart3Component::set_load(bool on) {
  Request r;
  r.kind = Kind::WRITE_LOAD;
  r.item = proto::ITEM_LOAD;
  r.frame = proto::build_set(proto::ITEM_LOAD, proto::LOAD_OFFSET,
                             {on ? proto::LOAD_MODULE_ALWAYS_ON : proto::LOAD_MODULE_ALWAYS_OFF}, this->address_);
  ESP_LOGW(TAG, "Writing load mode %u (%s) to the controller", on ? proto::LOAD_MODULE_ALWAYS_ON : proto::LOAD_MODULE_ALWAYS_OFF,
           on ? "always on" : "always off");
  this->queue_.push_front(std::move(r));  // avant les lectures
}

void ESmart3Component::send_next_(uint32_t now) {
  Request r = std::move(this->queue_.front());
  this->queue_.pop_front();
  // Module sans direction automatique : on passe en émission, on attend la fin de l'envoi (flush bloque
  // jusqu'à ce que le dernier octet soit sorti du UART), puis on repasse tout de suite en réception.
  if (this->flow_control_pin_ != nullptr)
    this->flow_control_pin_->digital_write(true);
  this->write_array(r.frame.data(), r.frame.size());
  this->flush();
  if (this->flow_control_pin_ != nullptr)
    this->flow_control_pin_->digital_write(false);
  ESP_LOGV(TAG, "TX item %u: %s", r.item, format_hex_pretty(r.frame).c_str());
  this->pending_req_ = std::move(r);
  this->pending_ = true;
  this->sent_ms_ = now;
}

void ESmart3Component::finish_pending_(uint32_t now) {
  this->pending_ = false;
  this->last_activity_ms_ = now;
}

// ---------------------------------------------------------------------------
// Boucle : réception, resynchronisation, timeouts, envoi
// ---------------------------------------------------------------------------
void ESmart3Component::loop() {
  const uint32_t now = millis();

  while (this->available()) {
    uint8_t b;
    if (!this->read_byte(&b))
      break;
    this->reader_.push(b);
    this->last_rx_ms_ = now;
  }

  proto::Frame f;
  while (this->reader_.pop(f))
    this->handle_frame_(f);

  if (this->reader_.buffered() > 0 && now - this->last_rx_ms_ > PARTIAL_FRAME_TIMEOUT_MS) {
    ESP_LOGV(TAG, "Dropping %u byte(s) of incomplete frame", (unsigned) this->reader_.buffered());
    this->reader_.reset();
  }

  if (this->pending_ && now - this->sent_ms_ > RESPONSE_TIMEOUT_MS)
    this->on_timeout_();

  if (!this->pending_ && !this->queue_.empty() && now - this->last_activity_ms_ >= MIN_GAP_MS &&
      now - this->last_rx_ms_ >= MIN_GAP_MS)
    this->send_next_(now);
}

void ESmart3Component::on_timeout_() {
  ESP_LOGW(TAG, "No response to request for item %u", this->pending_req_.item);
  const bool was_write = this->pending_req_.kind == Kind::WRITE_LOAD;
  this->finish_pending_(millis());
  if (this->consecutive_timeouts_ < 255)
    this->consecutive_timeouts_++;
  if (this->consecutive_timeouts_ == TIMEOUTS_BEFORE_WARNING) {
    this->status_set_warning("No response from the eSmart3 (check A/B wiring, GND and RS485 direction control)");
  }
  if (this->consecutive_timeouts_ >= TIMEOUTS_BEFORE_UNAVAILABLE && !this->unavailable_published_)
    this->publish_unavailable_();
  if (was_write && this->load_switch_ != nullptr && this->load_known_)
    this->load_switch_->publish_state(this->load_on_);  // la commande n'a pas été confirmée : on rend l'état réel
}

void ESmart3Component::publish_unavailable_() {
  this->unavailable_published_ = true;
  ESP_LOGW(TAG, "eSmart3 unreachable: publishing NaN on measurement sensors");
  sensor::Sensor *all[] = {this->pv_voltage_sensor_,       this->battery_voltage_sensor_, this->charging_current_sensor_,
                           this->load_voltage_sensor_,     this->load_current_sensor_,    this->charging_power_sensor_,
                           this->load_power_sensor_,       this->battery_temperature_sensor_,
                           this->internal_temperature_sensor_, this->battery_level_sensor_, this->charge_mode_sensor_,
                           this->fault_code_sensor_};
  for (sensor::Sensor *s : all) {
    if (s != nullptr)
      s->publish_state(NAN);
  }
}

void ESmart3Component::note_good_frame_() {
  this->consecutive_timeouts_ = 0;
  this->unavailable_published_ = false;
  this->status_clear_warning();
}

// ---------------------------------------------------------------------------
// Réception
// ---------------------------------------------------------------------------
void ESmart3Component::handle_frame_(const proto::Frame &f) {
  ESP_LOGV(TAG, "RX cmd 0x%02X item %u, %u data byte(s)", f.cmd, f.item, (unsigned) f.data.size());
  switch (f.cmd) {
    case proto::CMD_SET_NO_RESP:  // réponse à un GET (le régulateur la renvoie sous cet identifiant)
      this->handle_data_(f);
      break;
    case proto::CMD_ACK:
    case proto::CMD_NACK:
    case proto::CMD_ERR:
      this->handle_ack_(f);
      break;
    default:
      // GET/SET/EXEC : écho de notre propre requête (certains modules RS485 rebouclent TX sur RX)
      // ou trame d'un autre maître sur le bus. Rien à faire.
      break;
  }
}

void ESmart3Component::handle_data_(const proto::Frame &f) {
  proto::WordBlock block;
  if (!proto::parse_block(f, block)) {
    ESP_LOGW(TAG, "Malformed data frame for item %u", f.item);
    return;
  }
  const uint32_t now = millis();
  bool decoded = false;

  // Les trames de données sont décodées même sans requête en cours : si un autre maître
  // (le Cloud Box M2) interroge le bus, on profite de ses réponses.
  switch (f.item) {
    case proto::ITEM_STATE: {
      proto::State s;
      decoded = proto::decode_state(block, s);
      if (decoded)
        this->publish_state_(s);
      break;
    }
    case proto::ITEM_LOG: {
      proto::Energy e;
      decoded = proto::decode_energy(block, this->u32_high_word_first_, e);
      if (decoded)
        this->publish_energy_(e);
      break;
    }
    case proto::ITEM_LOAD: {
      proto::LoadInfo l;
      decoded = proto::decode_load(block, l);
      if (decoded)
        this->publish_load_(l);
      break;
    }
    default:
      return;
  }

  if (!decoded) {
    ESP_LOGW(TAG, "Item %u answer too short (%u words from offset %u)", f.item, (unsigned) block.words.size(),
             block.offset);
    return;
  }
  this->note_good_frame_();
  if (this->pending_ && this->pending_req_.kind != Kind::WRITE_LOAD && this->pending_req_.item == f.item) {
    this->refusals_[static_cast<uint8_t>(this->pending_req_.kind)] = 0;
    this->finish_pending_(now);
  }
}

void ESmart3Component::handle_ack_(const proto::Frame &f) {
  if (!this->pending_ || this->pending_req_.item != f.item) {
    ESP_LOGV(TAG, "Unsolicited ack/nack (cmd 0x%02X, item %u)", f.cmd, f.item);
    return;
  }
  const uint32_t now = millis();
  const Kind kind = this->pending_req_.kind;
  const bool ok = f.cmd == proto::CMD_ACK;
  this->finish_pending_(now);
  this->note_good_frame_();  // le régulateur répond, donc le bus fonctionne

  if (kind == Kind::WRITE_LOAD) {
    if (ok) {
      ESP_LOGI(TAG, "Load command accepted by the controller");
      this->enqueue_(Kind::LOAD);  // relit l'état réel
    } else {
      ESP_LOGW(TAG, "Load command refused by the controller (cmd 0x%02X)", f.cmd);
      if (this->load_switch_ != nullptr && this->load_known_)
        this->load_switch_->publish_state(this->load_on_);
    }
    return;
  }

  // Une lecture refusée : on réessaie quelques fois puis on abandonne cette lecture
  // (pour la lecture temps réel : repli sur 24 octets, sans wFault).
  const uint8_t idx = static_cast<uint8_t>(kind);
  if (this->refusals_[idx] < 255)
    this->refusals_[idx]++;
  ESP_LOGW(TAG, "Read of item %u refused (cmd 0x%02X), attempt %u", f.item, f.cmd, this->refusals_[idx]);
  if (this->refusals_[idx] >= REFUSALS_BEFORE_DISABLE) {
    if (kind == Kind::STATE && this->state_read_bytes_ != FALLBACK_STATE_READ_BYTES) {
      ESP_LOGW(TAG, "Falling back to a shorter state read (%u bytes): fault code unavailable", FALLBACK_STATE_READ_BYTES);
      this->state_read_bytes_ = FALLBACK_STATE_READ_BYTES;
      this->refusals_[idx] = 0;
    } else if (kind != Kind::STATE) {
      ESP_LOGW(TAG, "Giving up on item %u: this firmware does not seem to support it", f.item);
      this->disabled_[idx] = true;
    }
  }
}

// ---------------------------------------------------------------------------
// Publication
// ---------------------------------------------------------------------------
void ESmart3Component::publish_text_(text_sensor::TextSensor *s, const std::string &value) {
  if (s != nullptr && (!s->has_state() || s->state != value))
    s->publish_state(value);
}

void ESmart3Component::publish_state_(const proto::State &s) {
  if (this->pv_voltage_sensor_ != nullptr)
    this->pv_voltage_sensor_->publish_state(s.pv_voltage);
  if (this->battery_voltage_sensor_ != nullptr)
    this->battery_voltage_sensor_->publish_state(s.battery_voltage);
  if (this->charging_current_sensor_ != nullptr)
    this->charging_current_sensor_->publish_state(s.charging_current);
  if (this->load_voltage_sensor_ != nullptr)
    this->load_voltage_sensor_->publish_state(s.load_voltage);
  if (this->load_current_sensor_ != nullptr)
    this->load_current_sensor_->publish_state(s.load_current);
  if (this->charging_power_sensor_ != nullptr)
    this->charging_power_sensor_->publish_state(s.charging_power);
  if (this->load_power_sensor_ != nullptr)
    this->load_power_sensor_->publish_state(s.load_power);
  if (this->battery_temperature_sensor_ != nullptr)
    this->battery_temperature_sensor_->publish_state(s.battery_temp);  // int16 : les négatifs sont corrects
  if (this->internal_temperature_sensor_ != nullptr)
    this->internal_temperature_sensor_->publish_state(s.internal_temp);
  if (this->battery_level_sensor_ != nullptr)
    this->battery_level_sensor_->publish_state(s.battery_level);
  if (this->charge_mode_sensor_ != nullptr)
    this->charge_mode_sensor_->publish_state(s.charge_mode);
  if (s.has_fault) {
    if (this->fault_code_sensor_ != nullptr)
      this->fault_code_sensor_->publish_state(s.fault);
    if (this->fault_binary_sensor_ != nullptr)
      this->fault_binary_sensor_->publish_state(s.fault != 0);
    publish_text_(this->faults_text_sensor_, proto::fault_text(s.fault));
  }
  publish_text_(this->charge_mode_text_sensor_, proto::charge_mode_name(s.charge_mode));
}

void ESmart3Component::publish_energy_(const proto::Energy &e) {
  // Wh -> kWh
  if (this->energy_today_sensor_ != nullptr)
    this->energy_today_sensor_->publish_state(e.production_today_wh / 1000.0f);
  if (this->energy_month_sensor_ != nullptr)
    this->energy_month_sensor_->publish_state(e.production_month_wh / 1000.0f);
  if (this->energy_total_sensor_ != nullptr)
    this->energy_total_sensor_->publish_state(e.production_total_wh / 1000.0f);
  if (this->load_energy_today_sensor_ != nullptr)
    this->load_energy_today_sensor_->publish_state(e.load_today_wh / 1000.0f);
  if (this->load_energy_month_sensor_ != nullptr)
    this->load_energy_month_sensor_->publish_state(e.load_month_wh / 1000.0f);
  if (this->load_energy_total_sensor_ != nullptr)
    this->load_energy_total_sensor_->publish_state(e.load_total_wh / 1000.0f);
}

void ESmart3Component::publish_load_(const proto::LoadInfo &l) {
  this->load_on_ = l.load_on;
  this->load_known_ = true;
  if (this->load_on_binary_sensor_ != nullptr)
    this->load_on_binary_sensor_->publish_state(l.load_on);
  publish_text_(this->load_mode_text_sensor_, proto::load_mode_text(l.module_select1));
  if (this->load_switch_ != nullptr)
    this->load_switch_->publish_state(l.load_on);
}

// ---------------------------------------------------------------------------
// Interrupteur
// ---------------------------------------------------------------------------
void ESmart3LoadSwitch::write_state(bool state) {
  // Pas de publish_state ici : l'état affiché est celui relu sur le régulateur après l'ACK.
  this->parent_->set_load(state);
}

}  // namespace esmart3
}  // namespace esphome
