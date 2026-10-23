#pragma once

// Protocole série du régulateur MPPT eSmart3 (Supower / Yueqing) — couche pure C++.
//
// Ce fichier n'inclut RIEN d'ESPHome : il se compile et se teste avec g++ seul
// (voir tests/test_protocol.cpp). Source : "MPPT Solar Controller communication
// protocol, February 2018" (github.com/skagmo/esmart_mppt/files/3401128/esmart3-serial-comm.pdf) + lib skagmo/E-t0m.
//
// Trame :  AA | type | adr | cmd | item | longueur | données[longueur] | checksum
//   - checksum = (0 - somme de tous les octets précédents) & 0xFF
//   - mots de 16 bits en little-endian
//   - GET   : données = offset(mot, LE) + nb d'octets à lire (1 octet)
//   - réponse GET : cmd = 3 (SET_NO_RESP), données = offset(mot, LE) + mots lus
//   - SET   : données = offset(mot, LE) + mots à écrire ; réponse ACK (0) ou NACK (4)

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace esphome {
namespace esmart3 {
namespace proto {

constexpr uint8_t START_BYTE = 0xAA;
constexpr uint8_t DEVICE_TYPE_MPPT = 0x01;
constexpr size_t HEADER_LEN = 6;          // AA type adr cmd item len
constexpr size_t MAX_DATA_LEN = 120;      // limite annoncée par le PDF
constexpr size_t MAX_FRAME_LEN = HEADER_LEN + MAX_DATA_LEN + 1;

enum Cmd : uint8_t {
  CMD_ACK = 0x00,
  CMD_GET = 0x01,
  CMD_SET = 0x02,
  CMD_SET_NO_RESP = 0x03,  // c'est aussi le type des réponses à un GET
  CMD_NACK = 0x04,
  CMD_EXEC = 0x05,
  CMD_ERR = 0x7F,
};

enum Item : uint8_t {
  ITEM_STATE = 0,       // db_ChgSts : mesures temps réel (lecture seule)
  ITEM_BATTERY = 1,     // db_BatParam
  ITEM_LOG = 2,         // db_Log : énergies jour/mois/total
  ITEM_SAMPLING = 3,    // db_parameters : calibration interne — NE PAS TOUCHER
  ITEM_LOAD = 4,        // db_LoadParam : sortie charge
  ITEM_REMOTE = 6,      // db_remoteControl
  ITEM_PROTECTION = 7,  // db_ProParam
  ITEM_INFO = 8,        // db_Information
};

// Valeurs de wLoadModuleSelect1 qui forcent la sortie charge (PDF §8, usage E-t0m).
constexpr uint16_t LOAD_MODULE_ALWAYS_ON = 5117;
constexpr uint16_t LOAD_MODULE_ALWAYS_OFF = 5118;

// Offsets (en mots) utilisés par ce composant.
constexpr uint16_t STATE_OFFSET = 0x00;
constexpr uint8_t STATE_READ_BYTES = 0x1E;  // mots 0x00..0x0E (inclut wFault)
constexpr uint16_t LOG_OFFSET = 0x06;
constexpr uint8_t LOG_READ_BYTES = 0x20;  // mots 0x06..0x15 (énergies)
constexpr uint16_t LOAD_OFFSET = 0x01;
constexpr uint8_t LOAD_READ_BYTES = 0x1E;  // mots 0x01..0x0F (inclut wLoadSts)

struct Frame {
  uint8_t dev_type{DEVICE_TYPE_MPPT};
  uint8_t addr{0};
  uint8_t cmd{0};
  uint8_t item{0};
  std::vector<uint8_t> data;
};

inline uint8_t checksum(const uint8_t *buf, size_t len) {
  uint8_t sum = 0;
  for (size_t i = 0; i < len; i++)
    sum = static_cast<uint8_t>(sum + buf[i]);
  return static_cast<uint8_t>((0 - sum) & 0xFF);
}

inline std::vector<uint8_t> encode(const Frame &f) {
  std::vector<uint8_t> out;
  out.reserve(HEADER_LEN + f.data.size() + 1);
  out.push_back(START_BYTE);
  out.push_back(f.dev_type);
  out.push_back(f.addr);
  out.push_back(f.cmd);
  out.push_back(f.item);
  out.push_back(static_cast<uint8_t>(f.data.size()));
  out.insert(out.end(), f.data.begin(), f.data.end());
  out.push_back(checksum(out.data(), out.size()));
  return out;
}

// Requête de lecture : `byte_len` octets à partir du mot `word_offset`.
inline std::vector<uint8_t> build_get(uint8_t item, uint16_t word_offset, uint8_t byte_len, uint8_t addr = 0) {
  Frame f;
  f.addr = addr;
  f.cmd = CMD_GET;
  f.item = item;
  f.data = {static_cast<uint8_t>(word_offset & 0xFF), static_cast<uint8_t>(word_offset >> 8), byte_len};
  return encode(f);
}

// Requête d'écriture de mots 16 bits à partir du mot `word_offset`.
inline std::vector<uint8_t> build_set(uint8_t item, uint16_t word_offset, const std::vector<uint16_t> &words,
                                      uint8_t addr = 0) {
  Frame f;
  f.addr = addr;
  f.cmd = CMD_SET;
  f.item = item;
  f.data = {static_cast<uint8_t>(word_offset & 0xFF), static_cast<uint8_t>(word_offset >> 8)};
  for (uint16_t w : words) {
    f.data.push_back(static_cast<uint8_t>(w & 0xFF));
    f.data.push_back(static_cast<uint8_t>(w >> 8));
  }
  return encode(f);
}

// Lecteur de trames tolérant : resynchronisation sur 0xAA, rejet des checksums faux.
class FrameReader {
 public:
  void push(uint8_t b) {
    buf_.push_back(b);
    if (buf_.size() > 4 * MAX_FRAME_LEN)  // garde-fou mémoire
      buf_.erase(buf_.begin(), buf_.begin() + static_cast<long>(buf_.size() - MAX_FRAME_LEN));
  }
  void reset() { buf_.clear(); }
  size_t buffered() const { return buf_.size(); }

  // Extrait la prochaine trame valide, si elle est complète.
  bool pop(Frame &out) {
    while (true) {
      size_t skip = 0;
      while (skip < buf_.size() && buf_[skip] != START_BYTE)
        skip++;
      if (skip > 0)
        buf_.erase(buf_.begin(), buf_.begin() + static_cast<long>(skip));
      if (buf_.size() < HEADER_LEN)
        return false;
      size_t data_len = buf_[5];
      if (data_len > MAX_DATA_LEN) {  // longueur absurde : ce 0xAA n'était pas un début de trame
        buf_.erase(buf_.begin());
        continue;
      }
      size_t total = HEADER_LEN + data_len + 1;
      if (buf_.size() < total)
        return false;
      if (checksum(buf_.data(), total - 1) != buf_[total - 1]) {
        buf_.erase(buf_.begin());
        continue;
      }
      out.dev_type = buf_[1];
      out.addr = buf_[2];
      out.cmd = buf_[3];
      out.item = buf_[4];
      out.data.assign(buf_.begin() + HEADER_LEN, buf_.begin() + static_cast<long>(HEADER_LEN + data_len));
      buf_.erase(buf_.begin(), buf_.begin() + static_cast<long>(total));
      return true;
    }
  }

 private:
  std::vector<uint8_t> buf_;
};

// ---------------------------------------------------------------------------
// Décodage d'une réponse GET : un bloc de mots à partir d'un offset.
// ---------------------------------------------------------------------------
struct WordBlock {
  uint16_t offset{0};
  std::vector<uint16_t> words;

  bool contains(uint16_t word, size_t count = 1) const {
    return word >= offset && static_cast<size_t>(word - offset) + count <= words.size();
  }
  uint16_t u16(uint16_t word) const { return words[word - offset]; }
  int16_t s16(uint16_t word) const { return static_cast<int16_t>(words[word - offset]); }
  // Uint32s du PDF : struct { wHi16; wLow16; } et exemple 123456789 -> 5B 07 15 CD,
  // soit mot haut en premier. Le texte du PDF dit l'inverse : d'où l'option.
  uint32_t u32(uint16_t word, bool high_word_first) const {
    uint32_t a = words[word - offset];
    uint32_t b = words[word - offset + 1];
    return high_word_first ? ((a << 16) | b) : ((b << 16) | a);
  }
};

inline bool parse_block(const Frame &f, WordBlock &out) {
  if (f.cmd != CMD_SET_NO_RESP || f.data.size() < 2 || (f.data.size() % 2) != 0)
    return false;
  out.offset = static_cast<uint16_t>(f.data[0] | (f.data[1] << 8));
  out.words.clear();
  for (size_t i = 2; i + 1 < f.data.size(); i += 2)
    out.words.push_back(static_cast<uint16_t>(f.data[i] | (f.data[i + 1] << 8)));
  return true;
}

// ---------------------------------------------------------------------------
// Item 0 : état temps réel
// ---------------------------------------------------------------------------
struct State {
  uint16_t charge_mode{0};
  float pv_voltage{0}, battery_voltage{0}, charging_current{0};
  float load_voltage{0}, load_current{0};
  uint16_t charging_power{0}, load_power{0};
  int16_t battery_temp{0}, internal_temp{0};
  uint16_t battery_level{0};
  bool has_fault{false};
  uint16_t fault{0};
};

inline bool decode_state(const WordBlock &b, State &s) {
  if (!b.contains(0x00, 0x0C))  // au minimum mots 0x00..0x0B (jusqu'à wBatCap)
    return false;
  s.charge_mode = b.u16(0x00);
  s.pv_voltage = b.u16(0x01) / 10.0f;
  s.battery_voltage = b.u16(0x02) / 10.0f;
  s.charging_current = b.u16(0x03) / 10.0f;
  // 0x04 = wOutVolt (usage interne)
  s.load_voltage = b.u16(0x05) / 10.0f;
  s.load_current = b.u16(0x06) / 10.0f;
  s.charging_power = b.u16(0x07);
  s.load_power = b.u16(0x08);
  s.battery_temp = b.s16(0x09);  // int16 d'après le PDF (ssieb lisait en uint16 : bug sous 0 °C)
  s.internal_temp = b.s16(0x0A);
  s.battery_level = b.u16(0x0B);
  s.has_fault = b.contains(0x0E);
  s.fault = s.has_fault ? b.u16(0x0E) : 0;
  return true;
}

inline const char *charge_mode_name(uint16_t mode) {
  switch (mode) {
    case 0: return "Standby";
    case 1: return "MPPT";
    case 2: return "Bulk";
    case 3: return "Float";
    case 4: return "Pre-charge";
    default: return "Unknown";
  }
}

// Table 9-1 du PDF. La numérotation des bits (1 = bit 0 ?) n'est pas précisée :
// on suppose que le premier élément de la liste est le bit 0. À confirmer.
inline std::string fault_text(uint16_t fault) {
  static const char *const NAMES[] = {
      "Battery overvoltage",  "PV overvoltage",    "Charge overcurrent",       "Discharge overcurrent",
      "Battery temp alarm",   "Internal temp alarm", "PV undervoltage",        "Battery undervoltage",
      "Zero-trip protection", "Manual switchgear control",
  };
  if (fault == 0)
    return "OK";
  std::string out;
  for (unsigned bit = 0; bit < 16; bit++) {
    if (!(fault & (1u << bit)))
      continue;
    if (!out.empty())
      out += ", ";
    if (bit < sizeof(NAMES) / sizeof(NAMES[0]))
      out += NAMES[bit];
    else
      out += "bit " + std::to_string(bit);
  }
  return out;
}

// ---------------------------------------------------------------------------
// Item 2 : énergies (Wh)
// ---------------------------------------------------------------------------
struct Energy {
  uint32_t production_today_wh{0}, production_month_wh{0}, production_total_wh{0};
  uint32_t load_today_wh{0}, load_month_wh{0}, load_total_wh{0};
};

inline bool decode_energy(const WordBlock &b, bool high_word_first, Energy &e) {
  if (!b.contains(0x06, 0x10))  // mots 0x06..0x15
    return false;
  e.production_today_wh = b.u32(0x06, high_word_first);
  e.production_month_wh = b.u32(0x0A, high_word_first);
  e.production_total_wh = b.u32(0x0E, high_word_first);
  e.load_today_wh = b.u32(0x10, high_word_first);
  e.load_month_wh = b.u32(0x12, high_word_first);
  e.load_total_wh = b.u32(0x14, high_word_first);
  return true;
}

// ---------------------------------------------------------------------------
// Item 4 : sortie charge
// ---------------------------------------------------------------------------
struct LoadInfo {
  uint16_t module_select1{0};
  bool load_on{false};
};

inline bool decode_load(const WordBlock &b, LoadInfo &l) {
  if (!b.contains(0x01) || !b.contains(0x0F))
    return false;
  l.module_select1 = b.u16(0x01);
  l.load_on = b.u16(0x0F) != 0;
  return true;
}

inline std::string load_mode_text(uint16_t module) {
  if (module == 5100)
    return "Light control + delay";
  if (module >= 5101 && module <= 5115)
    return "Light control + timer " + std::to_string(module - 5100) + " h";
  if (module == 5116)
    return "Dual timer";
  if (module == LOAD_MODULE_ALWAYS_ON)
    return "Manual: always on";
  if (module == LOAD_MODULE_ALWAYS_OFF)
    return "Manual: always off";
  return "Unknown (" + std::to_string(module) + ")";
}

}  // namespace proto
}  // namespace esmart3
}  // namespace esphome
