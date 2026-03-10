import os
import random
import hashlib
import sys

SERVER_HOST = "server"
SERVER_PORT = "9999"

ERROR_RATE = 0.1

CLIENTS_DIR = "config/clients"
SERVER_DIR = "config/server"


def generate_password(username):
    """Gen a password based on the username using MD5 hash"""
    return hashlib.md5(username.encode()).hexdigest()


def ensure_dir(path):
    """Create directory if it doesn't exist"""
    if not os.path.exists(path):
        try:
            os.makedirs(path)
            print(f"Directory created: {path}")
        except OSError as e:
            print(f"Error creating directory {path}: {e}")
            return False
    return True


def clean_conf_files(directory):
    """Remove existing .conf files from a directory"""
    for filename in os.listdir(directory):
        if filename.endswith('.conf'):
            os.remove(os.path.join(directory, filename))


def write_conf_file(filepath, username, password, client_type,
                    host=SERVER_HOST, port=SERVER_PORT,
                    protocol=None, ip_version=None):
    """Write a single .conf file"""
    try:
        with open(filepath, "w") as f:
            f.write(f"host = {host}\n")
            if ip_version:
                f.write(f"ipversion = {ip_version}\n")
            if protocol:
                f.write(f"protocol = {protocol}\n")
            f.write(f"port = {port}\n")
            f.write(f"type = {client_type}\n")
            f.write(f"username = {username}\n")
            f.write(f"password = {password}\n")
    except IOError as e:
        print(f"Error writing file {filepath}: {e}")


def generate_configs(num_clients=2000):
    if not ensure_dir(CLIENTS_DIR) or not ensure_dir(SERVER_DIR):
        return

    clients = []

    if num_clients < 2000:
        print("Cleaning existing config files...")
        clean_conf_files(CLIENTS_DIR)
        clean_conf_files(SERVER_DIR)

    for i in range(1, num_clients + 1):
        username = f"client_{i:04d}"
        if num_clients <= 20:
            client_type = "WAREHOUSE" if i % 2 == 1 else "HUB"
        else:
            client_type = "WAREHOUSE" if i <= (num_clients // 2) else "HUB"
        clients.append({
            "username": username,
            "type": client_type
        })

    print(f"Generating {len(clients)} configuration files...")

    for client in clients:
        username = client['username']
        correct_password = generate_password(username)
        client_type = client['type']

        # Client password may be intentionally wrong to test auth failures
        client_password = correct_password
        if random.random() < ERROR_RATE:
            client_password += "_wrong"

        protocol = random.choice(['tcp', 'udp'])
        ip_version = random.choice(['v4', 'v6'])

        # Client .conf (may have wrong password)
        write_conf_file(
            os.path.join(CLIENTS_DIR, f"{username}.conf"),
            username, client_password, client_type,
            protocol=protocol, ip_version=ip_version
        )

        # Server .conf (always correct password, for DB population)
        write_conf_file(
            os.path.join(SERVER_DIR, f"{username}.conf"),
            username, correct_password, client_type
        )

    print(f"Generated client configs in: {CLIENTS_DIR}")
    print(f"Generated server configs in: {SERVER_DIR}")
    print("Process completed.")


if __name__ == "__main__":
    num_clients = 2000  # Default
    if len(sys.argv) > 1:
        try:
            num_clients = int(sys.argv[1])
        except ValueError:
            print(f"Invalid number of clients: {sys.argv[1]}. Using default: 2000")
            num_clients = 2000
    generate_configs(num_clients)
