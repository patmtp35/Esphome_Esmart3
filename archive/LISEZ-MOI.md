# Archive — version de juin 2024

Contenu d'origine du dépôt, déplacé sans modification (`README.md`, `esphome/`, `img/`).
Le même état est repérable par le tag `archive-2024-06`.

Notes de revue ajoutées lors de l'archivage :

* La configuration ESP32 (`esphome/Esp32/esmart3V2.yaml`) est notée « testé OK » ; celle du D1 Mini
  (`esphome/D1-Mini/D1_mini.yaml`) est notée non fonctionnelle.
* `D1_mini.yaml` contient `gateway_ip:!secret ip_gateway` (sans espace après les deux-points) : ce YAML est
  invalide tel quel, ce qui peut suffire à expliquer l'échec de ce fichier.
* Les capteurs déclarent `state_class: total_increasing` alors qu'il s'agit de mesures instantanées
  (tensions, courants, puissances, températures) : `measurement` est la valeur correcte pour Home Assistant.
* Le composant de ssieb lit les températures en non signé : une valeur négative devient ~65534.

La nouvelle version (dossier `components/`) corrige ces points.
