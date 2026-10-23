// Tests de la couche protocole (aucune dépendance ESPHome).
// Les vecteurs viennent du PDF constructeur (PDF constructeur publié par skagmo : github.com/skagmo/esmart_mppt/files/3401128/esmart3-serial-comm.pdf, §9)
// et des trames codées en dur dans la lib skagmo/E-t0m.
//
//   make -C tests test

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "../components/esmart3/esmart3_protocol.h"

using namespace esphome::esmart3::proto;
using Bytes = std::vector<uint8_t>;

static int g_checks = 0;
static int g_failed = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    g_checks++;                                                           \
    if (!(cond)) {                                                        \
      g_failed++;                                                         \
      std::printf("ECHEC  %s:%d  %s\n", __FILE__, __LINE__, #cond);       \
    }                                                                     \
  } while (0)

static bool near(float a, float b) { return (a > b ? a - b : b - a) < 0.001f; }

static Bytes feed_all(const Bytes &in, std::vector<Frame> &frames) {
  FrameReader r;
  for (uint8_t b : in)
    r.push(b);
  Frame f;
  while (r.pop(f))
    frames.push_back(f);
  return {};
}

// --- Requêtes : doivent être identiques octet pour octet aux exemples du PDF ---
static void test_requests() {
  // PDF §9 "Read the data"
  CHECK(build_get(ITEM_STATE, 0, 0x1A) == (Bytes{0xAA, 0x01, 0x00, 0x01, 0x00, 0x03, 0x00, 0x00, 0x1A, 0x37}));
  // PDF §9 "Write the data" : type de batterie = 1
  CHECK(build_set(ITEM_BATTERY, 1, {1}) == (Bytes{0xAA, 0x01, 0x00, 0x02, 0x01, 0x04, 0x01, 0x00, 0x01, 0x00, 0x4C}));
  // Requête codée en dur dans le composant ssieb (24 octets)
  CHECK(build_get(ITEM_STATE, 0, 0x18) == (Bytes{0xAA, 1, 0, 1, 0, 3, 0, 0, 24, 0x39}));
  // REQUEST_MSG0 de E-t0m : adresse 1, 30 octets
  CHECK(build_get(ITEM_STATE, 0, 0x1E, 1) == (Bytes{0xAA, 0x01, 0x01, 0x01, 0x00, 0x03, 0x00, 0x00, 0x1E, 0x32}));
  // LOAD_ON / LOAD_OFF de E-t0m = wLoadModuleSelect1 (mot 1) à 5117 / 5118
  CHECK(build_set(ITEM_LOAD, 1, {LOAD_MODULE_ALWAYS_ON}, 1) ==
        (Bytes{0xAA, 0x01, 0x01, 0x02, 0x04, 0x04, 0x01, 0x00, 0xFD, 0x13, 0x39}));
  CHECK(build_set(ITEM_LOAD, 1, {LOAD_MODULE_ALWAYS_OFF}, 1) ==
        (Bytes{0xAA, 0x01, 0x01, 0x02, 0x04, 0x04, 0x01, 0x00, 0xFE, 0x13, 0x38}));
  CHECK(LOAD_MODULE_ALWAYS_ON == 0x13FD && LOAD_MODULE_ALWAYS_OFF == 0x13FE);
  // Requêtes émises par le composant (adresse 0), telles qu'écrites dans le README
  CHECK(build_get(ITEM_STATE, STATE_OFFSET, STATE_READ_BYTES) ==
        (Bytes{0xAA, 0x01, 0x00, 0x01, 0x00, 0x03, 0x00, 0x00, 0x1E, 0x33}));
  CHECK(build_get(ITEM_LOG, LOG_OFFSET, LOG_READ_BYTES) ==
        (Bytes{0xAA, 0x01, 0x00, 0x01, 0x02, 0x03, 0x06, 0x00, 0x20, 0x29}));
  CHECK(build_get(ITEM_LOAD, LOAD_OFFSET, LOAD_READ_BYTES) ==
        (Bytes{0xAA, 0x01, 0x00, 0x01, 0x04, 0x03, 0x01, 0x00, 0x1E, 0x2E}));
  // Requêtes que le composant émet réellement : le checksum doit se vérifier
  for (const Bytes &req : {build_get(ITEM_STATE, STATE_OFFSET, STATE_READ_BYTES),
                           build_get(ITEM_LOG, LOG_OFFSET, LOG_READ_BYTES),
                           build_get(ITEM_LOAD, LOAD_OFFSET, LOAD_READ_BYTES)}) {
    CHECK(checksum(req.data(), req.size() - 1) == req.back());
    uint8_t sum = 0;
    for (uint8_t b : req)
      sum = static_cast<uint8_t>(sum + b);
    CHECK(sum == 0);
  }
}

// --- Réponses du PDF ---
static void test_pdf_responses() {
  // ACK d'une écriture de batterie, PDF §9
  std::vector<Frame> fr;
  feed_all({0xAA, 0x01, 0x00, 0x00, 0x01, 0x00, 0x54}, fr);
  CHECK(fr.size() == 1);
  CHECK(fr[0].cmd == CMD_ACK && fr[0].item == ITEM_BATTERY && fr[0].data.empty());

  // ACK de la correction d'heure (item 6)
  fr.clear();
  feed_all({0xAA, 0x01, 0x00, 0x00, 0x06, 0x00, 0x4F}, fr);
  CHECK(fr.size() == 1 && fr[0].cmd == CMD_ACK && fr[0].item == ITEM_REMOTE);

  // Trame de correction d'heure du PDF (checksum 0x30)
  fr.clear();
  feed_all({0xAA, 0x01, 0x00, 0x02, 0x06, 0x10, 0x01, 0x00, 0x04, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x02,
            0x00, 0x02, 0x00, 0x01, 0x00, 0x30},
           fr);
  CHECK(fr.size() == 1 && fr[0].cmd == CMD_SET && fr[0].data.size() == 16);

  // Réponse temps réel du PDF §9 (35 octets, checksum 0x90)
  const Bytes resp = {0xAA, 0x01, 0x00, 0x03, 0x00, 0x1C, 0x00, 0x00, 0x00, 0x00, 0x2F, 0x00, 0x0C, 0x00, 0xC8, 0x00,
                      0x0B, 0x00, 0x0C, 0x00, 0xF6, 0x01, 0x18, 0x00, 0x00, 0x00, 0x05, 0x00, 0x78, 0x00, 0x00, 0x00,
                      0x00, 0x00, 0x90};
  fr.clear();
  feed_all(resp, fr);
  CHECK(fr.size() == 1);
  if (fr.size() != 1)
    return;
  CHECK(fr[0].cmd == CMD_SET_NO_RESP && fr[0].item == ITEM_STATE);
  WordBlock b;
  CHECK(parse_block(fr[0], b));
  CHECK(b.offset == 0 && b.words.size() == 13);
  State s;
  CHECK(decode_state(b, s));
  CHECK(s.charge_mode == 0);
  CHECK(near(s.pv_voltage, 4.7f));  // le PDF : « the PV voltage is 4.7V »
  CHECK(near(s.battery_voltage, 1.2f));
  CHECK(near(s.charging_current, 20.0f));
  CHECK(near(s.load_voltage, 1.2f));
  CHECK(near(s.load_current, 50.2f));
  CHECK(s.charging_power == 24 && s.load_power == 0);
  CHECK(s.battery_temp == 5 && s.internal_temp == 120 && s.battery_level == 0);
  CHECK(!s.has_fault);  // cette réponse s'arrête avant wFault
}

// --- Températures négatives : le bug du composant ssieb ---
static void test_negative_temperature() {
  WordBlock b;
  b.offset = 0;
  b.words.assign(15, 0);
  b.words[0x09] = 0xFFFE;  // -2 °C
  b.words[0x0A] = 0xFFF6;  // -10 °C
  b.words[0x0E] = 0x0001;  // défaut : bit 0
  State s;
  CHECK(decode_state(b, s));
  CHECK(s.battery_temp == -2);
  CHECK(s.internal_temp == -10);
  CHECK(s.has_fault && s.fault == 1);
  CHECK(fault_text(0) == "OK");
  CHECK(fault_text(1) == "Battery overvoltage");
  CHECK(fault_text(0x0005) == "Battery overvoltage, Charge overcurrent");
  CHECK(fault_text(0x8000) == "bit 15");
}

// --- Entiers 32 bits : exemple du PDF (123456789 -> 5B 07 15 CD) ---
static void test_u32() {
  // Réponse GET fabriquée : offset 0x06, 16 mots, énergie du jour = 123456789 Wh
  Frame f;
  f.cmd = CMD_SET_NO_RESP;
  f.item = ITEM_LOG;
  f.data = {0x06, 0x00};
  for (int i = 0; i < 16; i++) {
    uint16_t w = 0;
    if (i == 0) w = 0x075B;  // 5B 07 en little-endian
    if (i == 1) w = 0xCD15;  // 15 CD
    if (i == 2) w = 0x0101;  // wTodayEngDate (ignoré)
    if (i == 4) w = 0x0000;  // mois : mot haut
    if (i == 5) w = 0x1234;  // mois : mot bas = 4660 Wh
    f.data.push_back(static_cast<uint8_t>(w & 0xFF));
    f.data.push_back(static_cast<uint8_t>(w >> 8));
  }
  Bytes wire = encode(f);
  std::vector<Frame> fr;
  feed_all(wire, fr);
  CHECK(fr.size() == 1);
  if (fr.size() != 1)
    return;
  WordBlock b;
  CHECK(parse_block(fr[0], b));
  CHECK(b.offset == 6 && b.words.size() == 16);
  Energy e;
  CHECK(decode_energy(b, true, e));
  CHECK(e.production_today_wh == 123456789u);
  CHECK(e.production_month_wh == 4660u);
  CHECK(e.production_total_wh == 0u && e.load_today_wh == 0u);
  Energy e2;
  CHECK(decode_energy(b, false, e2));  // option low_first : doit donner autre chose
  CHECK(e2.production_today_wh == 0xCD15075Bu);

  // Bloc trop court : refus propre
  WordBlock small;
  small.offset = 6;
  small.words.assign(4, 0);
  CHECK(!decode_energy(small, true, e));
}

// --- Sortie charge ---
static void test_load() {
  WordBlock b;
  b.offset = 1;
  b.words.assign(15, 0);  // mots 0x01..0x0F
  b.words[0] = 5117;      // wLoadModuleSelect1
  b.words[0x0F - 1] = 1;  // wLoadSts
  LoadInfo l;
  CHECK(decode_load(b, l));
  CHECK(l.module_select1 == 5117 && l.load_on);
  CHECK(load_mode_text(5117) == "Manual: always on");
  CHECK(load_mode_text(5118) == "Manual: always off");
  CHECK(load_mode_text(5100) == "Light control + delay");
  CHECK(load_mode_text(5103) == "Light control + timer 3 h");
  CHECK(load_mode_text(5116) == "Dual timer");
  b.words[0x0F - 1] = 0;
  CHECK(decode_load(b, l) && !l.load_on);
  WordBlock small;
  small.offset = 1;
  small.words.assign(3, 0);
  CHECK(!decode_load(small, l));
  CHECK(std::strcmp(charge_mode_name(1), "MPPT") == 0 && std::strcmp(charge_mode_name(9), "Unknown") == 0);
}

// --- Robustesse du lecteur : bruit, trames coupées, mauvais checksum, écho ---
static void test_reader_robustness() {
  const Bytes ack = {0xAA, 0x01, 0x00, 0x00, 0x01, 0x00, 0x54};
  const Bytes req = build_get(ITEM_STATE, 0, 0x1E);  // écho de notre propre requête (RS485 en boucle)

  Bytes stream = {0x00, 0x13, 0xFF};                // bruit avant
  stream.insert(stream.end(), req.begin(), req.end());  // écho de la requête
  Bytes bad = ack;
  bad.back() ^= 0x01;                                // checksum faux
  stream.insert(stream.end(), bad.begin(), bad.end());
  stream.insert(stream.end(), ack.begin(), ack.end());  // trame valide juste après
  stream.push_back(0xAA);                            // début de trame coupé en fin de flux

  std::vector<Frame> fr;
  FrameReader r;
  for (uint8_t b : stream)  // octet par octet, comme sur l'UART
    r.push(b);
  Frame f;
  while (r.pop(f))
    fr.push_back(f);
  CHECK(fr.size() == 2);
  if (fr.size() == 2) {
    CHECK(fr[0].cmd == CMD_GET);  // l'écho est décodé mais le composant l'ignorera (cmd = GET)
    CHECK(fr[1].cmd == CMD_ACK);
  }
  CHECK(r.buffered() == 1);  // le 0xAA final attend la suite

  // Longueur annoncée absurde (0xFF) : ne doit pas bloquer le lecteur
  FrameReader r2;
  for (uint8_t b : Bytes{0xAA, 0x01, 0x00, 0x03, 0x00, 0xFF})
    r2.push(b);
  for (uint8_t b : ack)
    r2.push(b);
  Frame g;
  CHECK(r2.pop(g) && g.cmd == CMD_ACK);

  // Garde-fou mémoire : un flot sans 0xAA ne fait pas grossir le tampon
  FrameReader r3;
  for (int i = 0; i < 5000; i++)
    r3.push(0x55);
  Frame h;
  CHECK(!r3.pop(h));
  CHECK(r3.buffered() == 0);
}

int main() {
  test_requests();
  test_pdf_responses();
  test_negative_temperature();
  test_u32();
  test_load();
  test_reader_robustness();
  std::printf("%d vérifications, %d échec(s)\n", g_checks, g_failed);
  return g_failed == 0 ? 0 : 1;
}
