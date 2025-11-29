import os
import random
import hashlib
import json

SERVER_HOST = "127.0.0.1"
SERVER_PORT_TCP = "9999"
SERVER_PORT_UDP = "9998"

ERROR_RATE = 0.1 

OUTPUT_DIR = "config/clients"
SERVER_CREDENTIALS_FILE = "config/credentials.json"

def generate_password(username):
    """Gen a password based on the username using MD5 hash"""
    return hashlib.md5(username.encode()).hexdigest()

def generate_configs():
    if not os.path.exists(OUTPUT_DIR):
        try:
            os.makedirs(OUTPUT_DIR)
            print(f"Directory created: {OUTPUT_DIR}")
        except OSError as e:
            print(f"Error creating directory {OUTPUT_DIR}: {e}")
            return

    clients = []
    server_credentials = []

    for i in range(1, 2001):
        username = f"client_{i:04d}"
        client_type = "warehouse" if i <= 1000 else "hub"       
        clients.append({
            "username": username,
            "type": client_type
        })

    print(f"Generating {len(clients)} configuration files...")

    for client in clients:
        username = client['username']
        correct_password = generate_password(username)
        client_type = client['type']
        
        server_credentials.append({
            "username": username,
            "password": correct_password,
            "type": client_type
        })

        client_password = correct_password
        if random.random() < ERROR_RATE:
            client_password += "_wrong"

        protocol = random.choice(['tcp', 'udp'])
        ip_version = random.choice(['v4', 'v6'])
        
        port = SERVER_PORT_TCP if protocol == 'tcp' else SERVER_PORT_UDP

        filename = os.path.join(OUTPUT_DIR, f"{username}.conf")
        
        try:
            with open(filename, "w") as f:
                f.write(f"host = {SERVER_HOST}\n")
                f.write(f"ipversion = {ip_version}\n")
                f.write(f"protocol = {protocol}\n")
                f.write(f"port = {port}\n")
                f.write(f"type = {client_type}\n")
                f.write(f"username = {username}\n")
                f.write(f"password = {client_password}\n")
            
        except IOError as e:
            print(f"Error writing file {filename}: {e}")

    try:
        with open(SERVER_CREDENTIALS_FILE, "w") as f:
            json.dump(server_credentials, f, indent=4)
        print(f"Generated server credentials file: {SERVER_CREDENTIALS_FILE}")
    except IOError as e:
        print(f"Error writing server credentials file: {e}")

    print("Process completed.")
if __name__ == "__main__":
    generate_configs()
