import json
import random
import string

CLIENTS = 2000
NUM_WAREHOUSES = int(CLIENTS*0.20)
NUM_HUBS = int(CLIENTS*0.80)
FAKE_RATE = 0.05

def deterministic_password(client_id):
    random.seed(client_id)
    return ''.join(random.choices(string.ascii_letters + string.digits, k=10))

def get_port(protocol, version):
    if protocol == "tcp" and version == "ipv4":
        return "9999"
    elif protocol == "tcp" and version == "ipv6":
        return "9998"
    elif protocol == "udp" and version == "ipv4":
        return "9997"
    elif protocol == "udp" and version == "ipv6":
        return "9996"
    else:
        raise ValueError("Invalid protocol/version combination")

host = "dhl_server"
PROTOCOLS = ["tcp", "udp"]
VERSIONS = ["ipv4", "ipv6"]

clients = []
server_credentials = []

for i in range(1, NUM_WAREHOUSES + 1):
    client_id = f"warehouse{i:04d}"
    username = f"user{i:04d}"
    password = deterministic_password(client_id)
    protocol = random.choice(PROTOCOLS)
    version = random.choice(VERSIONS)
    port = get_port(protocol, version)
    entry = {
        "client_id": client_id,
        "username": username,
        "password": password,
        "connect": {
            "host": host,
            "port": port,
            "protocol": protocol,
            "version": version
        }
    }
    clients.append(entry)

for i in range(1, NUM_HUBS + 1):
    client_id = f"hub{i:04d}"
    idx = i + NUM_WAREHOUSES
    username = f"user{idx:04d}"
    password = deterministic_password(client_id)
    protocol = random.choice(PROTOCOLS)
    version = random.choice(VERSIONS)
    entry = {
        "client_id": client_id,
        "username": username,
        "password": password,
        "connect": {
            "host": host,
            "port": port,
            "protocol": protocol,
            "version": version
        }
    }
    clients.append(entry)

num_fakes = int(len(clients) * FAKE_RATE)
fake_indices = random.sample(range(len(clients)), num_fakes)

for idx in fake_indices:
    clients[idx]["password"] = "wrongpass123"

for client in clients:
    server_entry = {
        "client_id": client["client_id"],
        "username": client["username"],
        "password": client["password"]
    }
    server_credentials.append(server_entry)

with open("../config/clients_credentials.json", "w") as f:
    json.dump({"clients": clients}, f, indent=4)

with open("../config/server_credentials.json", "w") as f:
    json.dump({"clients": server_credentials}, f, indent=4)

print(f"✅ Generated:")
print(f"  - clients_credentials.json ({len(clients)} entries, {num_fakes} fake passwords)")
print(f"  - server_credentials.json ({len(server_credentials)} valid entries)")
