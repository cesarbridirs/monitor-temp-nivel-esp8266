#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <NeoPixelBus.h>
#include "config.h"

// ================= CONFIGURACAO =================
const unsigned long INTERVALO_ATUALIZACAO = 60000;

// ================= LEDS WS2812B =================
#define LED_PIN D5
#define NUM_LEDS 7

NeoPixelBus<NeoGrbFeature, NeoEsp8266BitBang800KbpsMethod> leds(NUM_LEDS, LED_PIN);

// ================= DISPLAY =================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDR 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ================= DADOS RECEBIDOS =================
float nivel = 0;
float porcentagem = 0;
float temperatura = 0;
float umidade = 0;
float tempBoiler = 0;
bool releEstado = false;
bool dadosRecebidos = false;
unsigned long ultimaBusca = 0;

RgbColor corNivel(float pct) {
  pct = constrain(pct, 0.0f, 100.0f);

  if (pct < 30.0f) {
    return RgbColor(120, 0, 0);
  }
  if (pct < 70.0f) {
    return RgbColor(120, 70, 0);
  }
  return RgbColor(0, 120, 0);
}

void atualizarLEDs() {
  leds.ClearTo(RgbColor(0, 0, 0));

  if (!dadosRecebidos) {
    leds.SetPixelColor(0, RgbColor(0, 0, 80));
    leds.Show();
    return;
  }

  int ledsAcesos = map((int)constrain(porcentagem, 0.0f, 100.0f), 0, 100, 0, NUM_LEDS);
  ledsAcesos = constrain(ledsAcesos, 0, NUM_LEDS);

  RgbColor cor = corNivel(porcentagem);
  for (int i = 0; i < ledsAcesos; i++) {
    leds.SetPixelColor(i, cor);
  }

  leds.Show();
}

void testarLEDs() {
  leds.ClearTo(RgbColor(0, 0, 0));
  leds.Show();
  delay(100);

  for (int i = 0; i < NUM_LEDS; i++) {
    leds.SetPixelColor(i, RgbColor(80, 0, 0));
    leds.Show();
    delay(80);
    yield();
  }

  for (int i = 0; i < NUM_LEDS; i++) {
    leds.SetPixelColor(i, RgbColor(0, 80, 0));
    leds.Show();
    delay(80);
    yield();
  }

  for (int i = 0; i < NUM_LEDS; i++) {
    leds.SetPixelColor(i, RgbColor(0, 0, 80));
    leds.Show();
    delay(80);
    yield();
  }

  leds.ClearTo(RgbColor(0, 0, 0));
  leds.Show();
}

void mostrarMensagem(const char* linha1, const char* linha2 = "") {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 12);
  display.println(linha1);
  display.setCursor(0, 28);
  display.println(linha2);
  display.display();
}

void atualizarDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(2, 0);
  display.print("IP:");
  display.print(WiFi.localIP());

  display.setCursor(2, 8);
  if (WiFi.status() == WL_CONNECTED) {
    display.printf("RSSI:%3d dBm", WiFi.RSSI());
  } else {
    display.print("SEM CONEXAO WIFI");
  }

  display.setCursor(2, 16);
  if (dadosRecebidos) {
    display.printf("NIVEL:%0.fcm %0.f%%", nivel, porcentagem);
  } else {
    display.print("NIVEL:--cm --%");
  }

  const int barraY = 24;
  display.drawRect(2, barraY, 124, 10, SSD1306_WHITE);
  const int largura = map((int)constrain(porcentagem, 0, 100), 0, 100, 0, 122);
  display.fillRect(3, barraY + 1, largura, 8, SSD1306_WHITE);

  display.setCursor(2, 39);
  display.printf("TEMP.BOILER:%0.1fC", tempBoiler);

  display.setCursor(2, 48);
  display.printf("TEMP.AMBI:%0.1fC", temperatura);

  display.setCursor(2, 57);
  display.printf("HUMIDADE:%0.f%%", umidade);

  display.display();
}

bool buscarDadosDoServidor() {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  WiFiClient client;
  HTTPClient http;
  String url = "http://" + String(IP_SERVIDOR) + "/data";

  Serial.println("Buscando: " + url);
  if (!http.begin(client, url)) {
    return false;
  }

  int codigoHttp = http.GET();
  if (codigoHttp != HTTP_CODE_OK) {
    Serial.printf("Servidor respondeu HTTP %d\n", codigoHttp);
    http.end();
    return false;
  }

  String resposta = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError erro = deserializeJson(doc, resposta);
  if (erro) {
    Serial.print("JSON invalido: ");
    Serial.println(erro.c_str());
    return false;
  }

  nivel = doc["nivel_cm"] | 0.0f;
  porcentagem = doc["nivel_porcent"] | 0.0f;
  temperatura = doc["temp"] | 0.0f;
  umidade = doc["hum"] | 0.0f;
  tempBoiler = doc["temp_boiler"] | 0.0f;
  releEstado = doc["rele_on"] | false;
  dadosRecebidos = true;
  atualizarLEDs();

  Serial.printf("Nivel: %.1f cm (%.1f%%) | Ambiente: %.1f C | Umidade: %.0f%% | Boiler: %.1f C | Rele: %s\n",
                nivel, porcentagem, temperatura, umidade, tempBoiler, releEstado ? "ON" : "OFF");
  return true;
}

void conectarWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);
  mostrarMensagem("CONECTANDO WIFI...");

  while (WiFi.status() != WL_CONNECTED) {
    delay(400);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("Monitor conectado. IP: ");
  Serial.println(WiFi.localIP());
}

void setup() {
  Serial.begin(115200);
  Wire.begin(D2, D1);

  leds.Begin();
  leds.ClearTo(RgbColor(0, 0, 0));
  leds.Show();
  testarLEDs();

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED nao encontrado");
    while (true) {
      yield();
    }
  }

  conectarWiFi();
  atualizarDisplay();
  atualizarLEDs();
}

void loop() {
  if (millis() - ultimaBusca >= INTERVALO_ATUALIZACAO || ultimaBusca == 0) {
    if (!buscarDadosDoServidor()) {
      Serial.println("Nao foi possivel atualizar os dados do servidor.");
    }
    atualizarDisplay();
    atualizarLEDs();
    ultimaBusca = millis();
  }

  yield();
}
