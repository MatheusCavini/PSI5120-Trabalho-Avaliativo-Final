/*
 * PSI5120 - Trabalho Final - Smart Lamp Retrofit
 * Fase 2: identidade e conectividade (SEM sensores/rele nesta fase)
 *
 * Objetivo: provar que o dispositivo autentica via mTLS (certificado
 * X.509) no AWS IoT Core e que a IoT Policy autoriza Connect e um
 * Publish de teste no topico de telemetria.
 *
 * Evidencia esperada no Serial Monitor:
 *   [WIFI_CONNECTED]
 *   [MQTT_CONNECTED]
 *   [PUBLISH_OK]
 *
 * Pre-requisito: copiar include/secrets_template.h para
 * include/secrets.h e preencher com seus valores reais (P2.6).
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "secrets.h"

WiFiClientSecure netClient;
PubSubClient mqttClient(netClient);

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
}

bool conectarMqtt() {
  mqttClient.setServer(AWS_IOT_ENDPOINT, 8883);

  Serial.println("[MQTT] Tentando handshake TLS + conexao MQTT...");

  // O terceiro parametro (Client ID) deve corresponder ao Thing name
  // para manter rastreabilidade entre identidade MQTT e o registro
  // no device registry.
  bool ok = mqttClient.connect(THING_NAME);

  if (ok) {
    Serial.println("[MQTT_CONNECTED]");
  } else {
    // client.state() retorna um codigo PubSubClient (nao um erro HTTP).
    // -2 tipicamente indica falha de rede/TLS antes mesmo do CONNACK;
    // 5 indica CONNACK negado (ex.: falha de autorizacao/policy).
    Serial.print("[MQTT_CONNECT_FAILED] state=");
    Serial.println(mqttClient.state());
  }

  return ok;
}

void publicarTeste() {
  const char* payload = "{\"test\":\"fase2_connectivity_check\"}";
  bool ok = mqttClient.publish(TOPIC_TELEMETRY, payload);

  if (ok) {
    Serial.println("[PUBLISH_OK]");
  } else {
    Serial.println("[PUBLISH_FAILED]");
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("[BOOT] Fase 2 - identidade e conectividade");

  conectarWifi();

  // Configura a conexao TLS com os tres elementos da credencial mTLS:
  // CA do servidor (para o dispositivo validar o AWS IoT Core) e
  // certificado + chave privada do dispositivo (para o AWS IoT Core
  // validar o dispositivo).
  netClient.setCACert(AWS_ROOT_CA);
  netClient.setCertificate(DEVICE_CERT);
  netClient.setPrivateKey(DEVICE_PRIVATE_KEY);

  if (conectarMqtt()) {
    publicarTeste();
  }
}

void loop() {
  // Fase 2 nao mantem conexao persistente nem faz retry automatico:
  // o objetivo aqui e um teste pontual de identidade/conectividade.
  // Retry com backoff e o comportamento continuo entram na Fase 3,
  // junto com a logica real de telemetria e shadow.
  mqttClient.loop();
  delay(1000);
}