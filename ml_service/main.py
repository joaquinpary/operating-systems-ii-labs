"""
LoRa-CHADS ML Prediction Service.

Lightweight HTTP server that estimates shipment ETA and recommended box size
based on item quantities.  Runs on port 9000.

Endpoint:
    POST /predict
    Body:  {"items": [{"item_id": 1, "quantity": 5}, ...]}
    Reply: {"eta_hours": ..., "box_size": "...", "confidence": ...}
"""

import json
import math
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer

PORT = 9000

# ── Heuristic weights ───────────────────────────────────────────────────────
# Volume units per item type (arbitrary scale).
ITEM_VOLUME = {
    1: 2.0,   # food
    2: 3.0,   # water (heavy/bulky)
    3: 0.5,   # medicine (small)
    4: 4.0,   # tools
    5: 3.5,   # guns
    6: 1.0,   # ammo
}

# Box size thresholds (total volume units).
BOX_THRESHOLDS = [
    (10,  "small"),
    (30,  "medium"),
    (50,  "large"),
]

# Base ETA in hours; scales with total quantity.
BASE_ETA = 24.0
ETA_PER_UNIT = 0.5
MAX_ETA = 168.0  # 7 days cap


def _total_volume(items: list[dict]) -> float:
    vol = 0.0
    for item in items:
        item_id = item.get("item_id", 0)
        qty = item.get("quantity", 0)
        vol += ITEM_VOLUME.get(item_id, 1.0) * max(qty, 0)
    return vol


def _total_quantity(items: list[dict]) -> int:
    return sum(max(item.get("quantity", 0), 0) for item in items)


def predict(items: list[dict]) -> dict:
    volume = _total_volume(items)
    total_qty = _total_quantity(items)

    # Box size by volume.
    box_size = "extra-large"
    for threshold, label in BOX_THRESHOLDS:
        if volume <= threshold:
            box_size = label
            break

    # ETA: base + per-unit scaling, clamped.
    eta = BASE_ETA + ETA_PER_UNIT * total_qty
    eta = min(eta, MAX_ETA)
    eta = math.ceil(eta)

    # Confidence: higher quantity → lower confidence.
    confidence = round(max(0.5, 1.0 - total_qty / 200.0), 2)

    return {
        "eta_hours": eta,
        "box_size": box_size,
        "confidence": confidence,
    }


# ── HTTP handler ────────────────────────────────────────────────────────────

class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        if self.path != "/predict":
            self._reply(404, {"error": "not found"})
            return

        length = int(self.headers.get("Content-Length", 0))
        if length == 0:
            self._reply(400, {"error": "empty body"})
            return

        try:
            body = json.loads(self.rfile.read(length))
        except (json.JSONDecodeError, UnicodeDecodeError):
            self._reply(400, {"error": "invalid JSON"})
            return

        items = body.get("items")
        if not isinstance(items, list) or len(items) == 0:
            self._reply(400, {"error": "items array required"})
            return

        result = predict(items)
        self._reply(200, result)

    def do_GET(self):
        if self.path == "/health":
            self._reply(200, {"status": "ok"})
            return
        self._reply(404, {"error": "not found"})

    def _reply(self, code: int, body: dict):
        payload = json.dumps(body).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    # Silence per-request log lines.
    def log_message(self, fmt, *args):
        pass


if __name__ == "__main__":
    server = HTTPServer(("0.0.0.0", PORT), Handler)
    print(f"ml_service listening on :{PORT}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    server.server_close()
    sys.exit(0)
