import json
import random
import string

NUM_WAREHOUSES = 500
NUM_HUBS = 1500
FAKE_RATE = 0.05

def deterministic_password(client_id):
    random.seed(client_id)
    return ''.join(random.choices(string.ascii_letters + string.digits, k=10))

# Opciones aleatorias para los nuevos campos
host = "localhost"
port = "9999"
PROTOCOLS = ["tcp", "udp"]
VERSIONS = ["ipv4", "ipv6"]

clients = []
server_credentials = []

# Generar información para los clientes
for i in range(1, NUM_WAREHOUSES + 1):
    client_id = f"warehouse{i:04d}"
    username = f"user{i:04d}"
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

# Generar contraseñas falsas para algunos clientes
num_fakes = int(len(clients) * FAKE_RATE)
fake_indices = random.sample(range(len(clients)), num_fakes)

for idx in fake_indices:
    clients[idx]["password"] = "wrongpass123"

# Para el servidor, sólo queremos las credenciales sin los datos de "connect"
for client in clients:
    # Se crea una entrada para el servidor (sin el campo "connect")
    server_entry = {
        "client_id": client["client_id"],
        "username": client["username"],
        "password": client["password"]
    }
    server_credentials.append(server_entry)

# Guardar el archivo de credenciales de los clientes
with open("../config/clients_credentials.json", "w") as f:
    json.dump({"clients": clients}, f, indent=4)

# Guardar el archivo de credenciales del servidor
with open("../config/server_credentials.json", "w") as f:
    json.dump({"clients": server_credentials}, f, indent=4)

print(f"✅ Generated:")
print(f"  - clients_credentials.json ({len(clients)} entries, {num_fakes} fake passwords)")
print(f"  - server_credentials.json ({len(server_credentials)} valid entries)")
