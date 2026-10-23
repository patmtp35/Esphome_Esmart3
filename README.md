# esphome-esmart3

Composant ESPHome pour le régulateur solaire **eSmart3 MPPT** (Supower / Yueqing, version 40 A),
lu par son port RJ45 RS485. Il remplace le composant `esmart3` de ssieb, dont il reprend l'idée
en corrigeant ses défauts et en ajoutant ce que le protocole permet.

Basé sur les travaux de skagmo, ssieb et mtkodat, recoupés avec E-t0m et joba-1 (voir *Sources*).
Licence : Apache 2.0 (`LICENSE`).

> **Ancienne version (juin 2024)** : conservée telle quelle dans [`archive/`](archive/) ; l'état d'origine reste aussi
> consultable dans l'historique (commit `d9ac21b`, 21 juin 2024). Elle utilisait le composant de ssieb avec des fichiers YAML (ESP32 testé, D1 Mini non fonctionnel).

> **Statut de vérification** (à lire avant de flasher)
>
> | Élément | État |
> |---|---|
> | Couche protocole (trames, checksums, décodage, lecteur) | **Testée** : 69 vérifications `g++` (ASan/UBSan) dont les exemples du PDF constructeur |
> | Logique du composant (file de requêtes, timeouts, NACK, interrupteur) | **Simulée** : 85 vérifications contre de fausses en-têtes ESPHome (UART et horloge scriptées) |
> | Conformité à l'API ESPHome réelle (C++ et Python) | **Jamais compilé ni validé avec ESPHome** |
> | Fichiers Python (schémas YAML) | Syntaxe vérifiée, **jamais validés par `esphome config`** |
> | Matériel réel | **Jamais testé** |
>
> Le réseau de l'environnement où cette version a été écrite bloquait PyPI, donc ESPHome n'a pas pu y
> être installé. Première chose à faire chez vous : `esphome config examples/esmart3.yaml`, puis
> `esphome compile`. Si une erreur apparaît, elle viendra de l'API ESPHome (versions récentes) et
> sera locale à `components/esmart3/*.py` ou `esmart3.h/.cpp`.

## Câblage

Port RJ45 du régulateur (source : revue skagmo.com, confirmée par la lib E-t0m) :

| Broche RJ45 | Fonction |
|---|---|
| 1 | RS485 A (+) |
| 2 | RS485 B (−) |
| 5, 6 | GND |
| 7, 8 | +5 V en sortie (voir l'alimentation ci-dessous) |

Vers l'ESP32 : un module **RS485 ↔ TTL à direction automatique** (« auto flow control »), `TX → GPIO17`,
`RX → GPIO16` (modifiables).

**Montage déjà éprouvé** (ancienne version, ESP32 WROOM + petit module RS485 à 4 broches RX/TX/VCC/GND, photo dans
`archive/img/`) : broches RJ45 7 et 8 vers le VCC du module, A vers A, B vers B, broches 5 et 6 vers GND, puis
RX/TX/GND du module vers l'ESP32. Le 5 V du port RJ45 y alimentait aussi l'ESP32. Attention au choix des paires
du câble réseau pour les ponts VCC/GND.

**Alimentation : deux retours d'expérience contradictoires.** L'ancienne version fonctionnait sur le 5 V du régulateur ;
la lib Joba affirme au contraire qu'il est trop faible pour un ESP32 avec le Wi-Fi. Si l'ESP32 redémarre sans raison
(brown-out), alimentez-le à part et ne reliez que le module RS485 au port RJ45.

* Le composant **ne pilote aucune broche DE/RE** : un MAX485 nu ne fonctionnera pas.
* **Débranchez le Cloud Box M2.** Deux maîtres sur le même bus se télescopent. (Le composant sait
  profiter de ses réponses s'il reste branché, mais les collisions rendent les lectures aléatoires.)
* Le côté RS485 du régulateur travaille en **5 V**. Un module alimenté en 5 V a une sortie TTL à 5 V : mettez
  un adaptateur de niveau vers l'ESP32 (3,3 V), ou alimentez le module en 3,3 V comme ci-dessus si le sien le
  permet. La lib Joba précise aussi que le 5 V du port RJ45 est **trop faible pour alimenter un ESP32 avec le Wi-Fi**.
* Liaison fixée à **9600 bauds, 8N1** ; une autre valeur de `uart:` est refusée à la validation.

## Installation

```yaml
external_components:
  - source:
      type: local
      path: components      # chemin vers ce dépôt
    components: [esmart3]
```

Voir `examples/esmart3.yaml` pour une configuration complète (capteurs, textes, binaires).

## Entités

| Plateforme | Clé | Contenu |
|---|---|---|
| `sensor` | `pv_voltage`, `battery_voltage`, `charging_current`, `load_voltage`, `load_current`, `charging_power`, `load_power` | mesures temps réel (V, A, W) |
| `sensor` | `battery_temperature`, `internal_temperature` | °C, **signées** (négatif correct) |
| `sensor` | `battery_level` | SOC en %, calculé par le régulateur |
| `sensor` | `charge_mode`, `fault_code` | codes bruts (diagnostic) |
| `sensor` | `energy_today`, `energy_month`, `energy_total` | production en kWh |
| `sensor` | `load_energy_today`, `load_energy_month`, `load_energy_total` | consommation de la sortie charge en kWh |
| `text_sensor` | `charge_mode` | Standby / MPPT / Bulk / Float / Pre-charge |
| `text_sensor` | `faults` | défauts décodés, `OK` si aucun |
| `text_sensor` | `load_mode` | mode configuré de la sortie charge |
| `binary_sensor` | `load_on` | sortie charge active |
| `binary_sensor` | `fault` | au moins un défaut |
| `switch` | `load` | *optionnel, voir plus bas* |

Les énergies sont lues 1 fois sur `slow_update_every` interrogations (6 par défaut, soit 60 s avec
`update_interval: 10s`). Si le régulateur ne répond plus pendant ~6 requêtes, les capteurs numériques
passent à `NaN` au lieu de rester figés sur une valeur périmée.

## Différences avec le composant ssieb

* Températures lues en **signé** (`int16`, comme dans le PDF). ssieb les lit en non signé : −2 °C devient 65534.
* Lecture de `wFault` (code défaut), des **énergies** jour/mois/total et de l'**état de la sortie charge**.
* Requêtes **une à la fois** avec file d'attente, délai de réponse et resynchronisation sur `0xAA`.
* Tolère l'**écho** de TX sur RX (certains modules RS485 rebouclent) : seules les réponses sont décodées.
* Aucune écriture au démarrage, aucune écriture sans action explicite de l'utilisateur.

## Points à vérifier sur votre matériel

1. **Ordre des mots des entiers 32 bits (énergies).** Le PDF se contredit : le texte dit « mot bas
   en premier », mais sa structure `Uint32s { wHi16; wLow16 }` et son exemple (`123456789` → `5B 07 15 CD`)
   placent le **mot haut en premier**. C'est le défaut (`u32_word_order: high_first`). Si
   `energy_today` affiche des valeurs énormes, passez à `low_first`.
2. **Numérotation des bits de défaut.** Le tableau 9-1 du PDF ne précise pas la numérotation. Le composant
   prend le premier défaut pour le bit 0, ce que confirment les masques `0x001`…`0x200` de la lib
   [joba-1/Joba_ESmart3](https://github.com/joba-1/Joba_ESmart3) (même liste, même ordre). Le code brut reste
   disponible (`fault_code`).
3. **Longueur de lecture.** Le composant demande 30 octets (jusqu'à `wFault`), comme la lib E-t0m ; la lib
   Joba lit la structure entière (32 octets) sans problème signalé.
   Si le régulateur refuse (NACK) 3 fois de suite, il se replie sur 24 octets (lecture de ssieb) et
   `fault_code` n'est plus disponible.
4. **Précision des mesures.** D'après la revue de skagmo (unité de 2019, pas forcément la vôtre) :
   tension affichée ~10 % trop basse, courant ~1,5 A trop bas, courant et puissance **PV inventés** par
   le firmware (puissance batterie + 1 %), SOC calculé sur la tension seule. C'est pourquoi aucune entité
   « puissance PV » n'est exposée. Comparez avec un multimètre et corrigez avec `filters:` côté ESPHome.
5. **Les énergies** reposent sur ces mêmes mesures côté batterie : utilisez-les pour des tendances,
   pas pour de la comptabilité.

## Interrupteur de la sortie charge (optionnel, non testé)

```yaml
switch:
  - platform: esmart3
    esmart3_id: esmart3_hub
    load:
      name: "eSmart3 Sortie charge"
```

À activer en connaissance de cause :

* Il **écrit `wLoadModuleSelect1`** dans le régulateur : `5117` (toujours ON) ou `5118` (toujours OFF).
  Cela **remplace le mode configuré** (minuterie, commande par la lumière…). Relevez d'abord votre mode actuel
  avec l'entité `load_mode`. Le composant ne sait pas le restaurer.
* Le sens de 5117/5118 vient des trames `LOAD_ON`/`LOAD_OFF` de la lib E-t0m ; le PDF ne les nomme que
  « Normally open/close state », ambigu. Vérifiez sur banc avec une charge sans conséquence.
* La lib Joba écrit exactement les mêmes valeurs (5117/5118, item 4, offset 1) et ne sauvegarde pas non plus
  le mode précédent. Elle précise que la commande par minuterie ou par la lumière n'est pas gérée par `setLoad`.
* On ignore si le régulateur mémorise ce réglage en flash : **n'automatisez pas de basculements fréquents**.
* Le commutateur n'est **jamais restauré au démarrage** et affiche l'état réellement relu après l'écriture.

Les paramètres de batterie (item 1), de protection (item 7) et de calibration (item 3) ne sont **volontairement
pas écrits** par ce composant.

## Protocole (résumé)

Trame : `AA | type | adresse | cmd | item | longueur | données | checksum`, mots en little-endian,
`checksum = (0 − somme des octets précédents) & 0xFF`.

| Lecture | Requête émise (adresse 0) | Mots lus |
|---|---|---|
| État temps réel (item 0) | `AA 01 00 01 00 03 00 00 1E 33` | 0x00–0x0E |
| Énergies (item 2) | `AA 01 00 01 02 03 06 00 20 29` | 0x06–0x15 |
| Sortie charge (item 4) | `AA 01 00 01 04 03 01 00 1E 2E` | 0x01–0x0F |

La réponse à un GET porte la commande `0x03` (`CMD_SET_NO_RESP`), avec en tête de données le mot d'offset.
Le PDF du protocole (« MPPT Solar Controller communication protocol », février 2018) n'est pas redistribué ici ;
il est publié dans le dépôt de skagmo : <https://github.com/skagmo/esmart_mppt/files/3401128/esmart3-serial-comm.pdf>.
Le code de référence est `components/esmart3/esmart3_protocol.h`.

## Tests

```sh
make -C tests test
```

Deux suites, compilées avec `g++` sous ASan/UBSan, sans ESPHome ni matériel :

* `test_protocol` : les requêtes du PDF et de E-t0m octet pour octet, la réponse temps réel du PDF
  (checksum `0x90`), les températures négatives, l'exemple 32 bits, un flux bruité (parasites, mauvais
  checksum, écho, trame coupée).
* `test_hub` : le composant complet (`esmart3.cpp`) contre de **fausses** en-têtes ESPHome (`tests/stubs`,
  écrites d'après l'API telle que je la connais, donc pas une preuve de compatibilité) : cycle normal,
  écho ignoré, 6 timeouts → `NaN` puis reprise, repli sur 24 octets après 3 NACK, abandon d'une lecture
  optionnelle refusée, interrupteur (écriture, ACK, NACK, timeout), tampon partiel.

## Dépannage

| Symptôme | Piste |
|---|---|
| `No response to request for item 0` | câblage A/B (essayez de les inverser), GND, module sans direction automatique, Cloud Box encore branché |
| Valeurs de températures absurdes | sonde externe absente : la valeur renvoyée par le régulateur dépend du firmware |
| Énergies énormes | `u32_word_order: low_first` |
| Rien ne se passe | `logger: level: VERY_VERBOSE` : les trames TX/RX sont affichées en hexadécimal |

## Sources

* [esmart3-serial-comm.pdf](https://github.com/skagmo/esmart_mppt/files/3401128/esmart3-serial-comm.pdf) — protocole constructeur (février 2018)
* skagmo.com — [eSmart3 MPPT PV charger review](https://skagmo.com/page.php?p=documents%2F04_esmart3_review) (brochage, 9600 8N1, mesures)
* [E-t0m/esmart_mppt](https://github.com/E-t0m/esmart_mppt) — trames `REQUEST_MSG0`, `LOAD_ON`, `LOAD_OFF`
* [ssieb/esphome_components](https://github.com/ssieb/esphome_components) — composant `esmart3` d'origine
* [joba-1/Joba_ESmart3](https://github.com/joba-1/Joba_ESmart3) — lib Arduino complète (GPL v2) : masques de défauts,
  `setLoad` (5117/5118 à l'offset 1 de l'item 4), `getLoad` (offset 0x0F), délai entre commandes de 12 ms
