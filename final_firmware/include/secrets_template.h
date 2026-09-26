/*
 * TEMPLATE - copie este arquivo para 'include/secrets.h' e preencha.
 * NUNCA commite secrets.h em nenhum repositorio (adicione ao .gitignore).
 *
 * Como preencher os blocos de certificado:
 *   Abra cada arquivo baixado em P2.1/P2.2 num editor de texto e cole o
 *   conteudo INTEIRO (incluindo as linhas -----BEGIN...----- e
 *   -----END...-----) dentro do bloco R"KEY( ... )KEY" correspondente.
 *   Nao precisa escapar quebras de linha: o raw string literal do C++
 *   preserva o conteudo exatamente como colado.
 */

#pragma once

// ---------------------------------------------------------------------
// Wi-Fi
// ---------------------------------------------------------------------
#define WIFI_SSID     "SUA_REDE_WIFI"
#define WIFI_PASSWORD "SUA_SENHA_WIFI"

// ---------------------------------------------------------------------
// AWS IoT Core
// ---------------------------------------------------------------------
// Device data endpoint copiado em P2.5, SEM "https://" e SEM porta.
#define AWS_IOT_ENDPOINT "xxxxxxxxxxxxx-ats.iot.us-east-1.amazonaws.com"

// Client ID usado na conexao MQTT. Recomenda-se usar o proprio Thing
// name, para manter rastreabilidade entre identidade MQTT e cadastro.
#define THING_NAME "psi5120-final-<ID>-lamp01"

// Tópicos definidos no escopo do projeto (ver P2.3)
#define TOPIC_TELEMETRY "psi5120-final/<ID>/lamp01/telemetry"
#define TOPIC_COMMAND   "psi5120-final/<ID>/lamp01/command"

// ---------------------------------------------------------------------
// Amazon Root CA 1 (certs/AmazonRootCA1.pem)
// ---------------------------------------------------------------------
static const char AWS_ROOT_CA[] PROGMEM = R"KEY(
-----BEGIN CERTIFICATE-----
COLE_AQUI_O_CONTEUDO_DE_AmazonRootCA1.pem
-----END CERTIFICATE-----
)KEY";

// ---------------------------------------------------------------------
// Device certificate (certs/device.pem.crt)
// ---------------------------------------------------------------------
static const char DEVICE_CERT[] PROGMEM = R"KEY(
-----BEGIN CERTIFICATE-----
COLE_AQUI_O_CONTEUDO_DE_device.pem.crt
-----END CERTIFICATE-----
)KEY";

// ---------------------------------------------------------------------
// Private key (certs/private.pem.key)
// ATENCAO: este e o arquivo mais sensivel do projeto inteiro.
// ---------------------------------------------------------------------
static const char DEVICE_PRIVATE_KEY[] PROGMEM = R"KEY(
-----BEGIN RSA PRIVATE KEY-----
COLE_AQUI_O_CONTEUDO_DE_private.pem.key
-----END RSA PRIVATE KEY-----
)KEY";
