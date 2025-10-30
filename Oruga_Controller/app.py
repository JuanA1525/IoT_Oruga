import json
import logging
import os
from typing import Dict

import requests
from flask import Flask, jsonify, request, send_from_directory


ORION_BASE_URL = os.getenv("ORION_BASE_URL", "http://orion:1026")
ENTITY_ID = os.getenv("ENTITY_ID", "oruga")
ORION_ATTRS_URL = f"{ORION_BASE_URL}/v2/entities/{ENTITY_ID}/attrs"


def create_app() -> Flask:
    app = Flask(__name__)

    # Logging config
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s | %(levelname)s | %(name)s | %(message)s",
    )
    app.logger.setLevel(logging.INFO)

    app.logger.info("Starting oruga-controller")
    app.logger.info("ORION_BASE_URL=%s", ORION_BASE_URL)
    app.logger.info("ENTITY_ID=%s", ENTITY_ID)

    @app.route("/")
    def index():
        # Serve the controller page
        return send_from_directory(os.path.dirname(__file__), "oruga_controller.html")

    @app.route("/healthz")
    def healthz():
        return jsonify({"status": "ok"})

    @app.post("/api/move")
    def move():
        try:
            payload = request.get_json(silent=True) or {}
            direction = (payload.get("direction") or "").lower()
            allowed = {"forward", "backward", "left", "right", "stop"}
            if direction not in allowed:
                app.logger.warning("/api/move invalid direction=%s", direction)
                return (
                    jsonify({"ok": False, "error": "invalid direction"}),
                    400,
                )

            body = {
                "estado": {
                    "value": direction,
                    "type": "String",
                }
            }
            headers = {"Content-Type": "application/json"}

            app.logger.info("/api/move -> Orion PATCH %s body=%s", ORION_ATTRS_URL, body)
            r = requests.patch(
                ORION_ATTRS_URL,
                headers=headers,
                data=json.dumps(body),
                timeout=5,
            )
            app.logger.info("Orion response: %s %s", r.status_code, r.text[:300])

            if 200 <= r.status_code < 300:
                return jsonify({"ok": True, "direction": direction})
            return (
                jsonify(
                    {
                        "ok": False,
                        "status": r.status_code,
                        "body": r.text,
                    }
                ),
                r.status_code,
            )
        except requests.RequestException as e:
            app.logger.exception("Error contacting Orion: %s", e)
            return jsonify({"ok": False, "error": str(e)}), 502
        except Exception as e:  # noqa: BLE001
            app.logger.exception("/api/move unexpected error: %s", e)
            return jsonify({"ok": False, "error": "internal error"}), 500

    @app.post("/api/speed")
    def speed():
        try:
            payload = request.get_json(silent=True) or {}
            if not {"left_speed", "right_speed"}.issubset(payload.keys()):
                app.logger.warning("/api/speed requires left_speed and right_speed")
                return (
                    jsonify({"ok": False, "error": "left_speed and right_speed required"}),
                    400,
                )

            def clamp(v: int) -> int:
                return max(0, min(255, int(v)))

            left = clamp(payload.get("left_speed", 0))
            right = clamp(payload.get("right_speed", 0))

            body: Dict[str, Dict[str, object]] = {
                "left_speed": {"value": left, "type": "int"},
                "right_speed": {"value": right, "type": "int"},
            }

            headers = {"Content-Type": "application/json"}
            app.logger.info("/api/speed -> Orion PATCH %s body=%s", ORION_ATTRS_URL, body)
            r = requests.patch(
                ORION_ATTRS_URL,
                headers=headers,
                data=json.dumps(body),
                timeout=5,
            )
            app.logger.info("Orion response: %s %s", r.status_code, r.text[:300])

            if 200 <= r.status_code < 300:
                return jsonify({"ok": True, "left_speed": left, "right_speed": right})
            return (
                jsonify(
                    {
                        "ok": False,
                        "status": r.status_code,
                        "body": r.text,
                    }
                ),
                r.status_code,
            )
        except requests.RequestException as e:
            app.logger.exception("Error contacting Orion: %s", e)
            return jsonify({"ok": False, "error": str(e)}), 502
        except Exception as e:  # noqa: BLE001
            app.logger.exception("/api/speed unexpected error: %s", e)
            return jsonify({"ok": False, "error": "internal error"}), 500

    return app


app = create_app()
