/*
  Caixa para servidor do monitor de deposito ESP8266
  Projeto parametrico para OpenSCAD.

  Pecas:
  - base();
  - tampa();

  Para exportar:
  1. Abra este arquivo no OpenSCAD.
  2. Em "peca", escolha "base" ou "tampa".
  3. Pressione F6.
  4. File > Export > Export as STL.
*/

// ================= SELECAO DA PECA =================
peca = "montagem"; // "base", "tampa" ou "montagem"

// ================= MEDIDAS GERAIS =================
caixa_x = 150;
caixa_y = 105;
caixa_z = 42;
parede = 2.4;
raio_canto = 6;
folga_tampa = 0.35;

// ================= PARAFUSOS =================
diametro_parafuso = 3.2;      // M3 folgado
diametro_cabeca = 6.5;
altura_poste = 13;
diametro_poste = 9;
recuo_poste = 10;

// ================= COMPONENTES =================
// Ajuste estas medidas se seus modulos forem diferentes.
nodemcu_x = 58;
nodemcu_y = 31;
relay_x = 51;
relay_y = 38;
jsn_board_x = 42;
jsn_board_y = 30;
oled_janela_x = 27;
oled_janela_y = 15;

// Posicoes internas dos componentes na base.
nodemcu_pos = [18, 18, parede];
relay_pos   = [86, 16, parede];
jsn_pos     = [22, 62, parede];

// Janela OLED na tampa.
oled_centro = [caixa_x / 2, 36];

// Passagens de cabos.
diametro_cabo_sensor = 8;
diametro_cabo_alim = 7;
diametro_cabo_rele = 9;

// ================= QUALIDADE =================
$fn = 48;

// ================= UTILITARIOS =================
module rounded_box_2d(x, y, r) {
  hull() {
    translate([r, r]) circle(r = r);
    translate([x - r, r]) circle(r = r);
    translate([x - r, y - r]) circle(r = r);
    translate([r, y - r]) circle(r = r);
  }
}

module caixa_externa(x, y, z, r) {
  linear_extrude(z)
    rounded_box_2d(x, y, r);
}

module cavidade_interna() {
  translate([parede, parede, parede])
    caixa_externa(caixa_x - 2 * parede, caixa_y - 2 * parede, caixa_z, max(raio_canto - parede, 1));
}

module furo_cilindrico_x(d, comprimento) {
  rotate([0, 90, 0])
    cylinder(d = d, h = comprimento, center = true);
}

module furo_cilindrico_y(d, comprimento) {
  rotate([90, 0, 0])
    cylinder(d = d, h = comprimento, center = true);
}

module poste_parafuso(x, y, h = altura_poste) {
  translate([x, y, parede])
    difference() {
      cylinder(d = diametro_poste, h = h);
      translate([0, 0, -0.2])
        cylinder(d = diametro_parafuso, h = h + 0.4);
    }
}

module suporte_placa(pos, sx, sy, nome = "") {
  px = pos[0];
  py = pos[1];
  pz = pos[2];

  // Sombra do componente apenas para referencia visual no modo montagem.
  if (peca == "montagem") {
    translate([px, py, pz + 8])
      color([0.1, 0.55, 0.25, 0.45])
        cube([sx, sy, 1.5]);
    translate([px + sx / 2, py + sy / 2, pz + 11])
      color("black")
        linear_extrude(0.6)
          text(nome, size = 5, halign = "center", valign = "center");
  }

  // Quatro apoios baixos. Use cola quente, fita dupla face ou parafuso pequeno.
  for (ix = [0, 1]) {
    for (iy = [0, 1]) {
      translate([px + 5 + ix * (sx - 10), py + 5 + iy * (sy - 10), parede])
        cylinder(d = 5.5, h = 5);
    }
  }
}

module rasgos_respiro(x0, y0, n = 6) {
  for (i = [0 : n - 1]) {
    translate([x0, y0 + i * 7, -0.1])
      cube([34, 2.2, parede + 0.4]);
  }
}

// ================= BASE =================
module base() {
  difference() {
    caixa_externa(caixa_x, caixa_y, caixa_z, raio_canto);
    cavidade_interna();

    // USB do NodeMCU, lateral esquerda.
    translate([-0.1, nodemcu_pos[1] + 11, 16])
      cube([parede + 0.4, 13, 10]);

    // Cabo do sensor JSN-SR04T, lateral esquerda.
    translate([0, jsn_pos[1] + jsn_board_y / 2, 24])
      furo_cilindrico_x(diametro_cabo_sensor, parede + 1);

    // Saida para fios do rele/carga, lateral direita.
    translate([caixa_x, relay_pos[1] + relay_y / 2, 24])
      furo_cilindrico_x(diametro_cabo_rele, parede + 1);

    // Entrada de alimentacao auxiliar, parte inferior.
    translate([caixa_x / 2, 0, 24])
      furo_cilindrico_y(diametro_cabo_alim, parede + 1);
  }

  // Postes dos cantos.
  poste_parafuso(recuo_poste, recuo_poste);
  poste_parafuso(caixa_x - recuo_poste, recuo_poste);
  poste_parafuso(caixa_x - recuo_poste, caixa_y - recuo_poste);
  poste_parafuso(recuo_poste, caixa_y - recuo_poste);

  // Suportes internos.
  suporte_placa(nodemcu_pos, nodemcu_x, nodemcu_y, "ESP8266");
  suporte_placa(relay_pos, relay_x, relay_y, "RELE");
  suporte_placa(jsn_pos, jsn_board_x, jsn_board_y, "JSN");
}

// ================= TAMPA =================
module tampa() {
  tampa_z = 4;
  aba_z = 6;
  aba_x = caixa_x - 2 * parede - 2 * folga_tampa;
  aba_y = caixa_y - 2 * parede - 2 * folga_tampa;

  difference() {
    union() {
      caixa_externa(caixa_x, caixa_y, tampa_z, raio_canto);

      translate([parede + folga_tampa, parede + folga_tampa, -aba_z + 0.2])
        difference() {
          caixa_externa(aba_x, aba_y, aba_z, max(raio_canto - parede, 1));
          translate([parede, parede, -0.1])
            caixa_externa(aba_x - 2 * parede, aba_y - 2 * parede, aba_z + 0.4, max(raio_canto - 2 * parede, 1));
        }
    }

    // Janela do OLED.
    translate([oled_centro[0] - oled_janela_x / 2, oled_centro[1] - oled_janela_y / 2, -0.2])
      cube([oled_janela_x, oled_janela_y, tampa_z + 0.6]);

    // Furos dos parafusos da tampa.
    for (p = [
      [recuo_poste, recuo_poste],
      [caixa_x - recuo_poste, recuo_poste],
      [caixa_x - recuo_poste, caixa_y - recuo_poste],
      [recuo_poste, caixa_y - recuo_poste]
    ]) {
      translate([p[0], p[1], -0.2])
        cylinder(d = diametro_parafuso, h = tampa_z + 1);
      translate([p[0], p[1], tampa_z - 1.7])
        cylinder(d = diametro_cabeca, h = 2.2);
    }

    // Respiro superior para reduzir umidade/condensacao interna.
    rasgos_respiro(20, 70, 4);
    rasgos_respiro(96, 70, 4);
  }

  // Moldura fina em volta do OLED.
  translate([oled_centro[0] - oled_janela_x / 2 - 3, oled_centro[1] - oled_janela_y / 2 - 3, tampa_z])
    difference() {
      cube([oled_janela_x + 6, oled_janela_y + 6, 1.2]);
      translate([3, 3, -0.1])
        cube([oled_janela_x, oled_janela_y, 1.5]);
    }
}

// ================= MONTAGEM/RENDER =================
if (peca == "base") {
  base();
} else if (peca == "tampa") {
  tampa();
} else {
  base();
  translate([0, 0, caixa_z + 10])
    tampa();
}
