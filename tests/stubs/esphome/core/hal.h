#pragma once
// Doublure d'ESPHome pour les tests hors matériel : horloge pilotée par le test.
#include <cstdint>
namespace sim { extern uint32_t now_ms; }
inline uint32_t millis() { return sim::now_ms; }
