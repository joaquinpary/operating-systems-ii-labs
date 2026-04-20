"""
Gateway stress-test module.

Spawns N concurrent clients that each:
  1. Login (POST /login)
  2. Run R random cycles, each cycle picks an action:
     - create+dispatch  (weighted)
     - predict           (weighted)
     - status check      (weighted)
  3. Optionally open a WebSocket session.

Every created shipment is dispatched in the same cycle, so the counts match.

Public API:
    run_stress_test(base_url, num_clients, cycles_range, ...)
"""

from __future__ import annotations

import http.cookiejar
import json
import random
import socket
import threading
import time
import urllib.request
from dataclasses import dataclass, field
from typing import Any

from modules import api_gateway_client as gw


# ── Per-client result ───────────────────────────────────────────────────────

@dataclass
class ClientResult:
    client_id: str = ""
    login_ok: bool = False
    shipments_created: int = 0
    shipments_dispatched: int = 0
    predicts_ok: int = 0
    statuses_ok: int = 0
    ws_ok: bool = False
    total_requests: int = 0
    errors: list[str] = field(default_factory=list)
    elapsed: float = 0.0


# ── Single-client flow ──────────────────────────────────────────────────────

def _make_session() -> urllib.request.OpenerDirector:
    """Create an HTTP opener with a cookie jar to maintain sticky sessions."""
    jar = http.cookiejar.CookieJar()
    return urllib.request.build_opener(
        urllib.request.HTTPCookieProcessor(jar),
        urllib.request.HTTPSHandler(context=gw._SSL_CTX),
    )


def _session_request(
    opener: urllib.request.OpenerDirector,
    method: str,
    url: str,
    token: str | None = None,
    payload: Any = None,
    max_retries: int = 3,
) -> dict:
    """HTTP request using a cookie-aware session (for Traefik sticky sessions).

    Retries automatically on 429 (rate limit) with exponential backoff.
    """
    headers = {"Accept": "application/json"}
    data = None
    if token:
        headers["Authorization"] = f"Bearer {token}"
    if payload is not None:
        data = json.dumps(payload).encode("utf-8")
        headers["Content-Type"] = "application/json"

    for attempt in range(max_retries + 1):
        req = urllib.request.Request(url=url, data=data, headers=headers, method=method)
        try:
            with opener.open(req, timeout=30) as resp:
                charset = resp.headers.get_content_charset("utf-8")
                body = resp.read().decode(charset)
                return json.loads(body) if body else {}
        except urllib.error.HTTPError as exc:
            if exc.code == 429 and attempt < max_retries:
                retry_after = float(exc.headers.get("Retry-After", 1))
                backoff = min(retry_after, 2 ** attempt)
                time.sleep(backoff + random.uniform(0, 0.5))
                continue
            body = exc.read().decode("utf-8", errors="replace")
            try:
                return json.loads(body)
            except json.JSONDecodeError:
                return {"error": body}
        except urllib.error.URLError as exc:
            return {"error": str(exc.reason)}
        except Exception as exc:
            return {"error": str(exc)}
    return {"error": "max retries exceeded"}


def _random_quantities() -> dict[int, int]:
    quantities: dict[int, int] = {}
    for item_id, _ in gw.ITEM_CATALOGUE:
        qty = random.randint(0, 10)
        if qty > 0:
            quantities[item_id] = qty
    return quantities or {1: 1}


def _run_client(
    base_url: str,
    username: str,
    password: str,
    enable_ws: bool,
    req_delay: float = 0.0,
    cycles_min: int = 3,
    cycles_max: int = 8,
) -> ClientResult:
    result = ClientResult(client_id=username)
    session = _make_session()
    t0 = time.monotonic()

    # Track all shipment IDs created during this client's session
    created_shipments: list[str] = []

    try:
        # ── Login ───────────────────────────────────────────────────────
        resp = _session_request(session, "POST", f"{base_url}/login", payload={
            "username": username,
            "password": password,
        })
        result.total_requests += 1
        token = resp.get("token")
        if not token:
            result.errors.append(f"login: {resp.get('error', resp)}")
            result.elapsed = time.monotonic() - t0
            return result
        result.login_ok = True
        if req_delay > 0:
            time.sleep(req_delay)

        # ── Random cycles ───────────────────────────────────────────────
        num_cycles = random.randint(cycles_min, cycles_max)

        # Action weights: create+dispatch=50%, predict=25%, status=25%
        actions = ["ship", "predict", "status"]
        weights = [50, 25, 25]

        for _ in range(num_cycles):
            action = random.choices(actions, weights=weights, k=1)[0]

            if action == "ship":
                # Create shipment
                quantities = _random_quantities()
                items = gw.build_items(quantities)
                resp = _session_request(
                    session, "POST", f"{base_url}/shipments", token, {"items": items},
                )
                result.total_requests += 1
                sid = resp.get("shipment_id") or resp.get("id") or ""
                if "error" in resp:
                    result.errors.append(f"shipment: {resp['error']}")
                else:
                    result.shipments_created += 1
                    if sid:
                        created_shipments.append(sid)
                if req_delay > 0:
                    time.sleep(req_delay)

                # Dispatch it immediately
                if sid:
                    resp = _session_request(
                        session, "POST", f"{base_url}/dispatch", token,
                        {"shipment_id": sid},
                    )
                    result.total_requests += 1
                    if "error" in resp:
                        result.errors.append(f"dispatch: {resp['error']}")
                    else:
                        result.shipments_dispatched += 1
                    if req_delay > 0:
                        time.sleep(req_delay)

            elif action == "predict":
                quantities = _random_quantities()
                items = gw.build_items(quantities)
                resp = _session_request(
                    session, "POST", f"{base_url}/predict", token, {"items": items},
                )
                result.total_requests += 1
                if "error" in resp:
                    result.errors.append(f"predict: {resp['error']}")
                else:
                    result.predicts_ok += 1
                if req_delay > 0:
                    time.sleep(req_delay)

            elif action == "status":
                # Check status of a random previously-created shipment,
                # or get all statuses if none created yet.
                if created_shipments:
                    sid = random.choice(created_shipments)
                    resp = _session_request(
                        session, "GET", f"{base_url}/status/{sid}", token,
                    )
                else:
                    resp = _session_request(
                        session, "GET", f"{base_url}/status", token,
                    )
                result.total_requests += 1
                if "error" in resp:
                    result.errors.append(f"status: {resp['error']}")
                else:
                    result.statuses_ok += 1
                if req_delay > 0:
                    time.sleep(req_delay)

        # ── WebSocket (optional) ────────────────────────────────────────
        if enable_ws and created_shipments:
            try:
                ws_client = gw.open_ws(base_url, token, timeout=5.0)
                ws_client.send_cancel(random.choice(created_shipments))
                try:
                    ws_client.recv_json(timeout=3.0)
                except socket.timeout:
                    pass
                ws_client.close()
                result.ws_ok = True
            except Exception as exc:
                result.errors.append(f"ws: {exc}")

    except Exception as exc:
        result.errors.append(f"unexpected: {exc}")

    result.elapsed = time.monotonic() - t0
    return result


# ── Orchestrator ────────────────────────────────────────────────────────────

def run_stress_test(
    base_url: str,
    num_clients: int,
    enable_ws: bool = True,
    ramp_up_s: float = 0.0,
    req_delay_s: float = 0.0,
    cycles_min: int = 3,
    cycles_max: int = 8,
    credentials_dir: str | None = None,
) -> dict[str, Any]:
    """Run num_clients threads exercising the full gateway flow.

    Each client logs in once, then runs a random number of cycles
    (between cycles_min and cycles_max).  Each cycle randomly picks:
      - create + dispatch  (50%)
      - predict            (25%)
      - status check       (25%)

    Args:
        ramp_up_s:   Total seconds over which to stagger client launches.
        req_delay_s: Pause between consecutive requests within each client.
        cycles_min:  Minimum cycles per client (default 3).
        cycles_max:  Maximum cycles per client (default 8).
    """
    creds_dir = credentials_dir or gw.GATEWAY_CREDS_DIR
    cred_files = gw.list_credentials() if not credentials_dir else _list_conf_files(creds_dir)

    if not cred_files:
        return {"status": "error", "message": f"No credentials found in {creds_dir}"}

    if num_clients > len(cred_files):
        print(f"  Warning: requested {num_clients} clients but only {len(cred_files)} credentials exist.")
        print(f"  Running with {len(cred_files)} clients.\n")
        num_clients = len(cred_files)

    # Load credentials
    clients_creds: list[tuple[str, str]] = []
    for conf_path in cred_files[:num_clients]:
        cred = gw.read_credential(conf_path)
        username = cred.get("username", "")
        password = cred.get("password", "")
        if username and password:
            clients_creds.append((username, password))

    if not clients_creds:
        return {"status": "error", "message": "Could not load any valid credentials"}

    actual = len(clients_creds)
    stagger = ramp_up_s / actual if ramp_up_s > 0 and actual > 1 else 0.0

    print(f"  Launching {actual} clients against {base_url}")
    print(f"  Cycles per client: {cycles_min}–{cycles_max} (random)")
    if ramp_up_s > 0:
        print(f"  Ramp-up: {ramp_up_s:.0f}s  ({1/stagger:.1f} clients/s)" if stagger else "")
    if req_delay_s > 0:
        print(f"  Inter-request delay: {req_delay_s:.2f}s")
    print(f"  WebSocket phase: {'enabled' if enable_ws else 'disabled'}")
    print(f"  Running...\n")

    results: list[ClientResult] = [ClientResult()] * actual
    lock = threading.Lock()
    done_count = 0
    started = time.monotonic()

    def _worker(idx: int, user: str, pwd: str) -> None:
        nonlocal done_count
        r = _run_client(
            base_url, user, pwd, enable_ws, req_delay_s,
            cycles_min=cycles_min, cycles_max=cycles_max,
        )
        with lock:
            results[idx] = r
            done_count += 1
            if done_count % max(1, actual // 10) == 0 or done_count == actual:
                pct = done_count * 100 // actual
                print(f"  Progress: {done_count}/{actual} ({pct}%)")

    threads: list[threading.Thread] = []
    for i, (user, pwd) in enumerate(clients_creds):
        t = threading.Thread(target=_worker, args=(i, user, pwd), daemon=True)
        threads.append(t)
        t.start()
        if stagger > 0 and i < actual - 1:
            time.sleep(stagger)

    for t in threads:
        t.join(timeout=300)

    total_time = time.monotonic() - started

    # Aggregate
    logins = sum(1 for r in results if r.login_ok)
    shipments = sum(r.shipments_created for r in results)
    dispatches = sum(r.shipments_dispatched for r in results)
    predicts = sum(r.predicts_ok for r in results)
    statuses = sum(r.statuses_ok for r in results)
    ws_ok_count = sum(1 for r in results if r.ws_ok)
    total_reqs = sum(r.total_requests for r in results)
    error_count = sum(len(r.errors) for r in results)
    times = [r.elapsed for r in results if r.elapsed > 0]
    avg_time = sum(times) / len(times) if times else 0

    summary = {
        "clients": actual,
        "total_time_s": round(total_time, 2),
        "avg_client_time_s": round(avg_time, 2),
        "total_requests": total_reqs,
        "login_ok": logins,
        "shipments_created": shipments,
        "shipments_dispatched": dispatches,
        "predicts_ok": predicts,
        "statuses_ok": statuses,
        "ws_ok": ws_ok_count,
        "total_errors": error_count,
    }

    # Print summary
    _print_summary(summary, results)

    return summary


def _list_conf_files(directory: str) -> list[str]:
    import os
    if not os.path.isdir(directory):
        return []
    return sorted(
        os.path.join(directory, f)
        for f in os.listdir(directory)
        if f.endswith(".conf")
    )


def _print_summary(summary: dict[str, Any], results: list[ClientResult]) -> None:
    n = summary["clients"]
    total_reqs = summary["total_requests"]
    ship = summary["shipments_created"]
    disp = summary["shipments_dispatched"]
    pred = summary["predicts_ok"]
    stat = summary["statuses_ok"]
    errs = summary["total_errors"]
    ws = summary["ws_ok"]

    print(f"  ╔══════════════════════════════════════════════╗")
    print(f"  ║           Stress Test Results                 ║")
    print(f"  ╠══════════════════════════════════════════════╣")
    print(f"  ║  Clients:        {n:>6}                       ║")
    print(f"  ║  Total requests: {total_reqs:>6}                       ║")
    print(f"  ║  Total time:     {summary['total_time_s']:>6.1f}s                      ║")
    print(f"  ║  Avg/client:     {summary['avg_client_time_s']:>6.2f}s                      ║")
    print(f"  ╠══════════════════════════════════════════════╣")
    print(f"  ║  Login:          {summary['login_ok']:>5}/{n:<5} {'✓' if summary['login_ok'] == n else '✗'}               ║")
    print(f"  ║  Shipments:      {ship:>5}                          ║")
    print(f"  ║  Dispatched:     {disp:>5}  {'✓' if ship == disp else '✗ ('+str(ship-disp)+' missing)'}              ║")
    print(f"  ║  Predicts:       {pred:>5}                          ║")
    print(f"  ║  Status checks:  {stat:>5}                          ║")
    print(f"  ║  WebSocket:      {ws:>5}/{n:<5} {'✓' if ws == n else '—'}               ║")
    print(f"  ╠══════════════════════════════════════════════╣")
    print(f"  ║  Errors:         {errs:>5}                          ║")
    print(f"  ╚══════════════════════════════════════════════╝")

    # Show first few errors if any
    error_samples: list[str] = []
    for r in results:
        for e in r.errors:
            error_samples.append(f"    [{r.client_id}] {e}")
            if len(error_samples) >= 10:
                break
        if len(error_samples) >= 10:
            break

    if error_samples:
        print(f"\n  First errors (up to 10):")
        for line in error_samples:
            print(line)
        remaining = errs - len(error_samples)
        if remaining > 0:
            print(f"    ... and {remaining} more")
