/*
 * PSI5120 - Trabalho Final - Smart Lamp Retrofit
 * Fase 3 (v2): telemetria, Device Shadow e comando identify
 *
 * MUDANCA DE ARQUITETURA: a combinacao WiFiClientSecure + PubSubClient
 * apresentou desconexoes recorrentes (disconnectReason=CLIENT_ERROR),
 * confirmadas via CloudWatch como um pacote MQTT corrompido/fora de
 * ordem - uma categoria de bug conhecida e documentada publicamente
 * nessa combinacao especifica de bibliotecas no ESP32, sem correcao
 * definitiva do lado da aplicacao. Substituimos pelo esp-mqtt, o
 * cliente MQTT/TLS nativo do ESP-IDF (ja embutido no core arduino-esp32,
 * nao precisa de biblioteca externa), que usa uma pilha de socket/TLS
 * diferente - a mesma usada pelos exemplos oficiais da Espressif para
 * AWS IoT - e faz reconexao automatica internamente.
 *
 * Funde:
 *   - Fase 1: leitura de LDR + HC-SR04 e acionamento do rele
 *   - Fase 2/3: identidade mTLS, telemetria, shadow, comando identify
 *
 * Pre-requisito: copiar include/secrets_template.h para
 * include/secrets.h e preencher com seus valores reais (inalterado
 * em relacao a versao anterior).
 */

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <time.h>
#include "esp_event.h"
#include "mqtt_client.h"
#include "esp_arduino_version.h"
#include "secrets.h"

// ---------------------------------------------------------------------
// Pinagem (igual as fases anteriores)
// ---------------------------------------------------------------------
const int PIN_LDR   = 34;
const int PIN_TRIG  = 5;
const int PIN_ECHO  = 18;
const int PIN_RELAY = 26;

// ---------------------------------------------------------------------
// Parametros de decisao (calibrados na Fase 1)
// ---------------------------------------------------------------------
const int   LDR_ESCURO_LIMIAR       = 1500;
const float DISTANCIA_LIMIAR_CM     = 10.0;
const unsigned long ECHO_TIMEOUT_US = 15000UL;
const bool RELAY_ACTIVE_LOW = true;

const unsigned long SENSOR_LOOP_INTERVAL_MS = 500;
const unsigned long TELEMETRY_INTERVAL_MS   = 3000;

// ---------------------------------------------------------------------
// Estado global da aplicacao
// ---------------------------------------------------------------------
String currentMode      = "auto";
String currentLampState = "off";
unsigned long sequenceCounter = 0;

int   ultimaLeituraLdr = 0;
bool  ultimoEscuro     = false;
float ultimaDistanciaCm = -1.0;
bool  ultimaPresenca    = false;

unsigned long ultimaLeituraSensores   = 0;
unsigned long ultimoPublishTelemetria = 0;

String shadowUpdateTopic;
String shadowDeltaTopic;

esp_mqtt_client_handle_t mqttClient = nullptr;
volatile bool mqttConectado = false;

// ---------------------------------------------------------------------
// Atuador
// ---------------------------------------------------------------------
void setRele(bool ligar) {
  bool nivelEletrico = RELAY_ACTIVE_LOW ? !ligar : ligar;
  digitalWrite(PIN_RELAY, nivelEletrico ? HIGH : LOW);
}

// ---------------------------------------------------------------------
// Sensor ultrassonico
// ---------------------------------------------------------------------
float lerDistanciaCm() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);

  unsigned long duracaoUs = pulseIn(PIN_ECHO, HIGH, ECHO_TIMEOUT_US);
  if (duracaoUs == 0) {
    return -1.0;
  }
  return (duracaoUs * 0.0343f) / 2.0f;
}

// ---------------------------------------------------------------------
// Timestamp real via NTP
// ---------------------------------------------------------------------
String obterTimestampIso8601() {
  time_t agora;
  time(&agora);
  struct tm timeinfo;
  gmtime_r(&agora, &timeinfo);
  char buffer[25];
  strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  return String(buffer);
}

// ---------------------------------------------------------------------
// Shadow: publica o estado reportado (ground truth do dispositivo)
// ---------------------------------------------------------------------
void publishShadowReported() {
  if (!mqttConectado) {
    Serial.println("[SHADOW_REPORTED_SKIPPED] MQTT desconectado no momento");
    return;
  }

  StaticJsonDocument<256> doc;
  JsonObject state = doc.createNestedObject("state");
  JsonObject reported = state.createNestedObject("reported");
  reported["mode"] = currentMode;
  reported["lamp_state"] = currentLampState;

  char buffer[256];
  size_t n = serializeJson(doc, buffer, sizeof(buffer));

  int msgId = esp_mqtt_client_publish(mqttClient, shadowUpdateTopic.c_str(), buffer, n, 0, 0);

  Serial.print(msgId >= 0 ? "[SHADOW_REPORTED] " : "[SHADOW_REPORTED_FAILED] ");
  Serial.println(buffer);
}

// ---------------------------------------------------------------------
// Logica de decisao: le sensores e, em modo auto, decide o lamp_state
// (identica as fases anteriores)
// ---------------------------------------------------------------------
void avaliarSensoresEAtualizarEstado() {
  ultimaLeituraLdr = analogRead(PIN_LDR);
  ultimoEscuro = (ultimaLeituraLdr < LDR_ESCURO_LIMIAR);

  ultimaDistanciaCm = lerDistanciaCm();
  ultimaPresenca = (ultimaDistanciaCm > 0 && ultimaDistanciaCm <= DISTANCIA_LIMIAR_CM);

  if (currentMode == "auto") {
    String novoEstado = (ultimoEscuro || ultimaPresenca) ? "on" : "off";
    if (novoEstado != currentLampState) {
      currentLampState = novoEstado;
      setRele(currentLampState == "on");
      Serial.print("[AUTO_DECISION] lamp_state -> ");
      Serial.println(currentLampState);
      publishShadowReported();
    }
  }
  // Em modo manual, os sensores continuam sendo lidos e publicados como
  // telemetria, mas nao influenciam o rele: quem decide e o shadow.
}

// ---------------------------------------------------------------------
// Telemetria
// ---------------------------------------------------------------------
void publicarTelemetria() {
  if (!mqttConectado) {
    Serial.println("[TELEMETRY_PUBLISH_SKIPPED] MQTT desconectado no momento");
    return;
  }

  StaticJsonDocument<384> doc;
  doc["device_id"] = THING_NAME;
  doc["sequence"] = sequenceCounter++;
  doc["timestamp"] = obterTimestampIso8601();
  doc["light_raw"] = ultimaLeituraLdr;
  doc["escuro"] = ultimoEscuro;
  if (ultimaDistanciaCm > 0) {
    doc["distance_cm"] = ultimaDistanciaCm;
  } else {
    doc["distance_cm"] = nullptr;
  }
  doc["presence_proxima"] = ultimaPresenca;
  doc["mode"] = currentMode;
  doc["lamp_state"] = currentLampState;

  char buffer[384];
  size_t n = serializeJson(doc, buffer, sizeof(buffer));

  int msgId = esp_mqtt_client_publish(mqttClient, TOPIC_TELEMETRY, buffer, n, 0, 0);

  Serial.print(msgId >= 0 ? "[TELEMETRY_PUBLISH_OK] " : "[TELEMETRY_PUBLISH_FAILED] ");
  Serial.println(buffer);
}

// ---------------------------------------------------------------------
// Shadow: trata o delta recebido (diferenca entre desired e reported)
// ---------------------------------------------------------------------
void handleShadowDelta(const uint8_t* payload, size_t length) {
  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err) {
    Serial.print("[SHADOW_DELTA_PARSE_ERROR] ");
    Serial.println(err.c_str());
    return;
  }

  JsonObject state = doc["state"];
  bool precisaReportar = false;

  if (state.containsKey("mode")) {
    String novoModo = state["mode"].as<String>();
    if (novoModo == "auto" || novoModo == "manual") {
      currentMode = novoModo;
      precisaReportar = true;
      Serial.print("[SHADOW_DELTA] mode -> ");
      Serial.println(currentMode);
    } else {
      Serial.print("[SHADOW_DELTA_IGNORED] valor invalido para mode: ");
      Serial.println(novoModo);
    }
  }

  if (state.containsKey("lamp_state")) {
    String novoEstado = state["lamp_state"].as<String>();
    if (currentMode == "manual") {
      if (novoEstado == "on" || novoEstado == "off") {
        currentLampState = novoEstado;
        setRele(currentLampState == "on");
        precisaReportar = true;
        Serial.print("[SHADOW_DELTA] lamp_state -> ");
        Serial.println(currentLampState);
      } else {
        Serial.print("[SHADOW_DELTA_IGNORED] valor invalido para lamp_state: ");
        Serial.println(novoEstado);
      }
    } else {
      Serial.println("[SHADOW_DELTA_IGNORED] lamp_state recebido em modo auto; sensor decide localmente");
    }
  }

  if (precisaReportar) {
    publishShadowReported();
  }
}

// ---------------------------------------------------------------------
// Comando identify: fire-and-forget, sem estado persistido
// ---------------------------------------------------------------------
void handleCommand(const uint8_t* payload, size_t length) {
  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err) {
    Serial.print("[COMMAND_PARSE_ERROR] ");
    Serial.println(err.c_str());
    return;
  }

  String comando = doc["command"].as<String>();
  Serial.print("[COMMAND_RECEIVED] ");
  Serial.println(comando);

  if (comando == "identify") {
    bool estadoAnterior = (currentLampState == "on");
    for (int i = 0; i < 3; i++) {
      setRele(!estadoAnterior);
      delay(200);
      setRele(estadoAnterior);
      delay(200);
    }
    Serial.println("[IDENTIFY_DONE]");
  } else {
    Serial.println("[COMMAND_IGNORED] comando desconhecido");
  }
}

// ---------------------------------------------------------------------
// Handler de eventos do esp-mqtt. Roda no contexto da task interna da
// biblioteca (nao no loop() principal) - mantemos o processamento
// simples e reutilizamos as mesmas funcoes de tratamento de cima.
// ---------------------------------------------------------------------
static void mqttEventHandler(void* handlerArgs, esp_event_base_t base, int32_t eventId, void* eventData) {
  auto event = (esp_mqtt_event_handle_t)eventData;

  switch ((esp_mqtt_event_id_t)eventId) {
    case MQTT_EVENT_CONNECTED:
      Serial.println("[MQTT_CONNECTED]");
      mqttConectado = true;
      esp_mqtt_client_subscribe(mqttClient, shadowDeltaTopic.c_str(), 0);
      esp_mqtt_client_subscribe(mqttClient, TOPIC_COMMAND, 0);
      Serial.println("[SUBSCRIBED] shadow/update/delta e command");
      publishShadowReported();
      break;

    case MQTT_EVENT_DISCONNECTED:
      Serial.println("[MQTT_DISCONNECTED] (reconexao automatica interna do esp-mqtt)");
      mqttConectado = false;
      break;

    case MQTT_EVENT_DATA: {
      String topicStr(event->topic, event->topic_len);
      if (topicStr == shadowDeltaTopic) {
        handleShadowDelta((const uint8_t*)event->data, event->data_len);
      } else if (topicStr == String(TOPIC_COMMAND)) {
        handleCommand((const uint8_t*)event->data, event->data_len);
      }
      break;
    }

    case MQTT_EVENT_ERROR:
      Serial.println("[MQTT_EVENT_ERROR]");
      if (event->error_handle && event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
        Serial.print("[MQTT_TLS_ERROR] esp_tls_last_esp_err=0x");
        Serial.println(event->error_handle->esp_tls_last_esp_err, HEX);
      }
      break;

    default:
      break;
  }
}

// ---------------------------------------------------------------------
// Conectividade
// ---------------------------------------------------------------------
void conectarWifi() {
  Serial.print("[WIFI] Conectando a ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("[WIFI_CONNECTED] IP local: ");
  Serial.println(WiFi.localIP());
  Serial.print("[WIFI_RSSI] ");
  Serial.println(WiFi.RSSI());
}

void iniciarMqtt() {
  shadowUpdateTopic = String("$aws/things/") + THING_NAME + "/shadow/update";
  shadowDeltaTopic  = shadowUpdateTopic + "/delta";

  String brokerUri = String("mqtts://") + AWS_IOT_ENDPOINT + ":8883";

  esp_mqtt_client_config_t mqttCfg = {};

#if ESP_ARDUINO_VERSION_MAJOR >= 3
  // arduino-esp32 3.x (baseado em ESP-IDF 5.x): struct de config aninhada
  mqttCfg.broker.address.uri = brokerUri.c_str();
  mqttCfg.broker.verification.certificate = AWS_ROOT_CA;
  mqttCfg.credentials.client_id = THING_NAME;
  mqttCfg.credentials.authentication.certificate = DEVICE_CERT;
  mqttCfg.credentials.authentication.key = DEVICE_PRIVATE_KEY;
#else
  // arduino-esp32 2.x (baseado em ESP-IDF 4.4.x, caso do platform
  // espressif32@6.9.0 usado neste projeto): struct de config "plana"
  mqttCfg.uri = brokerUri.c_str();
  mqttCfg.cert_pem = AWS_ROOT_CA;
  mqttCfg.client_id = THING_NAME;
  mqttCfg.client_cert_pem = DEVICE_CERT;
  mqttCfg.client_key_pem = DEVICE_PRIVATE_KEY;
#endif

  mqttClient = esp_mqtt_client_init(&mqttCfg);
  esp_mqtt_client_register_event(mqttClient, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, mqttEventHandler, NULL);
  esp_mqtt_client_start(mqttClient);

  Serial.println("[MQTT] Cliente iniciado (reconexao automatica interna do esp-mqtt)");
}

// ---------------------------------------------------------------------
// Setup / Loop
// ---------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("[BOOT] Fase 3 (v2, esp-mqtt) - telemetria, shadow e comando");

  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_TRIG, LOW);
  setRele(false);

  conectarWifi();

  // O esp-mqtt depende do event loop padrao do ESP-IDF. O Arduino core
  // normalmente ja cria esse loop internamente (via WiFi.begin), entao
  // tratamos ESP_ERR_INVALID_STATE (loop ja existe) como algo normal,
  // nao um erro real.
  esp_err_t evLoopErr = esp_event_loop_create_default();
  if (evLoopErr != ESP_OK && evLoopErr != ESP_ERR_INVALID_STATE) {
    Serial.print("[EVENT_LOOP_ERROR] ");
    Serial.println(esp_err_to_name(evLoopErr));
  }

  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  Serial.print("[NTP] Sincronizando horario");
  time_t agora = time(nullptr);
  int tentativas = 0;
  const time_t EPOCH_MINIMO_PLAUSIVEL = 8 * 3600 * 2;
  while (agora < EPOCH_MINIMO_PLAUSIVEL && tentativas < 20) {
    delay(250);
    Serial.print(".");
    agora = time(nullptr);
    tentativas++;
  }
  Serial.println();
  Serial.println(agora >= EPOCH_MINIMO_PLAUSIVEL ? "[NTP_OK]" : "[NTP_TIMEOUT] prosseguindo mesmo assim");

  iniciarMqtt();

  // Leitura inicial dos sensores. O primeiro publishShadowReported() ja
  // acontece dentro do evento MQTT_EVENT_CONNECTED (mqttEventHandler),
  // entao nao repetimos aqui.
  avaliarSensoresEAtualizarEstado();
}

void loop() {
  unsigned long agora = millis();

  if (agora - ultimaLeituraSensores >= SENSOR_LOOP_INTERVAL_MS) {
    avaliarSensoresEAtualizarEstado();
    ultimaLeituraSensores = agora;
  }

  if (agora - ultimoPublishTelemetria >= TELEMETRY_INTERVAL_MS) {
    publicarTelemetria();
    ultimoPublishTelemetria = agora;
  }

  delay(20);
}