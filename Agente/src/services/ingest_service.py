import base64
import hashlib
import json
import logging
import os
import time
from typing import Dict, Tuple, Any

import requests
from Crypto.Cipher import AES

logger = logging.getLogger(__name__)

# Configuration
ORION_BASE = os.getenv("ORION_BASE", "http://54.81.22.123:1026")
GPS_ENTITY_ID = os.getenv("GPS_ENTITY_ID", "gps_oruga")
TEMP_ENTITY_ID = os.getenv("TEMP_ENTITY_ID", "temp_oruga")
HUM_ENTITY_ID = os.getenv("HUM_ENTITY_ID", "hum_oruga")
AGENT_TOKEN_ENV = "AGENT_TOKEN"


class IngestError(Exception):
    pass


def _get_token_or_raise() -> str:
    token = os.getenv(AGENT_TOKEN_ENV)
    if not token:
        raise IngestError(f"Server misconfigured: env var {AGENT_TOKEN_ENV} is not set")
    return token


def decrypt_agent_payload(body: Dict[str, Any]) -> Dict[str, Any]:
    # body is expected to contain: v, iv, tag, ct
    if not isinstance(body, dict):
        raise ValueError("Request body must be a JSON object")

    if body.get("v") != 1:
        raise ValueError("Unsupported version")

    token = _get_token_or_raise()

    try:
        key = hashlib.sha256(token.encode("utf-8")).digest()
        iv = base64.b64decode(body["iv"])  # nonce for GCM
        tag = base64.b64decode(body["tag"])  # auth tag
        ct = base64.b64decode(body["ct"])   # ciphertext

        cipher = AES.new(key, AES.MODE_GCM, nonce=iv)
        plaintext = cipher.decrypt_and_verify(ct, tag)
        data = json.loads(plaintext.decode("utf-8"))
        return data
    except KeyError as e:
        raise ValueError(f"Missing field in encrypted payload: {e}")
    except Exception as e:
        raise ValueError(f"Decryption or parsing failed: {e}")


def validate_data(data: Dict[str, Any]) -> Tuple[bool, Dict[str, str]]:
    required = [
        "node", "ts_ms", "lat", "lon", "alt_m", "speed_mps",
        "course_deg", "hdop", "sats", "fix_age_ms", "temp_c", "hum_pct"
    ]
    errors: Dict[str, str] = {}

    for k in required:
        if k not in data:
            errors[k] = "missing"
        elif data[k] is None:
            errors[k] = "null"

    if errors:
        return False, errors

    # Type coercion / validation
    try:
        node = str(data["node"]).strip()
        if not node:
            errors["node"] = "empty"

        ts_ms = int(data["ts_ms"])
        if ts_ms < 0:
            errors["ts_ms"] = "negative"

        lat = float(data["lat"])
        if not (-90.0 <= lat <= 90.0):
            errors["lat"] = "out_of_range"

        lon = float(data["lon"])
        if not (-180.0 <= lon <= 180.0):
            errors["lon"] = "out_of_range"

        alt_m = float(data["alt_m"])
        if not (-500.0 <= alt_m <= 10000.0):
            errors["alt_m"] = "out_of_range"

        speed_mps = float(data["speed_mps"])
        if not (0.0 <= speed_mps <= 100.0):
            errors["speed_mps"] = "out_of_range"

        course_deg = float(data["course_deg"])
        if not (0.0 <= course_deg < 360.0):
            errors["course_deg"] = "out_of_range"

        hdop = float(data["hdop"])
        if not (0.0 < hdop <= 50.0):
            errors["hdop"] = "out_of_range"

        sats = int(data["sats"])
        if not (0 <= sats <= 64):
            errors["sats"] = "out_of_range"

        fix_age_ms = int(data["fix_age_ms"])
        if fix_age_ms < 0:
            errors["fix_age_ms"] = "negative"

        temp_c = float(data["temp_c"])
        if not (-50.0 <= temp_c <= 85.0):
            errors["temp_c"] = "out_of_range"

        hum_pct = float(data["hum_pct"])
        if not (0.0 <= hum_pct <= 100.0):
            errors["hum_pct"] = "out_of_range"
    except (TypeError, ValueError) as e:
        errors["type"] = f"invalid_type_or_cast: {e}"

    return (len(errors) == 0), errors


def _patch_with_retries(url: str, payload: Dict[str, Any], headers: Dict[str, str], timeout_s: float = 3.0, retries: int = 2) -> Tuple[bool, int, str]:
    last_status = 0
    last_text = ""
    for attempt in range(retries + 1):
        try:
            resp = requests.patch(url, json=payload, headers=headers, timeout=timeout_s)
            last_status = resp.status_code
            last_text = resp.text
            if 200 <= resp.status_code < 300:
                return True, resp.status_code, resp.text
            # retry on 5xx
            if 500 <= resp.status_code < 600 and attempt < retries:
                time.sleep(0.5 * (2 ** attempt))
                continue
            return False, resp.status_code, resp.text
        except requests.RequestException as e:
            last_text = str(e)
            if attempt < retries:
                time.sleep(0.5 * (2 ** attempt))
                continue
            return False, last_status or 0, last_text
    return False, last_status, last_text


def build_orion_payloads(data: Dict[str, Any]) -> Dict[str, Dict[str, Any]]:
    gps_payload = {
        "node": {"value": data["node"]},
        "ts_ms": {"value": int(data["ts_ms"])},
        "lat": {"value": float(data["lat"])},
        "lon": {"value": float(data["lon"])},
        "alt_m": {"value": float(data["alt_m"])},
        "speed_mps": {"value": float(data["speed_mps"])},
        "course_deg": {"value": float(data["course_deg"])},
        "hdop": {"value": float(data["hdop"])},
        "sats": {"value": int(data["sats"])},
        "fix_age_ms": {"value": int(data["fix_age_ms"])},
    }

    temp_payload = {
        "node": {"value": data["node"]},
        "ts_ms": {"value": int(data["ts_ms"])},
        "temp": {"value": float(data["temp_c"])},
    }

    hum_payload = {
        "node": {"value": data["node"]},
        "ts_ms": {"value": int(data["ts_ms"])},
        "hum": {"value": float(data["hum_pct"])},
    }

    return {
        "gps": gps_payload,
        "temp": temp_payload,
        "hum": hum_payload,
    }


def post_to_orion(data: Dict[str, Any]) -> Dict[str, Any]:
    headers = {
        "Content-Type": "application/json",
    }

    payloads = build_orion_payloads(data)

    endpoints = {
        "gps": f"{ORION_BASE}/v2/entities/{GPS_ENTITY_ID}/attrs",
        "temp": f"{ORION_BASE}/v2/entities/{TEMP_ENTITY_ID}/attrs",
        "hum": f"{ORION_BASE}/v2/entities/{HUM_ENTITY_ID}/attrs",
    }

    results = {}
    success_count = 0

    for key in ["gps", "temp", "hum"]:
        url = endpoints[key]
        payload = payloads[key]
        ok, status, text = _patch_with_retries(url, payload, headers)
        results[key] = {
            "ok": ok,
            "status": status,
            "response": text,
        }
        if ok:
            success_count += 1
        level = logging.INFO if ok else logging.WARNING
        logger.log(level, f"Orion PATCH {key} -> {status}")

    overall = {
        "all_ok": success_count == 3,
        "any_ok": success_count > 0,
        "success_count": success_count,
        "results": results,
    }
    return overall


def process_ingest(encrypted_body: Dict[str, Any]) -> Dict[str, Any]:
    logger.debug("Processing ingest request")
    data = decrypt_agent_payload(encrypted_body)
    valid, errors = validate_data(data)
    if not valid:
        raise IngestError(json.dumps({"validation_errors": errors}))

    outcome = post_to_orion(data)
    return {
        "node": data.get("node"),
        "ts_ms": data.get("ts_ms"),
        "orion": outcome,
    }
