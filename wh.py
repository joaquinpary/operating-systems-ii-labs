import socket
import threading

MESSAGES = {
    "auth": """{"type":"client_auth_request","payload":{"type":"warehouse","username":"user0003","password":"IvcBerloz3","timestamp":"2025-05-13T22:33:01Z"},"checksum":"8F9A371A"}""",
    "ack": """{"type":"client_acknowledgment","payload":{"username":"user0003","session_token":"token","status":"success","timestamp":"2025-05-13T22:33:01Z"},"checksum":"EA4718F6"}""",
    "keepalive": """{"type":"client_keepalive","payload":{"username":"user0003","session_token":"token","timestamp":"2025-05-13T22:34:01Z"},"checksum":"59EF937F"}""",
    "inv_upd": """{"type":"client_inventory_update","payload":{"username":"user0003","session_token":"token","items":[{"item":"water","quantity":100},{"item":"food","quantity":100},{"item":"medicine","quantity":100},{"item":"guns","quantity":100},{"item":"ammo","quantity":100},{"item":"tools","quantity":100}],"timestamp":"2025-05-13T22:33:01Z"},"checksum":"81C0DF06"}""",
    "send_stock": """{"type":"warehouse_send_stock_to_hub","payload":{"username":"user0003","session_token":"token","hub_username":"user1398","items":[{"item":"water","quantity":0},{"item":"food","quantity":0},{"item":"medicine","quantity":0},{"item":"guns","quantity":95},{"item":"ammo","quantity":0},{"item":"tools","quantity":0}],"timestamp":"2025-05-13T22:34:19Z"},"checksum":"FB0280F4"}""",
    "req_stock": """{"type":"warehouse_request_stock","payload":{"username":"user0003","session_token":"token","items":[{"item":"water","quantity":0},{"item":"food","quantity":0},{"item":"medicine","quantity":0},{"item":"guns","quantity":395},{"item":"ammo","quantity":0},{"item":"tools","quantity":0}],"timestamp":"2025-05-13T22:34:19Z"},"checksum":"C5D9FC55"}"""
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
    host = "0.0.0.0"  # Cambiar si el servidor está en otra IP
    port = 9999        # Cambiar si tu servidor está en otro puerto

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
        print(f"Connection refused. Is the server running at {host}:{port}?")
    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    main()
