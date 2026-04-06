"""
Credential generator module.

Public API:
    generate_configs(num_clients)  — generate client & server .conf files
"""

import os
import random
import hashlib

SERVER_HOST = "server"
SERVER_PORT = "9999"

ERROR_RATE = 0.1

# Resolve paths relative to the project root (two levels up from this file)
_MODULE_DIR = os.path.dirname(os.path.abspath(__file__))
_PROJECT_ROOT = os.path.dirname(os.path.dirname(_MODULE_DIR))
CLIENTS_DIR = os.path.join(_PROJECT_ROOT, "config", "clients")
SERVER_DIR = os.path.join(_PROJECT_ROOT, "config", "server")


def generate_password(username):
    """Gen a password based on the username using MD5 hash"""
    return hashlib.md5(username.encode()).hexdigest()


def _ensure_dir(path):
    """Create directory if it doesn't exist"""
    if not os.path.exists(path):
        try:
            os.makedirs(path)
            print(f"  Directory created: {path}")
        except OSError as e:
            print(f"  Error creating directory {path}: {e}")
            return False
    return True


def _clean_conf_files(directory):
    """Remove existing .conf files from a directory"""
    for filename in os.listdir(directory):
        if filename.endswith('.conf'):
            os.remove(os.path.join(directory, filename))


def _write_conf_file(filepath, username, password, client_type,
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
        print(f"  Error writing file {filepath}: {e}")


def generate_configs(num_clients=2000):
    """Generate client and server configuration files.

    Args:
        num_clients: Number of client configs to generate.
    """
    if not _ensure_dir(CLIENTS_DIR) or not _ensure_dir(SERVER_DIR):
        return

    clients = []

    if num_clients < 2000:
        print("  Cleaning existing config files...")
        _clean_conf_files(CLIENTS_DIR)
        _clean_conf_files(SERVER_DIR)

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

    print(f"  Generating {len(clients)} configuration files...")

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
        _write_conf_file(
            os.path.join(CLIENTS_DIR, f"{username}.conf"),
            username, client_password, client_type,
            protocol=protocol, ip_version=ip_version
        )

        # Server .conf (always correct password, for DB population)
        _write_conf_file(
            os.path.join(SERVER_DIR, f"{username}.conf"),
            username, correct_password, client_type
        )

    # Always generate admin CLI credential
    cli_username = "cli_admin"
    cli_password = generate_password(cli_username)
    _write_conf_file(
        os.path.join(CLIENTS_DIR, "cli_admin.conf"),
        cli_username, cli_password, "CLI",
        protocol="tcp", ip_version="v4"
    )
    _write_conf_file(
        os.path.join(SERVER_DIR, "cli_admin.conf"),
        cli_username, cli_password, "CLI"
    )

    # Always generate API gateway credential
    gw_username = "api_gateway"
    gw_password = generate_password(gw_username)
    _write_conf_file(
        os.path.join(SERVER_DIR, "api_gateway.conf"),
        gw_username, gw_password, "GATEWAY"
    )

    print(f"  Client configs → {CLIENTS_DIR}")
    print(f"  Server configs → {SERVER_DIR}")
    print(f"  Admin CLI config → cli_admin.conf")
    print(f"  API Gateway config → api_gateway.conf")
    print("  Done.")
