/*
 * PSI5120 - Trabalho Final - Smart Lamp Retrofit
 * Fase 1: teste isolado de hardware (SEM MQTT / SEM nuvem)
 *
 * Comportamento:
 *   - Le o LDR (fotoresistor) em uma entrada analogica.
 *   - Le a distancia via sensor ultrassonico HC-SR04.
 *   - Aciona o rele (liga a lampada) se:
 *        (a) o ambiente estiver "escuro" (leitura do LDR abaixo de um limiar)
 *         OU
 *        (b) houver um objeto a <= 10 cm do sensor ultrassonico
 *   - Caso contrario, mantem o rele desligado.
 *
 * Objetivo desta fase: validar sensores e atuador isoladamente, com
 * evidencia via Serial Monitor, ANTES de introduzir MQTT/TLS/Shadow.
 *
 * IMPORTANTE (seguranca eletrica):
 *   - O rele deve ser um modulo opto-isolado apropriado para chavear a
 *     carga AC da lampada. Toda a fiacao de rede eletrica deve
 *     permanecer dentro de um invólucro fechado. Nao manusear a lampada
 *     energizada durante os testes.
 */

#include <Arduino.h>

// ---------------------------------------------------------------------
// Pinagem (ajuste conforme sua montagem)
// ---------------------------------------------------------------------
const int PIN_LDR      = 34;  // entrada analogica (ADC1), somente leitura
const int PIN_TRIG     = 5;   // saida digital - trigger do HC-SR04
const int PIN_ECHO     = 18;  // entrada digital - echo do HC-SR04
const int PIN_RELAY    = 26;  // saida digital - aciona o modulo de rele

// ---------------------------------------------------------------------
// Parametros de decisao (calibrar experimentalmente na Fase 1)
// ---------------------------------------------------------------------
// Leitura do ADC do ESP32 vai de 0 a 4095 (12 bits).
// Valores MENORES tendem a indicar MENOS luz incidindo no LDR num divisor
// de tensao tipico (LDR entre VCC e o pino, resistor fixo entre o pino e
// GND) - mas isso depende da sua montagem. Calibre com o Serial Monitor
// antes de confiar neste limiar.
const int   LDR_ESCURO_LIMIAR   = 1500;   // ajustar apos observar valores reais

const float DISTANCIA_LIMIAR_CM = 10.0;   // proximidade que aciona o rele
const unsigned long ECHO_TIMEOUT_US = 30000UL; // ~5m de alcance maximo, evita travar em pulseIn

// Logica do rele: alguns modulos sao "active LOW" (liga com LOW no pino
// de controle). Ajuste esta constante conforme o seu modulo.
const bool RELAY_ACTIVE_LOW = true;

// Intervalo entre leituras/decisoes
const unsigned long INTERVALO_LOOP_MS = 1000;

// ---------------------------------------------------------------------
// Funcoes auxiliares
// ---------------------------------------------------------------------

// Dispara o HC-SR04 e retorna a distancia estimada em centimetros.
// Retorna -1.0 se o eco nao for recebido dentro do timeout (sem leitura valida).
float lerDistanciaCm() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);

  unsigned long duracaoUs = pulseIn(PIN_ECHO, HIGH, ECHO_TIMEOUT_US);

  if (duracaoUs == 0) {
    // timeout: nenhum eco recebido (nada no alcance, ou erro de leitura)
    return -1.0;
  }

  // Velocidade do som ~ 0.0343 cm/us; divide por 2 (ida e volta)
  float distanciaCm = (duracaoUs * 0.0343f) / 2.0f;
  return distanciaCm;
}

void setRele(bool ligar) {
  bool nivelEletrico = RELAY_ACTIVE_LOW ? !ligar : ligar;
  digitalWrite(PIN_RELAY, nivelEletrico ? HIGH : LOW);
}

// ---------------------------------------------------------------------
// Setup / Loop
// ---------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_RELAY, OUTPUT);

  digitalWrite(PIN_TRIG, LOW);
  setRele(false); // estado inicial: lampada apagada

  Serial.println();
  Serial.println("[BOOT] Fase 1 - teste local de hardware iniciado");
  Serial.println("[BOOT] Sem MQTT/nuvem nesta fase.");
}

void loop() {
  int leituraLdr = analogRead(PIN_LDR);
  bool escuro = (leituraLdr < LDR_ESCURO_LIMIAR);

  float distanciaCm = lerDistanciaCm();
  bool presencaProxima = (distanciaCm > 0 && distanciaCm <= DISTANCIA_LIMIAR_CM);

  bool deveLigar = escuro || presencaProxima;
  setRele(deveLigar);

  // Log estruturado simples para facilitar captura de evidencia
  Serial.print("[LEITURA] ldr=");
  Serial.print(leituraLdr);
  Serial.print(" escuro=");
  Serial.print(escuro ? "true" : "false");
  Serial.print(" distancia_cm=");
  if (distanciaCm > 0) {
    Serial.print(distanciaCm, 1);
  } else {
    Serial.print("sem_eco");
  }
  Serial.print(" presenca_proxima=");
  Serial.print(presencaProxima ? "true" : "false");
  Serial.print(" lamp_state=");
  Serial.println(deveLigar ? "on" : "off");

  delay(INTERVALO_LOOP_MS);
}