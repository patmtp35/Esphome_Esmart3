// Banc de simulation du composant ESMART3 sans matériel ni ESPHome.
//
// On compile esmart3.cpp contre de fausses en-têtes ESPHome (tests/stubs) : UART scriptée,
// horloge pilotée par le test, capteurs qui enregistrent leur dernière valeur. Ça vérifie la
// logique propre au composant (file de requêtes, timeouts, NACK, interrupteur), PAS la
// conformité à l'API ESPHome réelle.
//
//   make -C tests test

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

#include "esmart3.h"

namespace sim {
uint32_t now_ms = 0;
std::string log;
void logf(const char *level, const char *fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  log += level;
  log += ": ";
  log += buf;
  log += "\n";
}
}  // namespace sim

using namespace esphome;
using namespace esphome::esmart3;
using Bytes = std::vector<uint8_t>;

static int g_checks = 0, g_failed = 0;
#define CHECK(cond)                                                \
  do {                                                             \
    g_checks++;                                                    \
    if (!(cond)) {                                                 \
      g_failed++;                                                  \
      std::printf("ECHEC  %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                              \
  } while (0)

static bool near(float a, float b, float eps = 0.001f) { return std::fabs(a - b) < eps; }

// --- fabrication de trames venant du régulateur ---
static Bytes data_frame(uint8_t item, uint16_t offset, const std::vector<uint16_t> &words) {
  proto::Frame f;
  f.cmd = proto::CMD_SET_NO_RESP;
  f.item = item;
  f.data = {static_cast<uint8_t>(offset & 0xFF), static_cast<uint8_t>(offset >> 8)};
  for (uint16_t w : words) {
    f.data.push_back(static_cast<uint8_t>(w & 0xFF));
    f.data.push_back(static_cast<uint8_t>(w >> 8));
  }
  return proto::encode(f);
}
static Bytes plain_frame(uint8_t cmd, uint8_t item) {
  proto::Frame f;
  f.cmd = cmd;
  f.item = item;
  return proto::encode(f);
}
static Bytes state_response() {
  // mode MPPT, 50,0 V PV, 14,1 V bat, 10,0 A, 15,0 W ... batterie -2 °C, SOC 80 %, défaut bit 0
  return data_frame(proto::ITEM_STATE, 0,
                    {1, 500, 141, 100, 0, 140, 10, 1410, 14, 0xFFFE, 35, 80, 0, 0, 0x0001});
}
static Bytes energy_response() {
  return data_frame(proto::ITEM_LOG, 6,
                    {0, 12345, 0x0101, 0, 4, 37856, 0x0101, 0, 18, 54919, 0, 1000, 0, 20000, 1, 24464});
}
static Bytes load_response(uint16_t module, bool on) {
  std::vector<uint16_t> w(15, 0);  // mots 0x01..0x0F
  w[0] = module;
  w[0x0F - 1] = on ? 1 : 0;
  return data_frame(proto::ITEM_LOAD, 1, w);
}

// --- pilotage ---
static void feed(ESmart3Component &hub, const Bytes &b) {
  for (uint8_t x : b)
    hub.rx.push_back(x);
}
static void advance(ESmart3Component &hub, uint32_t ms, uint32_t step = 10) {
  for (uint32_t t = 0; t < ms; t += step) {
    sim::now_ms += step;
    hub.loop();
  }
}
// Avance jusqu'à ce que le composant ait émis `n` trames au total.
static bool wait_tx(ESmart3Component &hub, size_t n, uint32_t limit_ms = 3000) {
  for (uint32_t t = 0; t < limit_ms && hub.tx.size() < n; t += 10) {
    sim::now_ms += 10;
    hub.loop();
  }
  return hub.tx.size() >= n;
}

static const Bytes REQ_STATE = proto::build_get(proto::ITEM_STATE, proto::STATE_OFFSET, proto::STATE_READ_BYTES);
static const Bytes REQ_ENERGY = proto::build_get(proto::ITEM_LOG, proto::LOG_OFFSET, proto::LOG_READ_BYTES);
static const Bytes REQ_LOAD = proto::build_get(proto::ITEM_LOAD, proto::LOAD_OFFSET, proto::LOAD_READ_BYTES);

struct Rig {
  ESmart3Component hub;
  ESmart3LoadSwitch sw;
  sensor::Sensor pv, bat, chg_a, load_v, load_a, chg_w, load_w, bat_t, int_t, soc, mode, fault_code;
  sensor::Sensor e_today, e_month, e_total, le_today, le_month, le_total;
  text_sensor::TextSensor mode_txt, faults_txt, load_mode_txt;
  binary_sensor::BinarySensor load_on, fault;

  Rig(bool with_switch = true) {
    hub.set_pv_voltage_sensor(&pv);
    hub.set_battery_voltage_sensor(&bat);
    hub.set_charging_current_sensor(&chg_a);
    hub.set_load_voltage_sensor(&load_v);
    hub.set_load_current_sensor(&load_a);
    hub.set_charging_power_sensor(&chg_w);
    hub.set_load_power_sensor(&load_w);
    hub.set_battery_temperature_sensor(&bat_t);
    hub.set_internal_temperature_sensor(&int_t);
    hub.set_battery_level_sensor(&soc);
    hub.set_charge_mode_sensor(&mode);
    hub.set_fault_code_sensor(&fault_code);
    hub.set_energy_today_sensor(&e_today);
    hub.set_energy_month_sensor(&e_month);
    hub.set_energy_total_sensor(&e_total);
    hub.set_load_energy_today_sensor(&le_today);
    hub.set_load_energy_month_sensor(&le_month);
    hub.set_load_energy_total_sensor(&le_total);
    hub.set_charge_mode_text_sensor(&mode_txt);
    hub.set_faults_text_sensor(&faults_txt);
    hub.set_load_mode_text_sensor(&load_mode_txt);
    hub.set_load_on_binary_sensor(&load_on);
    hub.set_fault_binary_sensor(&fault);
    if (with_switch) {
      sw.set_parent(&hub);
      hub.set_load_switch(&sw);
    }
    hub.setup();
  }
};

// Déroule un cycle complet (état + énergies + sortie charge) avec réponses normales.
static void full_cycle(Rig &r, uint16_t module = proto::LOAD_MODULE_ALWAYS_OFF, bool load_on = false) {
  size_t base = r.hub.tx.size();
  r.hub.update();
  CHECK(wait_tx(r.hub, base + 1));
  feed(r.hub, state_response());
  advance(r.hub, 20);
  CHECK(wait_tx(r.hub, base + 2));
  feed(r.hub, energy_response());
  advance(r.hub, 20);
  CHECK(wait_tx(r.hub, base + 3));
  feed(r.hub, load_response(module, load_on));
  advance(r.hub, 20);
}

static void test_normal_cycle() {
  sim::now_ms = 1000;
  Rig r;
  size_t writes_at_boot = 0;
  r.hub.update();
  CHECK(wait_tx(r.hub, 1));
  CHECK(r.hub.tx[0] == REQ_STATE);

  // L'écho de notre propre requête sur RX (module RS485 en boucle) ne doit rien déclencher.
  feed(r.hub, REQ_STATE);
  advance(r.hub, 30);
  CHECK(r.pv.n == 0);
  CHECK(r.hub.tx.size() == 1);  // et la requête reste en attente de sa vraie réponse

  feed(r.hub, state_response());
  advance(r.hub, 20);
  CHECK(r.pv.n == 1 && near(r.pv.state, 50.0f));
  CHECK(near(r.bat.state, 14.1f) && near(r.chg_a.state, 10.0f));
  CHECK(near(r.load_v.state, 14.0f) && near(r.load_a.state, 1.0f));
  CHECK(near(r.chg_w.state, 1410) && near(r.load_w.state, 14));
  CHECK(near(r.bat_t.state, -2.0f));  // négatif correct
  CHECK(near(r.int_t.state, 35) && near(r.soc.state, 80) && near(r.mode.state, 1));
  CHECK(near(r.fault_code.state, 1) && r.fault.state && r.fault.has);
  CHECK(r.mode_txt.state == "MPPT");
  CHECK(r.faults_txt.state == "Battery overvoltage");

  // 2e requête : énergies (compteur lent à 0 au premier update)
  CHECK(wait_tx(r.hub, 2));
  CHECK(r.hub.tx[1] == REQ_ENERGY);
  feed(r.hub, energy_response());
  advance(r.hub, 20);
  CHECK(near(r.e_today.state, 12.345f) && near(r.e_month.state, 300.0f) && near(r.e_total.state, 1234.567f, 0.01f));
  CHECK(near(r.le_today.state, 1.0f) && near(r.le_month.state, 20.0f) && near(r.le_total.state, 90.0f));

  // 3e requête : sortie charge
  CHECK(wait_tx(r.hub, 3));
  CHECK(r.hub.tx[2] == REQ_LOAD);
  feed(r.hub, load_response(proto::LOAD_MODULE_ALWAYS_OFF, false));
  advance(r.hub, 20);
  CHECK(r.load_on.has && !r.load_on.state);
  CHECK(r.load_mode_txt.state == "Manual: always off");
  CHECK(r.sw.published == 1 && !r.sw.state);

  // Aucune trame d'écriture n'est partie : la 2e octet commande (index 3) vaut GET (1) partout.
  for (const Bytes &t : r.hub.tx)
    if (t.size() > 3 && t[3] != proto::CMD_GET)
      writes_at_boot++;
  CHECK(writes_at_boot == 0);
  CHECK(!r.hub.warning);

  // Cycle suivant : le compteur lent (1/6) ne relit ni énergies ni sortie charge.
  size_t before = r.hub.tx.size();
  r.hub.update();
  advance(r.hub, 200);
  CHECK(r.hub.tx.size() == before + 1 && r.hub.tx.back() == REQ_STATE);
  feed(r.hub, state_response());
  advance(r.hub, 20);
  advance(r.hub, 300);
  CHECK(r.hub.tx.size() == before + 1);
}

static void test_timeouts_and_recovery() {
  sim::now_ms = 5000;
  sim::log.clear();
  Rig r;
  full_cycle(r);
  CHECK(r.pv.n >= 1);
  float last_good = r.pv.state;
  CHECK(near(last_good, 50.0f));

  // Le régulateur se tait : 6 requêtes sans réponse.
  for (int i = 0; i < 6; i++) {
    size_t base = r.hub.tx.size();
    r.hub.update();
    CHECK(wait_tx(r.hub, base + 1));
    advance(r.hub, 1200);  // dépasse RESPONSE_TIMEOUT_MS
    if (i == 1)
      CHECK(!r.hub.warning);  // pas d'alerte avant 3 échecs
    if (i == 2)
      CHECK(r.hub.warning);
  }
  CHECK(std::isnan(r.pv.state) && std::isnan(r.bat_t.state));  // plus de valeur périmée
  CHECK(sim::log.find("No response") != std::string::npos);

  // Retour du régulateur : tout redevient valide.
  size_t base = r.hub.tx.size();
  r.hub.update();
  CHECK(wait_tx(r.hub, base + 1));
  feed(r.hub, state_response());
  advance(r.hub, 20);
  CHECK(near(r.pv.state, 50.0f) && !r.hub.warning);
}

static void test_state_nack_fallback() {
  sim::now_ms = 20000;
  sim::log.clear();
  Rig r;
  // 3 NACK consécutifs sur la lecture de l'état -> repli sur 24 octets.
  for (int i = 0; i < 3; i++) {
    size_t base = r.hub.tx.size();
    r.hub.update();
    CHECK(wait_tx(r.hub, base + 1));
    CHECK(r.hub.tx.back() == REQ_STATE);
    feed(r.hub, plain_frame(proto::CMD_NACK, proto::ITEM_STATE));
    advance(r.hub, 100);
    // les lectures lentes (énergies, sortie charge) restent en file : on les laisse sans réponse
    advance(r.hub, 2500);
  }
  CHECK(sim::log.find("Falling back") != std::string::npos);
  size_t base = r.hub.tx.size();
  r.hub.update();
  advance(r.hub, 200);
  bool found = false;
  for (size_t i = base; i < r.hub.tx.size(); i++)
    if (r.hub.tx[i] == proto::build_get(proto::ITEM_STATE, 0, 0x18))
      found = true;
  CHECK(found);

  // Réponse courte (12 mots, sans wFault) : décodée, sans code défaut.
  feed(r.hub, data_frame(proto::ITEM_STATE, 0, {2, 520, 143, 50, 0, 143, 0, 700, 0, 12, 30, 90}));
  advance(r.hub, 20);
  CHECK(near(r.pv.state, 52.0f) && r.mode_txt.state == "Bulk");
  CHECK(r.fault_code.n == 0 && !r.fault.has);  // pas de wFault dans cette réponse : rien de publié
}

static void test_optional_reads_disabled_after_refusals() {
  sim::now_ms = 40000;
  sim::log.clear();
  Rig r;
  for (int i = 0; i < 3; i++) {
    size_t base = r.hub.tx.size();
    r.hub.update();
    CHECK(wait_tx(r.hub, base + 1));
    feed(r.hub, state_response());
    advance(r.hub, 20);
    CHECK(wait_tx(r.hub, base + 2));
    CHECK(r.hub.tx.back() == REQ_ENERGY);
    feed(r.hub, plain_frame(proto::CMD_ERR, proto::ITEM_LOG));
    advance(r.hub, 20);
    // sortie charge : réponse normale
    CHECK(wait_tx(r.hub, base + 3));
    feed(r.hub, load_response(proto::LOAD_MODULE_ALWAYS_OFF, false));
    advance(r.hub, 20);
    // on force le compteur lent à 0 pour le cycle suivant
    for (int k = 0; k < 5; k++)
      r.hub.update();
    advance(r.hub, 100);
    for (int k = 0; k < 2; k++) {
      // vider les requêtes d'état en attente
      feed(r.hub, state_response());
      advance(r.hub, 100);
    }
  }
  CHECK(sim::log.find("Giving up on item 2") != std::string::npos);
}

static void test_load_switch() {
  sim::now_ms = 60000;
  sim::log.clear();
  Rig r;
  full_cycle(r, proto::LOAD_MODULE_ALWAYS_OFF, false);
  CHECK(r.sw.state == false);
  size_t base = r.hub.tx.size();

  // Commande ON -> trame d'écriture envoyée en priorité
  r.sw.turn_on();
  CHECK(wait_tx(r.hub, base + 1));
  CHECK(r.hub.tx.back() == proto::build_set(proto::ITEM_LOAD, 1, {proto::LOAD_MODULE_ALWAYS_ON}));
  CHECK(r.sw.state == false);  // pas d'état optimiste : on attend la relecture

  // ACK -> relecture de la sortie charge -> l'interrupteur suit l'état réel
  feed(r.hub, plain_frame(proto::CMD_ACK, proto::ITEM_LOAD));
  advance(r.hub, 20);
  CHECK(wait_tx(r.hub, base + 2));
  CHECK(r.hub.tx.back() == REQ_LOAD);
  feed(r.hub, load_response(proto::LOAD_MODULE_ALWAYS_ON, true));
  advance(r.hub, 20);
  CHECK(r.sw.state == true && r.load_on.state == true);
  CHECK(r.load_mode_txt.state == "Manual: always on");

  // Commande OFF refusée (NACK) : l'interrupteur revient à l'état réel (ON)
  r.sw.turn_off();
  CHECK(wait_tx(r.hub, base + 3));
  CHECK(r.hub.tx.back() == proto::build_set(proto::ITEM_LOAD, 1, {proto::LOAD_MODULE_ALWAYS_OFF}));
  int published = r.sw.published;
  feed(r.hub, plain_frame(proto::CMD_NACK, proto::ITEM_LOAD));
  advance(r.hub, 20);
  CHECK(r.sw.published == published + 1 && r.sw.state == true);
  CHECK(sim::log.find("refused") != std::string::npos);

  // Commande sans réponse : timeout, l'interrupteur reste sur l'état réel
  r.sw.turn_off();
  CHECK(wait_tx(r.hub, base + 4));
  published = r.sw.published;
  advance(r.hub, 1200);
  CHECK(r.sw.published == published + 1 && r.sw.state == true);
}

static void test_garbage_and_partial() {
  sim::now_ms = 90000;
  Rig r;
  r.hub.update();
  CHECK(wait_tx(r.hub, 1));
  // bruit + moitié de trame, puis silence : le tampon est jeté, la vraie réponse passe ensuite
  Bytes half = state_response();
  half.resize(10);
  feed(r.hub, {0x00, 0x13});
  feed(r.hub, half);
  advance(r.hub, 400);  // dépasse PARTIAL_FRAME_TIMEOUT_MS
  feed(r.hub, state_response());
  advance(r.hub, 20);
  CHECK(r.pv.n == 1 && near(r.pv.state, 50.0f));
}

int main() {
  test_normal_cycle();
  test_timeouts_and_recovery();
  test_state_nack_fallback();
  test_optional_reads_disabled_after_refusals();
  test_load_switch();
  test_garbage_and_partial();
  std::printf("[hub] %d vérifications, %d échec(s)\n", g_checks, g_failed);
  if (g_failed != 0)
    std::printf("--- journal du dernier scénario ---\n%s", sim::log.c_str());
  return g_failed == 0 ? 0 : 1;
}
