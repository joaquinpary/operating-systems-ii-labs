# Operating Systems II Labs - Distributed Logistics System

Distributed logistics system with a Docker Compose stack and embedded firmware (Zephyr RTOS).

## Requirements

- [Docker](https://docs.docker.com/get-docker/) and [Docker Compose](https://docs.docker.com/compose/) v2
- For the firmware: [Zephyr / west](https://docs.zephyrproject.org/latest/develop/getting_started/index.html) toolchain and `qtbase5-dev` (native UI)

## Running the stack

```bash
cp .env.example .env
# Edit .env and replace sensitive values (passwords, JWT_SECRET, etc.)

docker compose up --build
```

Services available once the stack is up:

| Service | URL / Port |
|---|---|
| API Gateway (HTTPS) | https://api.localhost |
| API Gateway (HTTP → redirects) | http://api.localhost |
| Traefik dashboard | http://localhost:8082 |
| Eureka dashboard | http://localhost:8761 |
| Grafana | http://localhost:3000 (admin / see `.env`) |
| RabbitMQ management | http://localhost:15672 |
| ML Predictor | http://localhost:9000/health |
| PostgreSQL | localhost:5434 |
| MongoDB | localhost:27017 |
| MQTT Broker | localhost:1883 |

> **Local TLS:** Traefik is configured for TLS on `api.localhost`.
> Add `127.0.0.1 api.localhost` to your `/etc/hosts` and trust the self-signed certificate if needed.

## Simulated clients

The `client` service spawns `NUM_CLIENTS` (default 10) clients against the server:

```bash
# Change the number of clients
NUM_CLIENTS=20 docker compose up client
```

## Monitoring MQTT

With the broker running, subscribe to all topics:

```bash
docker exec -it dhl_mosquitto mosquitto_sub -t '#' -v
```

## Admin tools (`tools.py`)

`scripts/tools.py` is an interactive CLI that bundles all development and testing utilities in one place:

```bash
python3 scripts/tools.py
```

| Option | Description |
|---|---|
| **1) Generate Credentials** | Create client config files for N simulated clients |
| **2) Admin CLI** | Interactive shell against the C++ server (admin, hub, or warehouse auth) |
| **3) REST API Client** | Upload maps, run flow/circuit solvers, fetch results, or generate random maps |
| **4) Benchmark** | Automated throughput benchmarks for flow and circuit algorithms |
| **5) Profiling Graph** | Plot benchmark results from the server or a local JSON file (outputs PNG) |
| **6) API Gateway Tester** | Login, create/dispatch shipments, check statuses, open WebSocket chat, hit `/metrics` |
| **7) Generate TLS Certs** | Create self-signed certificates for Traefik local TLS |
| **8) Gateway Stress Test** | Concurrent load test against the API Gateway |

> The tools connect to services running locally. Make sure the Docker stack is up before using options 2–6 and 8.

## Firmware (outside Docker)

The firmware runs on Zephyr RTOS. To build and run it in simulation mode (`native_sim`):

```bash
# Incremental build
./scripts/build_firmware.sh

# Clean build
./scripts/build_firmware.sh --pristine
```

The binary is placed at `firmware/build/native_sim/zephyr/zephyr.exe`. The firmware connects to the MQTT broker at `localhost:1883`, so the Docker stack must be running.

### Provisioning

On first boot, a Qt5 window opens where you configure Wi-Fi credentials and the `employee_id`. Data is persisted in simulated NVS storage.

## Project structure

```
api_gateway/   → Public Go (Fiber) API with Traefik load balancing
config/        → Service configuration (server, mosquitto, traefik)
firmware/      → Zephyr firmware for courier devices (ESP32 / native_sim)
grafana/       → Dashboards and datasources
ml_service/    → Python prediction service
src/           → C++ core (server, client, shared modules)
tests/         → Unit tests
```
