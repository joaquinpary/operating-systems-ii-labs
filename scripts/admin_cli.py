#!/usr/bin/env python3
"""
Admin CLI for LoRa-CHADS server.
Connects via TCP, authenticates as an existing client, then sends
CLI_TO_SERVER__ADMIN_COMMAND messages to query the server.

Usage:
  python3 admin_cli.py              # use cli_admin credentials (default)
  python3 admin_cli.py <hub|wh> [n] # authenticate as a regular client

  Examples:
    python3 admin_cli.py             # uses config/clients/cli_admin.conf
    python3 admin_cli.py hub         # authenticate as first HUB in config/
    python3 admin_cli.py wh 3        # authenticate as client #3 (warehouse)

Admin commands:
  help                                  - Show available server commands
  clients [active true|false] [page N]  - List clients (default: active true)
  inventory <client_id>                 - Show inventory for a client
  transactions [<client_id>|all] [page N] - Transaction history
  n / next                              - Next page of last query
  p / prev                              - Previous page of last query
  quit                                  - Disconnect and exit
"""

import json
import os
import glob
import socket
import sys
import threading
from datetime import datetime, timezone

SERVER_HOST = '127.0.0.1'
SERVER_PORT = 9999
BUF_SIZE = 1024

# Loaded from config
CLIENT_ID = None
CLIENT_TYPE = None
USERNAME = None
PASSWORD = None

# Runtime state
sock = None
running = True
authenticated = False

# Pagination state — remembered so 'n'/'p' can navigate
last_command = None
last_args = ""
current_page = 1
total_pages = 1


# ── config helpers ──────────────────────────────────────────────────────────

def load_conf(path):
    conf = {}
    with open(path) as f:
        for line in f:
            line = line.strip()
            if '=' in line:
                key, value = line.split('=', 1)
                conf[key.strip()] = value.strip()
    return conf


def get_clients_dir():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    return os.path.join(project_root, 'config', 'clients')


def find_cli_conf():
    clients_dir = get_clients_dir()
    path = os.path.join(clients_dir, 'cli_admin.conf')
    if os.path.exists(path):
        return load_conf(path), path
    print(f"[ERROR] {path} not found — run gen_credentials.py first")
    sys.exit(1)


def find_client_conf(role, number=None):
    clients_dir = get_clients_dir()

    target_type = 'WAREHOUSE' if role in ('wh', 'warehouse') else 'HUB'

    if number is not None:
        path = os.path.join(clients_dir, f'client_{number:04d}.conf')
        if os.path.exists(path):
            conf = load_conf(path)
            return conf, path
        print(f"[ERROR] {path} not found")
        sys.exit(1)

    for path in sorted(glob.glob(os.path.join(clients_dir, '*.conf'))):
        conf = load_conf(path)
        if conf.get('type') == target_type:
            return conf, path

    print(f"[ERROR] No {target_type} client found in {clients_dir}")
    sys.exit(1)


# ── protocol helpers ────────────────────────────────────────────────────────

def calculate_checksum(json_str):
    return f"{sum(json_str.encode()) % 0xFFFFFF:X}"


def create_message(msg_type, payload):
    timestamp = datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%S.%f')[:-3] + 'Z'
    msg = {
        "msg_type": msg_type,
        "source_role": CLIENT_TYPE,
        "source_id": CLIENT_ID,
        "target_role": "SERVER",
        "target_id": "SERVER",
        "timestamp": timestamp,
        "payload": payload,
    }
    json_str = json.dumps(msg, separators=(',', ':'))
    msg["checksum"] = calculate_checksum(json_str)
    return json.dumps(msg, separators=(',', ':'))


def send_message(raw):
    buf = bytearray(BUF_SIZE)
    encoded = raw.encode()
    buf[:len(encoded)] = encoded
    sock.sendall(buf)


# ── receiver thread ─────────────────────────────────────────────────────────

def listener():
    global running, authenticated
    while running:
        try:
            sock.settimeout(1.0)
            data = sock.recv(BUF_SIZE)
            if not data:
                print("\n[INFO] Server closed connection")
                running = False
                break

            text = data.rstrip(b'\x00').decode()
            try:
                obj = json.loads(text)
            except json.JSONDecodeError:
                print(f"\n[RAW] {text}")
                print("> ", end='', flush=True)
                continue

            msg_type = obj.get("msg_type", "")

            # Auth response
            if "AUTH_RESPONSE" in msg_type:
                payload = obj.get("payload", {})
                code = payload.get("status_code", 0)
                if code == 200:
                    authenticated = True
                    print("\n[OK] Authenticated — admin CLI ready")
                else:
                    print(f"\n[FAIL] Auth rejected (code {code}): {payload.get('message', '')}")
                print("> ", end='', flush=True)
                continue

            # CLI response from admin plugin (raw JSON blob)
            # The server wraps the admin_cli response in a SERVER_TO_* message,
            # but it may also be the raw JSON from the plugin. Try both.
            payload = obj.get("payload", {})

            # If this looks like an admin response (has "status" key), pretty-print it
            if isinstance(payload, dict) and "status" in payload:
                print_admin_response(payload)
            elif "status" in obj:
                print_admin_response(obj)
            else:
                # Generic server message
                print(f"\n[SERVER] {json.dumps(obj, indent=2)}")

            print("> ", end='', flush=True)

        except socket.timeout:
            continue
        except Exception as e:
            if running:
                print(f"\n[ERROR] {e}")
            break


def print_admin_response(resp):
    global current_page, total_pages

    status = resp.get("status", "?")
    if status == "error":
        print(f"\n[ERROR] {resp.get('message', 'unknown error')}")
        return

    # Update pagination state from response
    if "page" in resp:
        current_page = resp["page"]
        total_pages = resp.get("total_pages", 1)

    skip_keys = {"status", "page", "total_pages", "total"}

    # Pretty-print the data part
    for key, value in resp.items():
        if key in skip_keys:
            continue
        if isinstance(value, list):
            print(f"\n[{key.upper()}] ({len(value)} rows)")
            for item in value:
                if isinstance(item, dict):
                    parts = [f"{k}={v}" for k, v in item.items()]
                    print(f"  {', '.join(parts)}")
                else:
                    print(f"  {item}")
        elif isinstance(value, dict):
            print(f"\n[{key.upper()}]")
            for k, v in value.items():
                print(f"  {k}: {v}")
        else:
            print(f"\n[{key.upper()}] {value}")

    # Show pagination footer
    if "page" in resp:
        total = resp.get("total", "?")
        print(f"\n  Page {current_page}/{total_pages} ({total} total) — 'n' next, 'p' prev")


# ── admin commands ──────────────────────────────────────────────────────────

def _replace_page(args, new_page):
    """Replace or append 'page N' in an args string."""
    import re
    if re.search(r'\bpage\s+\d+', args):
        return re.sub(r'\bpage\s+\d+', f'page {new_page}', args)
    suffix = f" page {new_page}" if args else f"page {new_page}"
    return args + suffix


def send_admin_command(command, args=""):
    msg_type = "CLI_TO_SERVER__ADMIN_COMMAND"
    payload = {"command": command, "args": args}
    raw = create_message(msg_type, payload)
    send_message(raw)


def send_auth():
    if CLIENT_TYPE == "CLI":
        msg_type = "CLI_TO_SERVER__AUTH_REQUEST"
    elif CLIENT_TYPE == "HUB":
        msg_type = "HUB_TO_SERVER__AUTH_REQUEST"
    else:
        msg_type = "WAREHOUSE_TO_SERVER__AUTH_REQUEST"
    payload = {"username": USERNAME, "password": PASSWORD}
    raw = create_message(msg_type, payload)
    send_message(raw)
    print(f"[SENT] {msg_type}")


# ── main ────────────────────────────────────────────────────────────────────

def main():
    global CLIENT_ID, CLIENT_TYPE, USERNAME, PASSWORD
    global sock, running
    global last_command, last_args

    if len(sys.argv) < 2:
        # Default: use cli_admin credentials
        conf, conf_path = find_cli_conf()
    else:
        role = sys.argv[1].lower()
        number = int(sys.argv[2]) if len(sys.argv) > 2 else None
        conf, conf_path = find_client_conf(role, number)

    CLIENT_ID = conf['username']
    CLIENT_TYPE = conf['type']
    USERNAME = conf['username']
    PASSWORD = conf['password']

    print(f"[CONFIG] {conf_path}")
    print(f"[CLIENT] id={CLIENT_ID} type={CLIENT_TYPE}")

    # Connect
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((SERVER_HOST, SERVER_PORT))
    print(f"[CONNECTED] {SERVER_HOST}:{SERVER_PORT}")

    # Start listener
    recv_thread = threading.Thread(target=listener, daemon=True)
    recv_thread.start()

    # Authenticate
    send_auth()

    # REPL
    try:
        while running:
            try:
                line = input("> ").strip()
            except EOFError:
                break
            if not line:
                continue

            parts = line.split(maxsplit=1)
            cmd = parts[0].lower()
            args = parts[1] if len(parts) > 1 else ""

            if cmd == "quit":
                break
            elif cmd in ("n", "next"):
                if last_command and current_page < total_pages:
                    new_args = _replace_page(last_args, current_page + 1)
                    send_admin_command(last_command, new_args)
                    last_args = new_args
                else:
                    print("No next page" if last_command else "No previous query")
            elif cmd in ("p", "prev"):
                if last_command and current_page > 1:
                    new_args = _replace_page(last_args, current_page - 1)
                    send_admin_command(last_command, new_args)
                    last_args = new_args
                else:
                    print("No previous page" if last_command else "No previous query")
            elif cmd in ("help", "clients", "inventory", "transactions"):
                if not authenticated:
                    print("[WARN] Not authenticated yet — server may reject the command")
                last_command = cmd
                last_args = args
                send_admin_command(cmd, args)
            else:
                print(f"Unknown command: {cmd}")
                print("Commands: help, clients, inventory, transactions, n, p, quit")
    except KeyboardInterrupt:
        pass
    finally:
        running = False
        sock.close()
        print("\n[DISCONNECTED]")


if __name__ == '__main__':
    main()
