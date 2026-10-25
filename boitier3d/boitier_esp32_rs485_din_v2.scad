// =====================================================================
// Boîtier rail DIN 4 modules : Freenove ESP32-WROOM + XY-485 (eSmart3)
// Imprimante : Ender 3 V3 KE  |  Matériau : PETG  |  Cotes en mm
// 3 pièces : base (socle), cover (capot), clip (clip DIN séparé)
// Rendu : part = "print" (les 3 pièces à plat), ou "base" / "cover" / "clip"
// =====================================================================

part = "print";   // "print" | "base" | "cover" | "clip"
$fn = 40;

// ---------- Boîtier ----------
W    = 70;     // largeur = 4 modules DIN (4 x 17.5)
H    = 90;     // hauteur standard DIN
wall = 2.4;    // épaisseur des parois
pz   = 4;      // épaisseur du fond
zc   = 20;     // profondeur utile de la cavité (au-dessus du fond)
cov  = 2.4;    // épaisseur du capot
tol  = 0.2;    // jeu d'assemblage par face
lip_h = 2.5;   // hauteur des ergots de centrage du capot
boss_d = 7;    // diamètre des plots de vis du capot
boss_pilot = 2.6;               // avant-trou vis M3 autotaraudeuse
bosses = [[6, 84], [64, 84], [33, 7]];   // positions des 3 vis du capot
engrave_txt  = "Esmart3-Esp32";   // texte gravé sur le capot
engrave_size = 6.5;               // hauteur des lettres (largeur totale ~50 mm)
engrave_d    = 0.6;               // profondeur de gravure (3 couches de 0,2 mm)

// ---------- Freenove ESP32-WROOM (mesures utilisateur) ----------
esp_l   = 57.15;   // longueur avec antenne
esp_w   = 28.15;   // largeur
esp_pin = 8.45;    // hauteur des broches/embases sous le PCB
esp_pcb = 1.6;     // épaisseur PCB (hypothèse)
esp_shield_h = 3.2;// hauteur du blindage WROOM au-dessus du PCB (hypothèse)
esp_gap = 0.55;    // jeu sous les broches
ridge_h = esp_pin + esp_gap;               // hauteur de la nervure support
esp_x0  = W - wall - esp_w - 0.5;          // contre la paroi droite
esp_y0  = 6;                               // bord USB-C côté bas
peg_x = 13.3;  peg_y = 41.5;               // pion de maintien sur le blindage (estimé sur photo)
peg_gap = 0.2;

// ---------- XY-485 (mesures utilisateur) ----------
rs_l  = 52.25;     // PCB sans connecteur JST
rs_w  = 22.65;
rs_jst = 2.25;     // dépassement du JST (54.50 - 52.25)
rs_hx_bot = 19.6;  // entraxe trous en largeur, côté bornier (à confirmer)
rs_hx_top = 19.1;  // entraxe trous en largeur, côté TTL/JST (mesuré)
rs_hy = 48.2;      // entraxe trous (longueur)
rs_stand = 4;      // hauteur des entretoises (bornier 11.8 -> reste ~4 mm sous le capot)
rs_x0 = 4;  rs_y0 = 7;   // bornier vers le bas, JST vers le haut
// positions des 4 trous du XY-485 : [x, y]
rs_pts = [for (r = [[0, rs_hx_bot], [rs_hy, rs_hx_top]])
            for (dx = [0, r[1]])
              [rs_x0 + (rs_w - r[1])/2 + dx, rs_y0 + (rs_l - rs_hy)/2 + r[0]]];

// ---------- Clip DIN ----------
tc   = 4;       // épaisseur plaque clip
rail = 35;      // largeur rail
rclr = 0.4;     // jeu rail
stem = 2.4;     // épaisseur des montants
lipL = 2.6;     // longueur des becs (recouvrement rail 2.2)
brim_t = 1.0;   // épaisseur des bords du rail
lip_gap = 0.5;  // jeu entre bord du rail et bec
lip_t = 1.4;    // épaisseur des becs
cw   = 44;      // largeur du clip
yc   = 20.5;    // centre du rail dans le repère du clip
slot_w = 1.8;   // fente de la languette flexible
arm_x0 = 8;     // début de la languette flexible
zl = tc + brim_t + lip_gap;     // dessous du bec
zt = zl + lip_t;                // dessus du bec
y_bot_in = yc - rail/2 - rclr;  // face intérieure du montant bas
y_top_in = yc + rail/2 + rclr;  // face intérieure du montant haut
clip_screws_y = [34, 56];       // dans le repère de la base (Y)
clip_y0_in_base = (H - 41) / 2; // décalage Y du clip dans la base

// =====================================================================
// BASE
// =====================================================================
module notch(x0, x1, zb) {
    translate([x0, -1, zb]) cube([x1 - x0, wall + 2, pz + zc - zb + 1]);
}

module base() {
    difference() {
        union() {
            // fond + parois
            difference() {
                cube([W, H, pz + zc]);
                translate([wall, wall, pz]) cube([W - 2*wall, H - 2*wall, zc + 1]);
            }
            // plots vis capot
            for (p = bosses)
                translate([p[0], p[1], pz - 0.01]) cylinder(d = boss_d, h = zc + 0.01);

            // entretoises XY-485
            for (p = rs_pts)
                translate([p[0], p[1], pz - 0.01])
                    cylinder(d = 5, h = rs_stand + 0.01);

            // nervure centrale sous le Freenove (les broches pendent de chaque côté)
            translate([esp_x0 + 5, esp_y0 + 4, pz - 0.01])
                cube([esp_w - 10, esp_l - 12, ridge_h + 0.01]);

            // guides latéraux gauche (la paroi droite sert de guide)
            for (s = [[8, 12], [33, 11]])
                translate([esp_x0 - tol - 1.6, esp_y0 + s[0], pz - 0.01])
                    cube([1.6, s[1], ridge_h + esp_pcb + 0.01]);

            // butée côté antenne (rehaussée de 2 mm)
            translate([esp_x0 + 8, esp_y0 + esp_l + tol, pz - 0.01])
                cube([12, 2, ridge_h + esp_pcb + 2 + 0.01]);

            // butées côté USB-C (de part et d'autre du connecteur)
            for (xo = [0.5, esp_w - 4.5])
                translate([esp_x0 + xo, esp_y0 - tol - 2, pz - 0.01])
                    cube([4, 2, ridge_h + esp_pcb + 0.01]);
        }

        // avant-trous vis capot
        for (p = bosses)
            translate([p[0], p[1], pz + 2]) cylinder(d = boss_pilot, h = zc + 1);

        // avant-trous vis M2 du XY-485
        for (p = rs_pts)
            translate([p[0], p[1], pz + rs_stand - 5.9])
                cylinder(d = 1.7, h = 6);

        // passages de câbles dans la paroi basse (ouverts vers le capot)
        notch(esp_x0 + 5, W - wall - 0.6, pz + 7.5);   // USB-C + alim 5 V
        notch(rs_x0 + 1, rs_x0 + rs_w - 1, 9.5);       // câble RS485 vers eSmart3

        // vis du clip DIN : trou ø3.4 + logement d'écrou M3 côté cavité
        for (y = clip_screws_y)
            translate([W/2, y, 0]) {
                translate([0, 0, -1]) cylinder(d = 3.4, h = pz + 2);
                translate([0, 0, pz - 2.6]) cylinder(d = 6.7, h = 2.7, $fn = 6);
            }
    }
}

// =====================================================================
// CAPOT (défini en position assemblée : face intérieure à z = pz + zc)
// =====================================================================
module cover() {
    zt0 = pz + zc;
    peg_len = zc - ridge_h - esp_pcb - esp_shield_h - peg_gap;
    difference() {
        union() {
            translate([0, 0, zt0]) cube([W, H, cov]);
            // ergots de centrage (côtés + haut, pas en bas pour les passages de câbles)
            translate([wall + tol, 15, zt0 - lip_h]) cube([1.2, 55, lip_h + 0.01]);
            translate([W - wall - tol - 1.2, 15, zt0 - lip_h]) cube([1.2, 55, lip_h + 0.01]);
            translate([12, H - wall - tol - 1.2, zt0 - lip_h]) cube([W - 24, 1.2, lip_h + 0.01]);
            // pion de maintien du Freenove, posé sur le blindage du WROOM
            translate([esp_x0 + peg_x - 4, esp_y0 + peg_y - 4, zt0 - peg_len])
                cube([8, 8, peg_len + 0.01]);
        }
        // trous de vis M3 fraisés (face extérieure)
        for (p = bosses)
            translate([p[0], p[1], zt0 - 0.01]) {
                cylinder(d = 3.4, h = cov + 0.02);
                translate([0, 0, cov - 1.5]) cylinder(d1 = 3.4, d2 = 6.4, h = 1.52);
            }
        // gravure sur la face extérieure (texte lisible face au boîtier)
        translate([W/2, H/2, zt0 + cov - engrave_d])
            linear_extrude(engrave_d + 0.1)
                text(engrave_txt, size = engrave_size, font = "Liberation Sans:style=Bold",
                     halign = "center", valign = "center");
    }
}

// capot retourné pour l'impression (face extérieure sur le plateau)
module cover_print() {
    translate([W, 0, pz + zc + cov]) rotate([0, 180, 0]) cover();
}

// =====================================================================
// CLIP DIN (repère d'impression : face contre la base sur le plateau)
// =====================================================================
module lip(yin, dir, x0, len) {
    pts = [[yin - dir*0.2, zl],
           [yin + dir*lipL, zl],
           [yin + dir*lipL, zl + 0.4],
           [yin, zt],
           [yin - dir*0.2, zt]];
    translate([x0, 0, 0]) rotate([90, 0, 90]) linear_extrude(len) polygon(pts);
}

module clip() {
    y_body0 = y_bot_in + slot_w;               // début de la plaque principale
    y_body1 = y_top_in + stem + 0.2;           // fin de la plaque principale
    difference() {
        union() {
            // plaque principale
            translate([0, y_body0, 0]) cube([cw, y_body1 - y_body0, tc]);
            // base de la languette flexible (côté gauche, reliée à la plaque)
            translate([0, y_bot_in - stem, 0]) cube([arm_x0, stem + slot_w + 0.01, tc]);
            // languette flexible (fléchit dans le plan, vers le bas)
            translate([arm_x0 - 0.01, y_bot_in - stem, 0]) cube([cw - arm_x0 + 0.01, stem, tc]);
            // montant + bec bas (sur la languette)
            translate([24, y_bot_in - stem, tc - 0.01]) cube([12, stem, zt - tc + 0.01]);
            lip(y_bot_in, 1, 24, 12);
            // montant + bec haut (fixe)
            translate([6, y_top_in, tc - 0.01]) cube([32, stem, zt - tc + 0.01]);
            lip(y_top_in, -1, 6, 32);
            // languette de manoeuvre jusqu'au bord bas du boîtier (tournevis)
            translate([cw - 8, -(H - 41)/2 + 0.2, 0])
                cube([8, y_bot_in + (H - 41)/2 - 0.2 + 0.01, tc]);
        }
        // trou de tournevis dans la languette de manoeuvre
        translate([cw - 4, -(H - 41)/2 + 5.5, -1]) cylinder(d = 4, h = tc + 2);
        // vis M3 fraisées (côté rail)
        for (y = clip_screws_y)
            translate([cw/2, y - clip_y0_in_base, 0]) {
                translate([0, 0, -1]) cylinder(d = 3.4, h = tc + 2);
                translate([0, 0, tc - 1.7]) cylinder(d1 = 3.4, d2 = 6.6, h = 1.71);
            }
    }
}

// =====================================================================
if (part == "base") base();
else if (part == "cover") cover_print();
else if (part == "clip") clip();
else {
    base();
    translate([W + 10, 0, 0]) cover_print();
    translate([0, H + 10 + (H - 41)/2, 0]) clip();
}
