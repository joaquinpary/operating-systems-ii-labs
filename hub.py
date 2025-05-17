import socket
import threading

MESSAGES = {
    "auth": """{"type":"client_auth_request","payload":{"type":"hub","username":"user1398","password":"9fPxdVDxTg","timestamp":"2025-05-13T22:33:02Z"},"checksum":"FEF2CC9F"}""",
    "ack": """{"type":"client_acknowledgment","payload":{"username":"user1398","session_token":"token","status":"success","timestamp":"2025-05-13T22:33:02Z"},"checksum":"008A1C1B"}""",
    "keepalive": """{"type":"client_keepalive","payload":{"username":"user1398","session_token":"token","timestamp":"2025-05-13T22:34:02Z"},"checksum":"398E0227"}""",
    "inv_upd": """{"type":"client_inventory_update","payload":{"username":"user1398","session_token":"token","items":[{"item":"water","quantity":64},{"item":"food","quantity":45},{"item":"medicine","quantity":48},{"item":"guns","quantity":23},{"item":"ammo","quantity":29},{"item":"tools","quantity":56}],"timestamp":"2025-05-13T22:34:02Z"},"checksum":"4CFDE447"}""",
    "req_stock": """{"type":"hub_request_stock","payload":{"username":"user1398","session_token":"token","items":[{"item":"water","quantity":0},{"item":"food","quantity":0},{"item":"medicine","quantity":0},{"item":"guns","quantity":95},{"item":"ammo","quantity":0},{"item":"tools","quantity":0}],"timestamp":"2025-05-13T22:34:19Z"},"checksum":"3CB6DD20"}""",
    "receive_stock": """{"type":"hub_confirm_stock","payload":{"username":"user1398","session_token":"token","warehouse_username":"user0003","items":[{"item":"water","quantity":0},{"item":"food","quantity":0},{"item":"medicine","quantity":0},{"item":"guns","quantity":95},{"item":"ammo","quantity":0},{"item":"tools","quantity":0}],"timestamp":"2025-05-13T22:34:19Z"},"checksum":"77EBACFF"}"""
}

def receive_messages(sock):
    try:
        while True:
            data = sock.recv(4096)
            if not data:
                print("Server closed connection.")
                break
            print(f"\n[Received] {data.decode()}\n> ", end="")
    except Exception as e:
        print(f"\n[Receiver Error] {e}")

def main():
    host = "0.0.0.0"  # Cambiá si es necesario
    port = 9999        # Cambiá si es necesario

    try:
        with socket.create_connection((host, port)) as sock:
            print("Connected to server.")
            threading.Thread(target=receive_messages, args=(sock,), daemon=True).start()

            while True:
                command = input("> ").strip()
                if command == "exit":
                    break
                msg = MESSAGES.get(command)+"\n"
                if msg:
                    sock.sendall(msg.encode())
                    print(f"[Sent] {command} message.")
                else:
                    print("Unknown command.")
    except ConnectionRefusedError:
        print(f"Connection refused. Make sure the server is running at {host}:{port}")
    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    main()
