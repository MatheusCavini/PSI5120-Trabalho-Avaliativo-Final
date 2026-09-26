"""
PSI5120 - Trabalho Final - Smart Lamp Retrofit
Backend do webapp de controle (invocado via API Gateway HTTP API).

Rotas esperadas (API Gateway HTTP API, payload format 2.0):
  GET  /shadow    -> retorna o documento atual do Device Shadow
  POST /shadow    -> atualiza o "desired" do Shadow (body: {"desired": {...}})
  POST /identify  -> publica o comando stateless "identify"

Duas relacoes de autorizacao distintas tornam este backend funcional
(mesmo padrao conceitual da Lambda da Fase 4, agora no sentido
cloud-to-device em vez de device-to-cloud):
  1. API Gateway -> Lambda: permissao de invocacao (configurada
     automaticamente pelo Console ao criar a integracao).
  2. Lambda -> AWS IoT Data Plane: controlada pela execution role da
     Lambda (inline policy com iot:GetThingShadow, iot:UpdateThingShadow
     e iot:Publish, configurada em passo separado).
"""

import json
import os

import boto3

IOT_DATA_ENDPOINT = os.environ["IOT_DATA_ENDPOINT"]  # ex: xxxxxxxxxxxxx-ats.iot.us-east-1.amazonaws.com
THING_NAME = os.environ["THING_NAME"]
COMMAND_TOPIC = os.environ["COMMAND_TOPIC"]

iot_data = boto3.client("iot-data", endpoint_url=f"https://{IOT_DATA_ENDPOINT}")

CORS_HEADERS = {
    "Access-Control-Allow-Origin": "*",
    "Access-Control-Allow-Headers": "Content-Type",
    "Access-Control-Allow-Methods": "GET,POST,OPTIONS",
}


def _response(status_code, body_dict):
    return {
        "statusCode": status_code,
        "headers": {**CORS_HEADERS, "Content-Type": "application/json"},
        "body": json.dumps(body_dict, ensure_ascii=False),
    }


def lambda_handler(event, context):
    route = event.get("routeKey", "")
    method = event.get("requestContext", {}).get("http", {}).get("method", "")

    # Fallback defensivo: normalmente o preflight CORS e tratado pela
    # propria configuracao de CORS do API Gateway, sem invocar a Lambda.
    if method == "OPTIONS":
        return _response(200, {"ok": True})

    if route == "GET /shadow":
        resp = iot_data.get_thing_shadow(thingName=THING_NAME)
        payload = json.loads(resp["payload"].read())
        return _response(200, payload)

    if route == "POST /shadow":
        body = json.loads(event.get("body") or "{}")
        desired = body.get("desired")
        if not isinstance(desired, dict) or not desired:
            return _response(400, {"error": "corpo deve conter um objeto 'desired' nao vazio"})

        shadow_payload = json.dumps({"state": {"desired": desired}}).encode("utf-8")
        iot_data.update_thing_shadow(thingName=THING_NAME, payload=shadow_payload)
        return _response(200, {"ok": True, "desired": desired})

    if route == "POST /identify":
        command_payload = json.dumps({"command": "identify"}).encode("utf-8")
        iot_data.publish(topic=COMMAND_TOPIC, qos=0, payload=command_payload)
        return _response(200, {"ok": True})

    return _response(404, {"error": f"rota desconhecida: {route}"})