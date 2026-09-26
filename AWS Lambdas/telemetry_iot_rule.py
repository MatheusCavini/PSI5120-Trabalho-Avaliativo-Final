"""
PSI5120 - Trabalho Final - Smart Lamp Retrofit
Lambda invocada pela AWS IoT Rule a cada mensagem de telemetria.

Fluxo: MQTT (telemetry) -> IoT Rule -> esta Lambda -> DynamoDB (PutItem)

Duas relacoes de autorizacao distintas e independentes tornam esse
pipeline funcional:
  1. AWS IoT -> Lambda: controlada pela resource-based policy da
     funcao (normalmente criada automaticamente pelo Console ao
     adicionar a acao Lambda na Rule).
  2. Lambda -> DynamoDB: controlada pela execution role da Lambda
     (inline policy com dynamodb:PutItem, configurada no proximo passo).
"""

import json
import os
from decimal import Decimal

import boto3

dynamodb = boto3.resource("dynamodb")
table = dynamodb.Table(os.environ["TABLE_NAME"])


def lambda_handler(event, context):
    print(json.dumps({
        "phase": "iot_rule_received",
        "request_id": context.aws_request_id,
        "event": event,
    }, ensure_ascii=False, sort_keys=True))

    item = {
        "device_id": str(event["device_id"]),
        "sequence": int(event["sequence"]),
        "timestamp": str(event["timestamp"]),
        "mode": str(event["mode"]),
        "lamp_state": str(event["lamp_state"]),
        "escuro": bool(event["escuro"]),
        "presence_proxima": bool(event["presence_proxima"]),
        "light_raw": int(event["light_raw"]),
    }

    # distance_cm pode chegar como JSON null (sem eco valido do HC-SR04
    # naquela leitura). O boto3 resource nao aceita None diretamente
    # como valor de atributo, entao so incluimos o campo quando existe.
    distance_cm = event.get("distance_cm")
    if distance_cm is not None:
        item["distance_cm"] = Decimal(str(distance_cm))

    table.put_item(Item=item)

    print(json.dumps({
        "phase": "dynamodb_put_ok",
        "request_id": context.aws_request_id,
        "device_id": item["device_id"],
        "sequence": item["sequence"],
    }, ensure_ascii=False, sort_keys=True))

    return {
        "statusCode": 200,
        "processed": True,
        "device_id": item["device_id"],
        "sequence": item["sequence"],
    }