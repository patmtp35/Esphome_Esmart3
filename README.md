# Esphome_Esmart3

Récupération des informations du ESMART3 Via Esphome

Recueil d'informations récupérés sur le net afin de pouvoir connecter esphome et le Esmart3

Basé sur les travaux de Skamgo, Ssieb et Mtkodat.

L'alimentation du esmart fournis du 5v suffisant pour alimenter ESP. 

Attention toutefois sur les paires utilisés, la couleur pour faire les ponts VERT ou Marron : VCC et GND esphome <=> RS485

Testé sur ESP32 Wroom

Connecxion coté ESP

RX TX +5V GND

Connexion coté ESMART3:

7,8 => vcc
A ==>A
B => B
5,6 => GND


et un module RS485 
![image](https://github.com/patmtp35/Esphome_Esmart3/assets/6410695/02eda12e-bea1-469f-8acd-4a2830d47e83)

