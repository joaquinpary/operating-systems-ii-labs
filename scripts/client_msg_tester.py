#!/usr/bin/env python3
"""
Interactive Client Mock for testing LoRa-CHADS server
Connects via TCP IPv4 to 127.0.0.1:9999
Loads credentials from config/clients/*.conf

Usage:
  python3 client_msg_tester.py <hub|wh> [client_number]

  Examples:
    python3 client_msg_tester.py hub        # picks first HUB from config/clients/
    python3 client_msg_tester.py wh         # picks first WAREHOUSE from config/clients/
    python3 client_msg_tester.py hub 2      # uses client config #2 that is a HUB
    python3 client_msg_tester.py wh 1       # uses client config #1 that is a WAREHOUSE

Commands:
  auth         - Send authentication request
  ack          - Send ACK for last received message
  keepalive    - Send keepalive message
  inventory    - Send inventory update
  stock        - Send stock request (Hub only)
  shipment     - Send shipment notice (Warehouse only)
  replenish    - Send replenish request (Warehouse only)
  receipt      - Send stock receipt confirmation
  quit         - Exit
"""

import socket
import threading
import json
import time
import sys
import os
import glob
from datetime import datetime, timezone

# Fixed connection config
SERVER_HOST = '127.0.0.1'
SERVER_PORT = 9999
PROTOCOL = 'tcp'

# Will be loaded from .conf
CLIENT_ID = None
CLIENT_TYPE = None
USERNAME = None
PASSWORD = None

# Global state
sock = None
last_received_timestamp = None
running = True


def load_conf(path):
    """Parse a .conf file and return a dict of key=value pairs"""
    conf = {}
    with open(path) as f:
        for line in f:
            line = line.strip()
            if '=' in line:
                key, value = line.split('=', 1)
                conf[key.strip()] = value.strip()
    return conf


def find_client_conf(role, number=None):
    """Find a matching .conf file from config/clients/.
    role: 'hub' or 'wh'/'warehouse'
    number: optional client number (e.g. 1 -> client_0001)
    """
    # Determine the script's location to find the config dir
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    clients_dir = os.path.join(project_root, 'config', 'clients')

    target_type = 'WAREHOUSE' if role in ('wh', 'warehouse') else 'HUB'

    if number is not None:
        # Try specific client number
        path = os.path.join(clients_dir, f'client_{number:04d}.conf')
        if os.path.exists(path):
            conf = load_conf(path)
            if conf.get('type') == target_type:
                return conf, path
            else:
                print(f"[WARN] client_{number:04d} is {conf.get('type')}, not {target_type}. Using it anyway.")
                return conf, path
        else:
            print(f"[ERROR] {path} not found")
            sys.exit(1)

    # Pick first matching type
    conf_files = sorted(glob.glob(os.path.join(clients_dir, '*.conf')))
    for path in conf_files:
        conf = load_conf(path)
        if conf.get('type') == target_type:
            return conf, path

    print(f"[ERROR] No {target_type} client found in {clients_dir}")
    sys.exit(1)


def calculate_checksum(json_str):
    """Calculate simple checksum (sum of bytes % 0xFFFFFF)"""
    checksum = sum(json_str.encode()) % 0xFFFFFF
    return f"{checksum:X}"


def create_message(msg_type, payload):
    """Create a message with proper structure"""
    timestamp = datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%S.%f')[:-3] + 'Z'
    
    msg = {
        "msg_type": msg_type,
        "source_role": CLIENT_TYPE,
        "source_id": CLIENT_ID,
        "target_role": "SERVER",
        "target_id": "SERVER",
        "timestamp": timestamp,
        "payload": payload
    }
    
    json_str = json.dumps(msg, separators=(',', ':'))
    checksum = calculate_checksum(json_str)
    msg["checksum"] = checksum
    
    return json.dumps(msg, separators=(',', ':'))


def listen_for_responses():
    """Thread function to listen for server responses"""
    global last_received_timestamp, running
    
    while running:
        try:
            sock.settimeout(1.0)
            
            # TCP: server sends 1024 bytes fixed buffer
            data = sock.recv(1024)
            if not data:
                print("\n[INFO] Server closed connection")
                running = False
                break
            
            message = data.rstrip(b'\x00').decode()
            
            print(f"\n{'='*80}")
            print(f"[RECEIVED]")
            
            try:
                msg_obj = json.loads(message)
                print(json.dumps(msg_obj, indent=2))
                
                # Save timestamp for ACK
                if 'timestamp' in msg_obj:
                    last_received_timestamp = msg_obj['timestamp']
                    print(f"\n[INFO] Saved timestamp for ACK: {last_received_timestamp}")
                    
            except json.JSONDecodeError:
                print(message)
            
            print(f"{'='*80}\n")
            print("> ", end='', flush=True)
            
        except socket.timeout:
            continue
        except Exception as e:
            if running:
                print(f"\n[ERROR] Receiving: {e}")
            break


def send_message(msg):
    """Send message via TCP (fixed 1024 byte buffer)"""
    buffer = bytearray(1024)
    msg_bytes = msg.encode()
    buffer[:len(msg_bytes)] = msg_bytes
    sock.sendall(buffer)


def _prefix():
    """Return message type prefix based on client type"""
    return 'WAREHOUSE_TO_SERVER' if CLIENT_TYPE == 'WAREHOUSE' else 'HUB_TO_SERVER'


def send_auth_request():
    """Send authentication request"""
    payload = {
        "username": USERNAME,
        "password": PASSWORD
    }
    msg = create_message(f"{_prefix()}__AUTH_REQUEST", payload)
    send_message(msg)
    print(f"[SENT] {_prefix()}__AUTH_REQUEST")


def send_ack():
    """Send ACK for last received message"""
    global last_received_timestamp
    
    if not last_received_timestamp:
        print("[ERROR] No message received yet. Cannot send ACK.")
        return
    
    # Create ACK with the timestamp of the message we're acknowledging
    msg = {
        "msg_type": f"{_prefix()}__ACK",
        "source_role": CLIENT_TYPE,
        "source_id": CLIENT_ID,
        "target_role": "SERVER",
        "target_id": "SERVER",
        "timestamp": last_received_timestamp,  # Use same timestamp as message we're ACKing
        "payload": {
            "status_code": 200,
            "ack_for_timestamp": last_received_timestamp
        }
    }
    
    json_str = json.dumps(msg, separators=(',', ':'))
    checksum = calculate_checksum(json_str)
    msg["checksum"] = checksum
    
    final_msg = json.dumps(msg, separators=(',', ':'))
    send_message(final_msg)
    print(f"[SENT] ACK for timestamp: {last_received_timestamp}")


def send_keepalive():
    """Send keepalive message"""
    payload = {}
    msg = create_message(f"{_prefix()}__KEEPALIVE", payload)
    send_message(msg)
    print(f"[SENT] KEEPALIVE")


def send_inventory():
    """Send inventory update"""
    payload = {
        "items": [
            {"item_id": 1, "item_name": "food", "quantity": 100},
            {"item_id": 2, "item_name": "water", "quantity": 100},
            {"item_id": 3, "item_name": "medicine", "quantity": 100},
            {"item_id": 4, "item_name": "tools", "quantity": 100},
            {"item_id": 5, "item_name": "guns", "quantity": 100},
            {"item_id": 6, "item_name": "ammo", "quantity": 100}
        ]
    }
    msg_type = f"{_prefix()}__INVENTORY_UPDATE"
    msg = create_message(msg_type, payload)
    send_message(msg)
    print(f"[SENT] {msg_type}")


def send_stock_request():
    """Send stock request (Hub only)"""
    if CLIENT_TYPE != 'HUB':
        print("[WARN] Stock request is a Hub operation")
    payload = {
        "items": [
            {"item_id": 1, "item_name": "food", "quantity": 10},
            {"item_id": 2, "item_name": "water", "quantity": 5}
        ]
    }
    msg = create_message("HUB_TO_SERVER__STOCK_REQUEST", payload)
    send_message(msg)
    print(f"[SENT] HUB_TO_SERVER__STOCK_REQUEST")


def send_shipment_notice():
    """Send shipment notice (Warehouse only)"""
    if CLIENT_TYPE != 'WAREHOUSE':
        print("[WARN] Shipment notice is a Warehouse operation")
    payload = {
        "items": [
            {"item_id": 1, "item_name": "food", "quantity": 10},
            {"item_id": 2, "item_name": "water", "quantity": 5}
        ]
    }
    msg = create_message("WAREHOUSE_TO_SERVER__SHIPMENT_NOTICE", payload)
    send_message(msg)
    print(f"[SENT] WAREHOUSE_TO_SERVER__SHIPMENT_NOTICE")


def send_replenish_request():
    """Send replenish request (Warehouse only)"""
    if CLIENT_TYPE != 'WAREHOUSE':
        print("[WARN] Replenish request is a Warehouse operation")
    payload = {
        "items": [
            {"item_id": 1, "item_name": "food", "quantity": 20},
            {"item_id": 2, "item_name": "water", "quantity": 20}
        ]
    }
    msg = create_message("WAREHOUSE_TO_SERVER__REPLENISH_REQUEST", payload)
    send_message(msg)
    print(f"[SENT] WAREHOUSE_TO_SERVER__REPLENISH_REQUEST")


def send_receipt_confirmation():
    """Send stock receipt confirmation"""
    payload = {
        "items": [
            {"item_id": 1, "item_name": "food", "quantity": 10},
            {"item_id": 2, "item_name": "water", "quantity": 5}
        ]
    }
    msg_type = f"{_prefix()}__STOCK_RECEIPT_CONFIRMATION"
    msg = create_message(msg_type, payload)
    send_message(msg)
    print(f"[SENT] {msg_type}")


def print_help():
    """Print available commands"""
    print("\n" + "="*80)
    print(f"Client: {CLIENT_ID} ({CLIENT_TYPE})")
    print("-"*80)
    print("  auth         - Send authentication request")
    print("  ack          - Send ACK for last received message")
    print("  keepalive    - Send keepalive message")
    print("  inventory    - Send inventory update")
    if CLIENT_TYPE == 'HUB':
        print("  stock        - Send stock request")
        print("  receipt      - Send stock receipt confirmation")
    else:
        print("  shipment     - Send shipment notice")
        print("  replenish    - Send replenish request")
        print("  receipt      - Send stock receipt confirmation")
    print("  help         - Show this help")
    print("  quit         - Exit")
    print("="*80 + "\n")


def main():
    global sock, running, CLIENT_ID, CLIENT_TYPE, USERNAME, PASSWORD
    
    # Parse CLI arguments
    if len(sys.argv) < 2 or sys.argv[1] in ('-h', '--help'):
        print("Usage: python3 client_msg_tester.py <hub|wh> [client_number]")
        print("  hub  - Connect as a HUB client")
        print("  wh   - Connect as a WAREHOUSE client")
        sys.exit(0)
    
    role = sys.argv[1].strip().lower()
    number = int(sys.argv[2]) if len(sys.argv) > 2 else None
    
    # Load credentials from .conf file
    conf, conf_path = find_client_conf(role, number)
    
    CLIENT_TYPE = conf['type']
    CLIENT_ID = conf['username']
    USERNAME = conf['username']
    PASSWORD = conf['password']
    
    print(f"\n{'='*80}")
    print(f"Interactive LoRa-CHADS Client Mock")
    print(f"{'='*80}")
    print(f"[CONFIG] Loaded: {os.path.basename(conf_path)}")
    print(f"[CONFIG] Client:  {CLIENT_ID} ({CLIENT_TYPE})")
    print(f"[CONFIG] Server:  {SERVER_HOST}:{SERVER_PORT} (TCPv4)")
    if '_wrong' in PASSWORD:
        print(f"[CONFIG] WARNING: Password has _wrong suffix (will fail auth)")
    print()
    
    # Create TCP IPv4 socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    
    print(f"[INFO] Connecting to {SERVER_HOST}:{SERVER_PORT}...")
    try:
        sock.connect((SERVER_HOST, SERVER_PORT))
        print(f"[INFO] Connected!\n")
    except Exception as e:
        print(f"[ERROR] Connection failed: {e}")
        return
    
    # Start listener thread
    listener_thread = threading.Thread(target=listen_for_responses, daemon=True)
    listener_thread.start()
    
    print_help()
    
    # Main command loop
    try:
        while running:
            try:
                cmd = input("> ").strip().lower()
                
                if not cmd:
                    continue
                    
                if cmd == 'auth':
                    send_auth_request()
                elif cmd == 'ack':
                    send_ack()
                elif cmd == 'keepalive':
                    send_keepalive()
                elif cmd == 'inventory':
                    send_inventory()
                elif cmd == 'stock':
                    send_stock_request()
                elif cmd == 'shipment':
                    send_shipment_notice()
                elif cmd == 'replenish':
                    send_replenish_request()
                elif cmd == 'receipt':
                    send_receipt_confirmation()
                elif cmd == 'help':
                    print_help()
                elif cmd in ['quit', 'exit', 'q']:
                    print("\n[INFO] Exiting...")
                    running = False
                    break
                else:
                    print(f"[ERROR] Unknown command: {cmd}")
                    print("Type 'help' for available commands")
                    
            except EOFError:
                print("\n[INFO] EOF detected, exiting...")
                running = False
                break
                
    except KeyboardInterrupt:
        print("\n[INFO] Interrupted by user")
    finally:
        running = False
        sock.close()
        print("[INFO] Socket closed")


if __name__ == '__main__':
    main()

