#!/usr/bin/env python3
"""
Interactive Client Mock for testing LoRa-CHADS server
Supports TCP/UDP and IPv4/IPv6

Commands:
  auth         - Send authentication request
  ack          - Send ACK for last received message
  keepalive    - Send keepalive message
  inventory    - Send inventory update
  quit         - Exit
"""

import socket
import threading
import json
import time
from datetime import datetime, timezone

# Configuration
CLIENT_ID = 'client_0002'
CLIENT_TYPE = 'HUB'
USERNAME = 'client_0002'
PASSWORD = '4f9d47217dd3a96300da3f5809ffef42'

# Global state
sock = None
last_received_timestamp = None
running = True
protocol = None  # 'tcp' or 'udp'
ip_version = None  # 4 or 6
server_host = None
server_port = 9999


def calculate_checksum(json_str):
    """Calculate simple checksum (sum of bytes % 0xFFFFFF)"""
    checksum = sum(json_str.encode()) % 0xFFFFFF
    return f"{checksum:X}"


def create_message(msg_type, payload):
    """Create a message with proper structure"""
    timestamp = datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')
    
    # Determine target role based on message type
    if 'HUB' in msg_type:
        target_role = 'SERVER'
    else:
        target_role = 'HUB'
    
    msg = {
        "msg_type": msg_type,
        "source_role": CLIENT_TYPE,
        "source_id": CLIENT_ID,
        "target_role": target_role,
        "target_id": "SERVER",
        "timestamp": timestamp,
        "payload": payload
    }
    
    # Calculate checksum (without checksum field)
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
            
            if protocol == 'udp':
                data, addr = sock.recvfrom(4096)
            else:  # TCP
                # TCP server sends 1024 bytes fixed buffer
                data = sock.recv(1024)
                if not data:
                    print("\n[INFO] Server closed connection")
                    running = False
                    break
                addr = sock.getpeername()
            
            # Strip null bytes (padding) before decoding
            message = data.rstrip(b'\x00').decode()
            
            print(f"\n{'='*80}")
            print(f"[RECEIVED] From {addr}")
            
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
    """Send message via TCP or UDP"""
    if protocol == 'udp':
        sock.sendto(msg.encode(), (server_host, server_port))
    else:  # TCP
        # TCP needs fixed-size buffer (1024 bytes as per server)
        buffer = bytearray(1024)
        msg_bytes = msg.encode()
        buffer[:len(msg_bytes)] = msg_bytes
        sock.sendall(buffer)


def send_auth_request():
    """Send authentication request"""
    payload = {
        "username": USERNAME,
        "password": PASSWORD
    }
    msg = create_message("HUB_TO_SERVER__AUTH_REQUEST", payload)
    send_message(msg)
    print(f"[SENT] AUTH_REQUEST")


def send_ack():
    """Send ACK for last received message"""
    global last_received_timestamp
    
    if not last_received_timestamp:
        print("[ERROR] No message received yet. Cannot send ACK.")
        return
    
    # Create ACK with the timestamp of the message we're acknowledging
    msg = {
        "msg_type": "HUB_TO_SERVER__ACK",
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
    msg = create_message("HUB_TO_SERVER__KEEPALIVE", payload)
    send_message(msg)
    print(f"[SENT] KEEPALIVE")


def send_inventory():
    """Send inventory update"""
    payload = {
        "items": [
            {"item_id": "ITEM001", "quantity": 100},
            {"item_id": "ITEM002", "quantity": 50}
        ]
    }
    msg = create_message("WAREHOUSE_TO_SERVER__INVENTORY_UPDATE", payload)
    send_message(msg)
    print(f"[SENT] INVENTORY_UPDATE")


def print_help():
    """Print available commands"""
    print("\n" + "="*80)
    print("Available commands:")
    print("  auth         - Send authentication request")
    print("  ack          - Send ACK for last received message")
    print("  keepalive    - Send keepalive message")
    print("  inventory    - Send inventory update")
    print("  help         - Show this help")
    print("  quit         - Exit")
    print("="*80 + "\n")


def main():
    global sock, running, protocol, ip_version, server_host, server_port
    
    print(f"\n{'='*80}")
    print(f"Interactive LoRa-CHADS Client Mock")
    print(f"{'='*80}\n")
    
    # Ask for protocol
    while True:
        proto_input = input("Protocol (tcp/udp) [udp]: ").strip().lower()
        if proto_input in ['tcp', 'udp', '']:
            protocol = proto_input or 'udp'
            break
        print("[ERROR] Invalid protocol. Use 'tcp' or 'udp'")
    
    # Ask for IP version
    while True:
        ip_input = input("IP version (4/6) [4]: ").strip()
        if ip_input in ['4', '6', '']:
            ip_version = int(ip_input or '4')
            break
        print("[ERROR] Invalid IP version. Use '4' or '6'")
    
    # Set server host based on IP version
    if ip_version == 6:
        server_host = '::1'
    else:
        server_host = '127.0.0.1'
    
    # Ask for port
    port_input = input(f"Server port [9999]: ").strip()
    if port_input:
        server_port = int(port_input)
    
    print(f"\n[CONFIG] Protocol: {protocol.upper()}")
    print(f"[CONFIG] IP Version: IPv{ip_version}")
    print(f"[CONFIG] Server: {server_host}:{server_port}")
    print(f"[CONFIG] Client ID: {CLIENT_ID} ({CLIENT_TYPE})\n")
    
    # Create socket based on protocol and IP version
    family = socket.AF_INET6 if ip_version == 6 else socket.AF_INET
    sock_type = socket.SOCK_STREAM if protocol == 'tcp' else socket.SOCK_DGRAM
    
    sock = socket.socket(family, sock_type)
    
    if protocol == 'tcp':
        print(f"[INFO] Connecting to {server_host}:{server_port}...")
        try:
            sock.connect((server_host, server_port))
            print(f"[INFO] Connected!\n")
        except Exception as e:
            print(f"[ERROR] Connection failed: {e}")
            return
    else:  # UDP
        sock.bind(('::' if ip_version == 6 else '0.0.0.0', 0))
        local_addr = sock.getsockname()
        print(f"[INFO] Listening on {local_addr[0]}:{local_addr[1]}\n")
    
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

