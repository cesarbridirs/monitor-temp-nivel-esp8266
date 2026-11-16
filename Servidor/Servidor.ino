#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <EEPROM.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ArduinoJson.h>
#include <NeoPixelBus.h>  // LEDs WS2812B
#include <math.h>
#include "config.h"

// ================= PINOS =================
#define TRIG_PIN     D5
#define ECHO_PIN     D6
#define DHT_PIN      D4
#define RELE_PIN     D3
#define DS18B20_PIN  D7        // Mesmo pino do exemplo anterior para DS18B20
#define LED_PIN      D8        // Ajuste se precisar para a sua fita
#define NUM_LEDS     7         // 7 LEDs endereçáveis

// Deixe 0 em operacao normal para reduzir carga na serial.
#define DEBUG_SERIAL 0

#if DEBUG_SERIAL
  #define LOG_PRINT(x) Serial.print(x)
  #define LOG_PRINTLN(x) Serial.println(x)
  #define LOG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
  #define LOG_PRINT(x)
  #define LOG_PRINTLN(x)
  #define LOG_PRINTF(...)
#endif

// ================= GOOGLE SHEETS =================
unsigned long ultimo_log_sheets = 0;
const unsigned long INTERVALO_SHEETS = 300000; // 5 minutos

// ================= OLED =================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDR 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ================= LED (NeoPixelBus) =================
NeoPixelBus<NeoGrbFeature, NeoEsp8266BitBang800KbpsMethod> leds(NUM_LEDS, LED_PIN);

// ================= OBJETOS =================
ESP8266WebServer server(80);
DHT dht(DHT_PIN, DHT22);
OneWire oneWire(DS18B20_PIN);
DallasTemperature ds18b20(&oneWire);

// ================= WIFI =================

// ================= VARIÁVEIS =================
const float ALTURA_RESERVATORIO = 90.0;
const float ALTURA_MAX_AGUA     = 70.0;
const float ESPACO_LIVRE        = 30.0;

float distancia = 0; 
float nivel = 0; 
float porcentagem = 0;
float temperatura = 0;
float umidade = 0;
float tempBoiler = 0;
float tempBoilerBruta = 0;
bool  rele_estado = false;
unsigned long ultimo_update = 0;
unsigned int falhas_distancia = 0;
unsigned int falhas_dht = 0;
unsigned int falhas_ds18b20 = 0;
unsigned int leituras_boiler_suspeitas = 0;
float candidato_boiler = 0;
bool boiler_suspeito = false;
bool distancia_valida = false;
bool dht_valido = false;
bool ds18b20_valido = false;
unsigned long ultima_tentativa_wifi = 0;
unsigned long inicio_suspeita_boiler = 0;

const unsigned long INTERVALO_UPDATE = 3000;
const unsigned long WIFI_RETRY_MS = 30000;
const unsigned long WATCHDOG_APP_MS = 120000;

const float TEMP_AMB_MIN = 5.0f;
const float TEMP_AMB_MAX = 45.0f;
const float UMIDADE_MIN = 5.0f;
const float UMIDADE_MAX = 100.0f;
const float BOILER_MIN = 0.0f;
const float BOILER_MAX = 90.0f;
const float BOILER_SALTO_MAX = 3.0f;
const float BOILER_CANDIDATO_TOLERANCIA = 0.8f;
const unsigned long BOILER_CONFIRMACAO_MS = 900000; // 15 minutos
const float TEMP_BOILER_OFFSET = 0.0f; // ajuste somente apos medir a diferenca com termometro confiavel

struct DadosRTC {
  uint32_t magic;
  uint32_t crc;
  float distancia_salva;
  float nivel_salvo;
  float porcentagem_salva;
  float temperatura_salva;
  float umidade_salva;
  float temp_boiler_salva;
  uint8_t flags;
  uint8_t reservado[3];
};

const uint32_t RTC_MAGIC = 0xD0E90730;
const int EEPROM_SIZE = sizeof(DadosRTC);
const int EEPROM_ADDR = 0;
const uint8_t RTC_FLAG_NIVEL = 0x01;
const uint8_t RTC_FLAG_DHT = 0x02;
const uint8_t RTC_FLAG_BOILER = 0x04;

uint32_t calcularCRC(const DadosRTC &dados) {
  const uint8_t *bytes = (const uint8_t *)&dados;
  uint32_t crc = 2166136261UL;

  for (size_t i = 8; i < sizeof(DadosRTC); i++) {
    crc ^= bytes[i];
    crc *= 16777619UL;
  }

  return crc;
}

DadosRTC montarDadosSalvos() {
  DadosRTC dados;
  dados.magic = RTC_MAGIC;
  dados.distancia_salva = distancia;
  dados.nivel_salvo = nivel;
  dados.porcentagem_salva = porcentagem;
  dados.temperatura_salva = temperatura;
  dados.umidade_salva = umidade;
  dados.temp_boiler_salva = tempBoiler;
  dados.flags = 0;
  dados.reservado[0] = 0;
  dados.reservado[1] = 0;
  dados.reservado[2] = 0;

  if (distancia_valida) dados.flags |= RTC_FLAG_NIVEL;
  if (dht_valido) dados.flags |= RTC_FLAG_DHT;
  if (ds18b20_valido) dados.flags |= RTC_FLAG_BOILER;

  dados.crc = calcularCRC(dados);
  return dados;
}

void salvarRTC() {
  DadosRTC dados = montarDadosSalvos();
  ESP.rtcUserMemoryWrite(0, (uint32_t *)&dados, sizeof(DadosRTC));
}

bool aplicarDadosSalvos(const DadosRTC &dados) {
  if (dados.magic != RTC_MAGIC) return false;
  if (dados.crc != calcularCRC(dados)) return false;
  if ((dados.flags & (RTC_FLAG_NIVEL | RTC_FLAG_DHT | RTC_FLAG_BOILER)) == 0) return false;

  if (dados.flags & RTC_FLAG_NIVEL) {
    distancia = dados.distancia_salva;
    nivel = dados.nivel_salvo;
    porcentagem = dados.porcentagem_salva;
    distancia_valida = true;
  }
  if (dados.flags & RTC_FLAG_DHT) {
    temperatura = dados.temperatura_salva;
    umidade = dados.umidade_salva;
    dht_valido = true;
  }
  if (dados.flags & RTC_FLAG_BOILER) {
    tempBoiler = dados.temp_boiler_salva;
    candidato_boiler = tempBoiler;
    ds18b20_valido = true;
  }

  return true;
}

bool carregarRTC() {
  DadosRTC dados;
  if (!ESP.rtcUserMemoryRead(0, (uint32_t *)&dados, sizeof(DadosRTC))) return false;
  return aplicarDadosSalvos(dados);
}

void salvarEEPROM() {
  DadosRTC dados = montarDadosSalvos();
  EEPROM.put(EEPROM_ADDR, dados);
  EEPROM.commit();
}

bool carregarEEPROM() {
  DadosRTC dados;
  EEPROM.get(EEPROM_ADDR, dados);
  return aplicarDadosSalvos(dados);
}

void carregarUltimosValores() {
  if (carregarRTC()) {
    LOG_PRINTLN("Valores restaurados da RTC.");
    return;
  }

  if (carregarEEPROM()) {
    LOG_PRINTLN("Valores restaurados da EEPROM.");
  }
}

// ================= SENSORES =================
float medirDistanciaUnica() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duracao = pulseIn(ECHO_PIN, HIGH, 30000);
  yield();
  if (duracao <= 0) return NAN;
  return (duracao * 0.034f) / 2.0f;
}

float medirDistancia() {
  const byte AMOSTRAS = 5;
  float leituras[AMOSTRAS];
  byte validas = 0;

  for (byte i = 0; i < AMOSTRAS; i++) {
    float leitura = medirDistanciaUnica();
    if (!isnan(leitura) && leitura >= (ESPACO_LIVRE - 2.0f) && leitura <= (ALTURA_RESERVATORIO + 10.0f)) {
      leituras[validas++] = leitura;
    }
    delay(20);
    yield();
  }

  if (validas == 0) return NAN;

  for (byte i = 0; i < validas - 1; i++) {
    for (byte j = i + 1; j < validas; j++) {
      if (leituras[j] < leituras[i]) {
        float tmp = leituras[i];
        leituras[i] = leituras[j];
        leituras[j] = tmp;
      }
    }
  }

  return leituras[validas / 2];
}

bool leituraDHTValida(float temp, float hum) {
  if (isnan(temp) || isnan(hum)) return false;
  if (temp < TEMP_AMB_MIN || temp > TEMP_AMB_MAX) return false;
  if (hum < UMIDADE_MIN || hum > UMIDADE_MAX) return false;
  if (fabs(temp) < 0.1f || fabs(hum) < 0.1f) return false;

  if (dht_valido) {
    if (fabs(temp - temperatura) > 8.0f) return false;
    if (fabs(hum - umidade) > 35.0f) return false;
  }

  return true;
}

bool leituraDS18B20Valida(float temp) {
  return temp != DEVICE_DISCONNECTED_C && !isnan(temp) && temp >= BOILER_MIN && temp <= BOILER_MAX && fabs(temp - 85.0f) > 0.1f;
}

bool aceitarLeituraBoiler(float novaTemp) {
  if (!leituraDS18B20Valida(novaTemp)) return false;

  if (!ds18b20_valido) {
    tempBoiler = novaTemp;
    candidato_boiler = novaTemp;
    leituras_boiler_suspeitas = 0;
    boiler_suspeito = false;
    inicio_suspeita_boiler = 0;
    ds18b20_valido = true;
    return true;
  }

  if (fabs(novaTemp - tempBoiler) <= BOILER_SALTO_MAX) {
    tempBoiler = novaTemp;
    candidato_boiler = novaTemp;
    leituras_boiler_suspeitas = 0;
    boiler_suspeito = false;
    inicio_suspeita_boiler = 0;
    return true;
  }

  if (!boiler_suspeito || fabs(novaTemp - candidato_boiler) > BOILER_CANDIDATO_TOLERANCIA) {
    candidato_boiler = novaTemp;
    leituras_boiler_suspeitas = 1;
    inicio_suspeita_boiler = millis();
    boiler_suspeito = true;
  } else {
    leituras_boiler_suspeitas++;
  }

  if (millis() - inicio_suspeita_boiler >= BOILER_CONFIRMACAO_MS && leituras_boiler_suspeitas >= 10) {
    LOG_PRINTF("Boiler aceito apos 15 min de confirmacao: %.1f C\n", novaTemp);
    tempBoiler = novaTemp;
    leituras_boiler_suspeitas = 0;
    boiler_suspeito = false;
    inicio_suspeita_boiler = 0;
    return true;
  }

  LOG_PRINTF("Boiler suspeito mantido fora: %.1f C, atual %.1f C\n", novaTemp, tempBoiler);
  return false;
}

void lerSensores() {
  float novaDistancia = medirDistancia();
  if (!isnan(novaDistancia)) {
    if (!distancia_valida || fabs(novaDistancia - distancia) <= 20.0f || falhas_distancia >= 5) {
      distancia = novaDistancia;
      nivel = constrain(ALTURA_MAX_AGUA - (distancia - ESPACO_LIVRE), 0, ALTURA_MAX_AGUA);
      porcentagem = (nivel / ALTURA_MAX_AGUA) * 100.0;
      distancia_valida = true;
      falhas_distancia = 0;
    } else {
      falhas_distancia++;
      LOG_PRINTF("Nivel descartado: %.1f cm\n", novaDistancia);
    }
  } else {
    falhas_distancia++;
    LOG_PRINTLN("Falha nivel; mantendo ultimo valor valido.");
  }

  float novaUmidade = dht.readHumidity();
  delay(60);
  yield();
  float novaTemperatura = dht.readTemperature();
  if (leituraDHTValida(novaTemperatura, novaUmidade)) {
    temperatura = novaTemperatura;
    umidade = novaUmidade;
    dht_valido = true;
    falhas_dht = 0;
  } else {
    falhas_dht++;
    LOG_PRINTLN("Falha DHT22; mantendo ultimo valor valido.");
    if (falhas_dht >= 10) {
      dht.begin();
      falhas_dht = 0;
    }
  }

  ds18b20.requestTemperatures();
  float novaTempBoilerBruta = ds18b20.getTempCByIndex(0);
  if (leituraDS18B20Valida(novaTempBoilerBruta)) {
    tempBoilerBruta = novaTempBoilerBruta;
  }

  float novaTempBoiler = novaTempBoilerBruta + TEMP_BOILER_OFFSET;
  if (leituraDS18B20Valida(novaTempBoilerBruta) && aceitarLeituraBoiler(novaTempBoiler)) {
    falhas_ds18b20 = 0;
  } else {
    falhas_ds18b20++;
    LOG_PRINTLN("Falha DS18B20; mantendo ultimo valor valido.");
    if (falhas_ds18b20 >= 10) {
      ds18b20.begin();
      falhas_ds18b20 = 0;
    }
  }

  salvarRTC();
}

// ================= DISPLAY =================
void atualizarDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(2, 0);
  display.setTextSize(1);
  display.print("IP:");
  display.print(WiFi.localIP());

  display.setCursor(2, 8);  // RSSI logo abaixo do IP
  display.printf("RSSI:%3d dBm", WiFi.RSSI());

  display.setCursor(2, 16);
  display.setTextSize(1);
  display.printf("NIVEL:%0.fcm %0.f%%", nivel, porcentagem);  
  
  int barra_y = 24;  // Centro do espaço 16-39
  display.drawRect(2, barra_y, 124, 10, SSD1306_WHITE);
  int width = map(porcentagem, 0, 100, 0, 124);
  display.fillRect(2, barra_y, width, 10, SSD1306_WHITE);

  display.setCursor(2, 39);
  display.setTextSize(1);
  display.printf("TEMP.BOILER:%0.1fC", tempBoiler);

  display.setCursor(2, 48);
  display.setTextSize(1);
  display.printf("TEMP.AMBI:%0.1fC", temperatura);

  display.setCursor(2, 57);
  display.setTextSize(1);
  display.printf("HUMIDADE:%0.f%%", umidade);

  display.display();
}

// ================= LEDS =================
void atualizarLEDsAntigo() {
  leds.ClearTo(RgbColor(0, 0, 0));  // Apaga todos

  // Qual LED (0-6)
  int ledIndex = map(porcentagem, 0, 100, 0, NUM_LEDS - 1);
  ledIndex = constrain(ledIndex, 0, NUM_LEDS - 1);

  // Intensidade progressiva
  uint8_t intensidade = map(porcentagem, 0, 100, 0, 255);

  // De vermelho (baixo nível) para verde (alto nível)
  uint8_t r = map(ledIndex, 0, NUM_LEDS - 1, 255, 0);
  uint8_t g = map(ledIndex, 0, NUM_LEDS - 1, 0, 255);
  uint8_t b = 0;

  RgbColor cor(r * intensidade / 255, g * intensidade / 255, b);
  leds.SetPixelColor(ledIndex, cor);
  leds.Show();
}

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

  if (!distancia_valida) {
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

// ================= ENVIO GOOGLE SHEETS =================
void enviarGoogleSheets() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (!distancia_valida || !dht_valido || !ds18b20_valido) {
    LOG_PRINTLN("Sheets ignorado: aguardando sensores validos.");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(7000);
  HTTPClient http;
  http.setTimeout(7000);

  String url = "https://script.google.com/macros/s/" + String(SHEET_SCRIPT_ID) + "/exec";
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  if (http.begin(client, url)) {
    http.addHeader("Content-Type", "application/json");
    JsonDocument doc;
    doc["nivel_cm"]   = nivel;
    doc["nivel_pct"]  = porcentagem;
    doc["temp_amb"]   = temperatura;
    doc["umidade"]    = umidade;
    doc["temp_boiler"]= tempBoiler;
    doc["temp_boiler_raw"] = tempBoilerBruta;
    doc["temp_boiler_offset"] = TEMP_BOILER_OFFSET;
    doc["rele"]       = rele_estado ? "ON" : "OFF";

    String json;
    serializeJson(doc, json);

    int httpCode = http.POST(json);
    LOG_PRINTF("Sheets HTTP: %d\n", httpCode);
    http.end();
  }
}

// ================= WEB SERVER (JSON + HTML) =================
void handleData() {
  String json = "{";
  json += "\"nivel_cm\":"      + String(nivel, 1) + ",";
  json += "\"nivel_porcent\":" + String(porcentagem, 1) + ",";
  json += "\"temp\":"          + String(temperatura, 1) + ",";
  json += "\"hum\":"           + String(umidade, 1) + ",";
  json += "\"temp_boiler\":"   + String(tempBoiler, 1) + ",";
  json += "\"temp_boiler_raw\":" + String(tempBoilerBruta, 1) + ",";
  json += "\"temp_boiler_offset\":" + String(TEMP_BOILER_OFFSET, 1) + ",";
  json += "\"rele_on\":"       + String(rele_estado ? "true" : "false") + ",";
  json += "\"nivel_ok\":"      + String(distancia_valida ? "true" : "false") + ",";
  json += "\"dht_ok\":"        + String(dht_valido ? "true" : "false") + ",";
  json += "\"boiler_ok\":"     + String(ds18b20_valido ? "true" : "false") + ",";
  json += "\"boiler_suspeito\":" + String(boiler_suspeito ? "true" : "false");
  json += "}";
  server.send(200, "application/json", json);
}

void manterWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  if (millis() - ultima_tentativa_wifi > WIFI_RETRY_MS || ultima_tentativa_wifi == 0) {
    LOG_PRINTLN("WiFi desconectado. Reconectando...");
    WiFi.disconnect();
    WiFi.begin(ssid, password);
    ultima_tentativa_wifi = millis();
  }
}

void handleRele() {
  rele_estado = !rele_estado;
  digitalWrite(RELE_PIN, rele_estado ? LOW : HIGH);
  
  // Retorna JSON com status atualizado, sem redirect.
  String json = "{";
  json += "\"rele_on\":" + String(rele_estado ? "true" : "false");
  json += "}";
  server.send(200, "application/json", json);
}

void handleRoot() {
  static const char html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-br">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width">
  <title>Estação Reservatório</title>
  <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
  <style>
    body{font-family:Arial,Helvetica,sans-serif;background:#0f0f23;color:#fff;margin:0;padding:20px;}
    .container{max-width:900px;margin:0 auto;}
    .cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:20px;margin:20px 0;}
    .card{background:#1a1a2e;border-radius:12px;padding:16px;text-align:center;box-shadow:0 3px 10px rgba(0,0,0,0.4);}
    .card.nivel{background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);}
    .card.dht{background:linear-gradient(135deg,#f093fb 0%,#f5576c 100%);}
    .card.boiler{background:linear-gradient(135deg,#ff9800 0%,#ffb74d 100%);}
    .card.rele{background:linear-gradient(135deg,#ff6b6b 0%,#ffa726 100%);}
    .big{font-size:2.4em;font-weight:300;}
    .small{font-size:0.9em;color:#eee;}
    .btn-rele{width:100%;padding:12px;font-size:1.1em;border-radius:10px;border:none;cursor:pointer;margin-top:10px;transition:all .2s;}
    .btn-rele.on{background:#4caf50;color:#fff;}
    .btn-rele.off{background:#757575;color:#fff;}
    .btn-rele:active{transform:scale(0.97);}
    canvas{border-radius:10px;background:#1a1a2e;}
    h1{text-align:center;font-size:2em;margin-bottom:5px;}
    .sub{font-size:0.9em;text-align:center;color:#aaa;}
  </style>
</head>
<body>
  <div class="container">
    <h1>Estação Reservatório + Boiler + LED</h1>
    <div class="sub">Nível caixa d'água | DHT22 | DS18B20 Boiler | Fita LED 7x</div>

    <div class="cards">
      <div class="card nivel">
        <div class="big" id="nivelPorc">--%</div>
        <div class="small">Reservatório</div>
        <div class="small" id="nivelCm">-- cm</div>
      </div>

      <div class="card dht">
        <div class="big" id="temp">--°C</div>
        <div class="small">Temperatura</div>
        <div class="big" id="hum">--%</div>
        <div class="small">Umidade</div>
      </div>

      <div class="card boiler">
        <div class="big" id="tempBoiler">--°C</div>
        <div class="small">Boiler (DS18B20)</div>
      </div>

      <div class="card rele">
        <div class="big" id="releStatus">OFF</div>
        <div class="small">Relé</div>
        <button class="btn-rele off" id="btnRele" onclick="toggleRele()">LIGAR</button>
      </div>
    </div>

    <h3>Gráfico Nível (%)</h3>
    <canvas id="chart" height="220"></canvas>
  </div>

<script>
  const ctx = document.getElementById('chart').getContext('2d');
  const chart = new Chart(ctx, {
    type: 'line',
    data: {
      labels: [],
      datasets: [{
        label: 'Nível (%)',
        data: [],
        borderColor: '#00ff88',
        backgroundColor: 'rgba(0,255,136,0.15)',
        tension: 0.4,
        fill: true
      }]
    },
    options: {
      responsive:true,
      scales:{y:{min:0,max:100,ticks:{stepSize:20}}}
    }
  });

  function updateData(d) {
    document.getElementById('nivelPorc').textContent = d.nivel_porcent.toFixed(1) + '%';
    document.getElementById('nivelCm').textContent = d.nivel_cm.toFixed(1) + ' cm';
    document.getElementById('temp').textContent = d.temp.toFixed(1) + '°C';
    document.getElementById('hum').textContent = d.hum.toFixed(0) + '%';
    document.getElementById('tempBoiler').textContent = d.temp_boiler.toFixed(1) + '°C';

    const releOn = d.rele_on;
    document.getElementById('releStatus').textContent = releOn ? 'ON' : 'OFF';
    const btn = document.getElementById('btnRele');
    btn.textContent = releOn ? 'DESLIGAR' : 'LIGAR';
    btn.className = 'btn-rele ' + (releOn ? 'on' : 'off');

    if (chart.data.labels.length >= 30) {
      chart.data.labels.shift();
      chart.data.datasets[0].data.shift();
    }
    chart.data.labels.push(new Date().toLocaleTimeString());
    chart.data.datasets[0].data.push(d.nivel_porcent);
    chart.update('none');
  }

function toggleRele() {
  fetch('/rele').then(r => r.json()).then(d => {
    // Atualiza só o relé imediatamente
    const releOn = d.rele_on;
    document.getElementById('releStatus').textContent = releOn ? 'ON' : 'OFF';
    const btn = document.getElementById('btnRele');
    btn.textContent = releOn ? 'DESLIGAR' : 'LIGAR';
    btn.className = 'btn-rele ' + (releOn ? 'on' : 'off');
  });
}

  function loop() {
    fetch('/data').then(r => r.json()).then(updateData).catch(e => console.log(e));
  }

  setInterval(loop, 3000);
  loop();
</script>
</body>
</html>
)rawliteral";
  server.send_P(200, "text/html", html);
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(RELE_PIN, OUTPUT);
  digitalWrite(RELE_PIN, HIGH);

  dht.begin();
  ds18b20.begin();
  ds18b20.setResolution(10);
  ds18b20.setWaitForConversion(true);
  EEPROM.begin(EEPROM_SIZE);
  carregarUltimosValores();

  Wire.begin(D2, D1);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED falhou");
    while (true);
  }

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Iniciando...");
  display.display();

  // LED strip
  leds.Begin();
  leds.ClearTo(RgbColor(0,0,0));
  leds.Show();
  testarLEDs();

  // WiFi
  WiFi.mode(WIFI_STA);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, password);
  Serial.print("Conectando WiFi");
  unsigned long inicioWiFi = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicioWiFi < 20000) {
    delay(300);
    LOG_PRINT(".");
    yield();
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nIP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nWiFi ainda desconectado; reconexao automatica ativa.");
  }

  // WebServer
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/rele", handleRele);
  server.begin();

  atualizarDisplay();
  Serial.println("Pronto");
}

// ================= LOOP =================
void loop() {
  manterWiFi();
  server.handleClient();

  if (ultimo_update != 0 && millis() - ultimo_update > WATCHDOG_APP_MS) {
    ESP.restart();
  }

  if (millis() - ultimo_update > INTERVALO_UPDATE) {
    lerSensores();
    atualizarDisplay();
    atualizarLEDs();

    unsigned long tempoDecorridoSheets = millis() - ultimo_log_sheets;
    long tempoRestante = tempoDecorridoSheets >= INTERVALO_SHEETS ? 0 : (INTERVALO_SHEETS - tempoDecorridoSheets) / 1000;
    LOG_PRINTF("Nivel %.1f cm %.1f%% | Amb %.1f C | Umid %.0f%% | Boiler %.1f C | Sheets em %ld s\n",
                 nivel, porcentagem, temperatura, umidade, tempBoiler, tempoRestante);

    ultimo_update = millis();
  }

  // Envio para Google Sheets
  if (millis() - ultimo_log_sheets > INTERVALO_SHEETS) {
    LOG_PRINTLN("Enviando ao Google Sheets...");
    enviarGoogleSheets();
    salvarEEPROM();
    ultimo_log_sheets = millis();
  }

  yield();
}
