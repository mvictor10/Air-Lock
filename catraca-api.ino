#include <SPI.h>
#include <MFRC522.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>  // Biblioteca para portal de configuração Wi‑Fi

// ——— Pinos —————————————————————————————————————————————
#define SS_PIN      D8  // SDA do RC522
#define RST_PIN     D3  // RST do RC522
#define LED_AZUL    D2  // GPIO4: pisca em toda leitura
#define LED_VERDE   D1  // GPIO5: pisca se autorizado

// ——— Constantes de configuração —————————————————————————
const char*   AP_SSID   = "AIR-LOCK";           // SSID temporário do portal
const char*   API_URL   = "http://192.168.4.2:3000/api.json"; // URL da sua API JSON
const uint16_t HTTP_PORT = 80;                   // Porta para o servidor web

// ——— Objetos globais —————————————————————————————————————
MFRC522        rfid(SS_PIN, RST_PIN);             // Leitor RFID
ESP8266WebServer server(HTTP_PORT);               // Servidor HTTP nativo
WiFiClient     wifiClient;                        // Cliente TCP para HTTPClient
HTTPClient     http;                              // HTTPClient para requisições

String lastUID    = "";                           // Último UID lido
String lastStatus = "nenhum";                     // Último status ("liberado"/"não autorizado")

// ——— Protótipos de funções —————————————————————————————————
void   setupRFID();
void   setupLEDs();
void   setupWiFiPortal();
void   setupServer();
String readUidString();
bool   queryApi(const String& uid);
void   blink(uint8_t pin, uint16_t ms);

// ——— setup() —————————————————————————————————————————————————
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Iniciando Catraca ESP8266 ===");

  setupRFID();
  setupLEDs();
  setupWiFiPortal();
  setupServer();

  Serial.println("Pronto! Acesse http://" + WiFi.localIP().toString() + "/uid");
}

// ——— loop() ——————————————————————————————————————————————————
void loop() {
  server.handleClient();

  // Pisca LED azul sempre que tenta ler
  blink(LED_AZUL, 100);

  // Se não há cartão novo, sai
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    return;
  }

  // Lê e formata UID
  lastUID = readUidString();
  Serial.println("UID lido: " + lastUID);

  // Consulta a API para verificar autorização
  bool autorizado = queryApi(lastUID);
  lastStatus = autorizado ? "liberado" : "não autorizado";
  Serial.println("Status: " + lastStatus);

  // Se autorizado, pisca LED verde
  if (autorizado) {
    blink(LED_VERDE, 200);
  }

  delay(500);  // debounce
}

// ——— Implementação das funções ———————————————————————————

// Inicializa o leitor RFID
void setupRFID() {
  SPI.begin();
  rfid.PCD_Init();
  Serial.println("RFID inicializado");
}

// Configura os pinos dos LEDs
void setupLEDs() {
  pinMode(LED_AZUL, OUTPUT);
  pinMode(LED_VERDE, OUTPUT);
  digitalWrite(LED_AZUL, LOW);
  digitalWrite(LED_VERDE, LOW);
}

// Abre portal de configuração Wi‑Fi (AP) e conecta como STA
void setupWiFiPortal() {
  WiFiManager wm;
  wm.setConfigPortalTimeout(180);       // timeout em segundos
  if (!wm.autoConnect(AP_SSID)) {       // abre portal "ESP_SETUP"
    Serial.println("Falha no portal de configuração — reiniciando...");
    ESP.restart();
  }
  Serial.println("Wi‑Fi configurado com sucesso!");
  Serial.println("IP local: " + WiFi.localIP().toString());
}

// Define rotas e inicia o servidor HTTP
void setupServer() {
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html",
                "<h3>API Catraca ESP8266</h3>"
                "<p>/uid → { \"uid\": \"...\", \"status\": \"...\" }</p>");
  });

  server.on("/uid", HTTP_GET, []() {
    StaticJsonDocument<200> doc;
    doc["uid"]    = lastUID;
    doc["status"] = lastStatus;
    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
  });

  server.begin();
  Serial.printf("Servidor HTTP iniciado na porta %u\n", HTTP_PORT);
}

// Lê o UID do cartão e retorna como string "AA BB CC DD"
String readUidString() {
  String s;
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) s += '0';
    s += String(rfid.uid.uidByte[i], HEX);
    if (i + 1 < rfid.uid.size) s += ' ';
  }
  s.toUpperCase();
  return s;
}

// Consulta a API JSON e retorna true se estado == "matriculado"
bool queryApi(const String& uid) {
  http.begin(wifiClient, API_URL);
  if (http.GET() != HTTP_CODE_OK) {
    http.end();
    Serial.println("Erro HTTP ao consultar API");
    return false;
  }
  String payload = http.getString();
  http.end();

  StaticJsonDocument<1024> doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.println("Erro ao parsear JSON: " + String(err.c_str()));
    return false;
  }

  for (JsonObject obj : doc.as<JsonArray>()) {
    if (uid == obj["uid"].as<const char*>() &&
        String(obj["estado"].as<const char*>()) == "matriculado") {
      return true;
    }
  }
  return false;
}

// Pisca um LED por 'ms' milissegundos
void blink(uint8_t pin, uint16_t ms) {
  digitalWrite(pin, HIGH);
  delay(ms);
  digitalWrite(pin, LOW);
}
