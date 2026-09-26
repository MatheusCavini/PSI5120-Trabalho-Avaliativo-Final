



# Smart Lamp Retrofit — AWS IoT Core

An end-to-end IoT implementation developed for **PSI5120** that retrofits a desk lamp with a broken physical switch into a connected device using an **ESP32**, local sensing, **AWS IoT Core**, MQTT, Device Shadow, Lambda, DynamoDB, and a browser-based control panel.

The project focuses on the technical architecture of a complete device-to-cloud and cloud-to-device path, from sensor acquisition and relay actuation to authenticated MQTT communication, state synchronization, serverless processing, and persistent telemetry storage.

---

## Overview

The lamp has two operating modes:

- **Auto:** the ESP32 decides locally whether the lamp should be on based on ambient light and proximity.
- **Manual:** the lamp state is controlled through the AWS IoT Device Shadow.

A separate **`identify`** command is sent as a stateless MQTT message to make the lamp blink without modifying its persistent shadow state.

The device communicates with AWS IoT Core over **MQTT/TLS with an X.509 client certificate**. Telemetry is routed by an IoT Rule to Lambda and then persisted in DynamoDB. A second Lambda, exposed through an API Gateway HTTP API, provides the backend for the browser control panel.

---

## System Architecture

<!--
TODO: Add the final architecture diagram here.
Suggested path: docs/images/architecture.png
The diagram should show:
ESP32 -> AWS IoT Core (MQTT + Device Shadow) -> IoT Rule -> Lambda -> DynamoDB
and the reverse/control path:
Browser -> API Gateway -> Lambda -> AWS IoT Data Plane -> Device Shadow / MQTT command
-->

> **Architecture diagram — planned figure**  
> Add the final system architecture diagram here.

### Main data flows

**Telemetry / device-to-cloud**

```text
ESP32
  │
  │ MQTT over TLS / X.509 mTLS
  ▼
AWS IoT Core
  │
  │ IoT Rule
  ▼
AWS Lambda (telemetry_iot_rule.py)
  │
  │ PutItem
  ▼
Amazon DynamoDB
```

**State synchronization / cloud-to-device**

```text
Browser / AWS IoT Console
          │
          │ desired state
          ▼
   AWS IoT Device Shadow
          │
          │ shadow/update/delta
          ▼
        ESP32
          │
          │ reported state
          └──────────────► Device Shadow
```

**Web control path**

```text
Browser
  │ HTTP
  ▼
API Gateway HTTP API
  │
  ▼
AWS Lambda (webapp_api_backend.py)
  │
  ├── GetThingShadow / UpdateThingShadow
  └── Publish identify command
          │
          ▼
   AWS IoT Data Plane
```

---

## Hardware

The prototype uses:

| Component | Function |
|---|---|
| ESP32 DOIT DevKit V1 | Main controller and AWS IoT client |
| LDR / photoresistor | Ambient light measurement |
| HC-SR04 | Proximity measurement |
| Relay module | Lamp switching |
| Desk lamp | Physical actuator/load |

### Pin mapping

| Signal | ESP32 GPIO |
|---|---:|
| LDR analog input | `34` |
| HC-SR04 TRIG | `5` |
| HC-SR04 ECHO | `18` |
| Relay control | `26` |

<!--
TODO: Add a photo or wiring diagram of the physical prototype.
Suggested path: docs/images/hardware.jpg
-->

> **Hardware photo / wiring diagram — planned figure**  
> Add the physical prototype image here.

---

## Firmware

The final firmware is located in [`final_firmware/`](final_firmware/).

### Platform

- **Board:** `esp32doit-devkit-v1`
- **Framework:** Arduino
- **PlatformIO platform:** `espressif32@6.9.0`
- **JSON:** ArduinoJson `^6.21.5`
- **MQTT/TLS:** `esp-mqtt`, provided by the ESP-IDF layer underneath the Arduino ESP32 core

The final implementation uses the native ESP-IDF MQTT client for the TLS/MQTT connection and automatic reconnection.

### Runtime behavior

The firmware has two periodic activities:

- Sensor evaluation every **500 ms**.
- Telemetry publication every **3 s**.

In `auto` mode:

```text
lamp_state = ON
when (dark) OR (presence_near)
```

The current thresholds are:

- LDR dark threshold: `light_raw < 1500`
- Proximity threshold: `distance_cm <= 10 cm`

In `manual` mode, the sensors continue to be measured and included in telemetry, but they no longer control the relay. The relay follows the `desired.lamp_state` received through the Device Shadow.

### Telemetry payload

A typical telemetry message contains:

```json
{
  "device_id": "psi5120-final-<ID>-lamp01",
  "sequence": 42,
  "timestamp": "2026-09-26T18:41:45Z",
  "light_raw": 2509,
  "escuro": false,
  "distance_cm": 192.4,
  "presence_proxima": false,
  "mode": "auto",
  "lamp_state": "off"
}
```

The monotonically increasing `sequence` field is used as the DynamoDB sort key for telemetry records.

---

## MQTT Topics

The device uses an application-specific telemetry and command namespace:

| Topic | Direction | Purpose |
|---|---|---|
| `psi5120-final/<ID>/lamp01/telemetry` | Device → Cloud | Periodic sensor/device telemetry |
| `psi5120-final/<ID>/lamp01/command` | Cloud → Device | Stateless commands such as `identify` |
| `$aws/things/<THING_NAME>/shadow/update` | Device → AWS IoT | Reported shadow state |
| `$aws/things/<THING_NAME>/shadow/update/delta` | AWS IoT → Device | Difference between desired and reported state |

The shadow document uses two state variables:

```json
{
  "mode": "auto | manual",
  "lamp_state": "on | off"
}
```

The shadow provides the persistent desired/reported state model, while `identify` deliberately remains a transient MQTT command.

---

## AWS IoT Core and Security

Each physical device is represented by one AWS IoT **Thing** and an associated **X.509 certificate**.

The device authenticates using **mutual TLS (mTLS)**. Authorization is handled independently through an IoT policy scoped to the operations required by the firmware:

- `iot:Connect` for the device client identity
- `iot:Publish` for telemetry and shadow updates
- `iot:Subscribe` for shadow delta and command filters
- `iot:Receive` for the corresponding subscribed topics

The policy template is available at [`AWS Policies/iot_policy_lamp.json`](AWS%20Policies/iot_policy_lamp.json).

The repository also contains the policy used by the web backend:

[`AWS Policies/webapp_backend_policy.json`](AWS%20Policies/webapp_backend_policy.json)

which grants only the required Device Shadow operations and publication of the `identify` command.

---

## Serverless Telemetry Pipeline

The file [`AWS Lambdas/telemetry_iot_rule.py`](AWS%20Lambdas/telemetry_iot_rule.py) implements the telemetry Lambda.

```text
MQTT telemetry
      │
      ▼
AWS IoT Rule
      │
      ▼
telemetry_iot_rule.py
      │
      ▼
DynamoDB PutItem
```

The Lambda receives the MQTT payload forwarded by the IoT Rule, normalizes the fields, and stores one item per telemetry sample.

The DynamoDB schema is based on:

- **Partition key:** `device_id`
- **Sort key:** `sequence`

The Lambda receives the table name through the `TABLE_NAME` environment variable. Its required DynamoDB permission is documented in [`AWS Policies/dynamo_db_policy.json`](AWS%20Policies/dynamo_db_policy.json).

---

## Browser Control Panel

The browser UI is implemented as a standalone HTML page in [`remote_control.html`](remote_control.html).

<!--
TODO: Add a screenshot of the final control panel here.
Suggested path: docs/images/control-panel.png
-->

![alt text](<control pannel.png>)

The page periodically reads the current Device Shadow and provides controls for:

- switching between **Auto** and **Manual** mode;
- setting the lamp **On/Off** state in Manual mode;
- sending the stateless **Identify** command.

The browser does **not** connect directly to MQTT. Instead, it calls an HTTP API backed by [`AWS Lambdas/webapp_api_backend.py`](AWS%20Lambdas/webapp_api_backend.py).

### HTTP API

| Method | Route | Function |
|---|---|---|
| `GET` | `/shadow` | Read the current Device Shadow |
| `POST` | `/shadow` | Update `desired` shadow state |
| `POST` | `/identify` | Publish the stateless identify command |

The Lambda uses the AWS IoT **data plane** through `boto3` and does not require MQTT credentials in the browser.

---

## Repository Structure

```text
.
├── final_firmware/
│   ├── include/
│   │   └── secrets_template.h
│   ├── src/
│   │   └── main.cpp
│   └── platformio.ini
│
├── local_sensors_test/
│   ├── include/
│   └── src/
│       └── main.cpp
│
├── connectivity_test/
│   ├── include/
│   ├── src/
│   │   └── main.cpp
│   └── platformio.ini
│
├── AWS Lambdas/
│   ├── telemetry_iot_rule.py
│   └── webapp_api_backend.py
│
├── AWS Policies/
│   ├── iot_policy_lamp.json
│   ├── webapp_backend_policy.json
│   └── dynamo_db_policy.json
│
├── evidences/
│   ├── logs.txt
│   ├── lambda_logs.csv
│   └── Shadow Events.txt
│
└── remote_control.html
```

### Development stages

The firmware is split into small stages so hardware, connectivity, and the final application could be validated independently:

1. **`local_sensors_test/`** — validates LDR, HC-SR04 and relay behavior without cloud connectivity.
2. **`connectivity_test/`** — validates Wi-Fi, X.509 authentication, MQTT/TLS connectivity and a test publication to AWS IoT Core.
3. **`final_firmware/`** — combines sensing, actuation, telemetry, Device Shadow synchronization and the `identify` command.

---

## Configuration

Sensitive device credentials are intentionally not stored in the repository.

For the final firmware, start from:

```text
final_firmware/include/secrets_template.h
```

and create a local `secrets.h` containing the environment-specific values, including:

- Wi-Fi credentials
- AWS IoT endpoint
- Thing name
- Root CA
- Device certificate
- Device private key

The same pattern is used by the connectivity test.

---

## Validation

The project was validated through the device serial log, AWS IoT Core, CloudWatch Logs, and DynamoDB.

The main executed scenarios were:

| Scenario | Expected behavior |
|---|---|
| Auto mode | Darkness or proximity independently turns the lamp on/off locally |
| Manual + Shadow | Desired state propagates through `delta` and is reflected in `reported` |
| Identify | Lamp blinks without changing persistent shadow state |
| Telemetry persistence | IoT Rule → Lambda → DynamoDB preserves telemetry samples |

Evidence collected during testing is available under [`evidences/`](evidences/).

<!--
TODO: Add selected evidence screenshots here, for example:
- AWS IoT Device Shadow
- MQTT test client / topic traffic
- Lambda CloudWatch logs
- DynamoDB table items
Suggested paths:
  docs/images/shadow.png
  docs/images/mqtt-console.png
  docs/images/cloudwatch.png
  docs/images/dynamodb.png
-->

> **AWS / evidence screenshots — planned figures**  
> Add the selected screenshots from the paper/evidence set here.

---

## Demo


https://github.com/user-attachments/assets/cfc4cc2f-bf45-4083-af5b-40c327652e8b


---

## Technical References

- [Final ESP32 firmware](final_firmware/src/main.cpp)
- [PlatformIO configuration](final_firmware/platformio.ini)
- [Telemetry Lambda](AWS%20Lambdas/telemetry_iot_rule.py)
- [Web backend Lambda](AWS%20Lambdas/webapp_api_backend.py)
- [Device IoT policy](AWS%20Policies/iot_policy_lamp.json)
- [Web backend policy](AWS%20Policies/webapp_backend_policy.json)
- [DynamoDB policy](AWS%20Policies/dynamo_db_policy.json)
- [Remote control panel](remote_control.html)
- [Collected evidence](evidences/)

---

## Project

**PSI5120 — Trabalho Avaliativo Final**  
Escola Politécnica da Universidade de São Paulo (EPUSP)
