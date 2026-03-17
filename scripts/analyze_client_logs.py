#!/usr/bin/env python3
"""
Client Log Analyzer
Analyzes all client logs to extract aggregate metrics: ACK timeouts,
message rates, disconnect patterns, and per-minute time-series.

Usage:
    python analyze_client_logs.py [log_dir]

    log_dir: Directory containing client_XXXX.log files
             Defaults to ../logs/clients/
"""

import os
import re
import sys
from collections import defaultdict
from datetime import datetime
from pathlib import Path


def parse_timestamp(ts_str: str) -> datetime:
    ts_str = ts_str.rstrip('Z')
    try:
        return datetime.strptime(ts_str, "%Y-%m-%d %H:%M:%S.%f")
    except ValueError:
        return datetime.strptime(ts_str, "%Y-%m-%d %H:%M:%S")


class ClientLogAnalyzer:
    def __init__(self, log_dir: str):
        self.log_dir = Path(log_dir)

        # Per-client data
        self.client_count = 0
        self.client_roles = {}  # client_id -> role

        # Aggregate counters
        self.total_messages_sent = 0
        self.total_ack_timeouts = 0
        self.total_max_retries = 0
        self.total_disconnects = 0
        self.disconnect_reasons = defaultdict(int)

        # Per-type sent counts
        self.sent_by_type = defaultdict(int)

        # Per-minute tracking
        self.sent_per_minute = defaultdict(int)
        self.ack_timeout_per_minute = defaultdict(int)
        self.disconnect_per_minute = defaultdict(int)
        self.sent_type_per_minute = defaultdict(lambda: defaultdict(int))

        # Per-client disconnect time
        self.client_disconnect_times = {}  # client_id -> timestamp
        self.client_connect_times = {}     # client_id -> timestamp

        # First/last timestamp seen
        self.first_ts = None
        self.last_ts = None

    def find_log_files(self) -> list:
        files = sorted(self.log_dir.glob("client_*.log"))
        return files

    def parse_all(self):
        files = self.find_log_files()
        self.client_count = len(files)
        print(f"Found {self.client_count} client log files")

        # Patterns
        msg_sent = re.compile(
            r'\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d+Z?)\].*Message sent: type=(\S+),'
        )
        role_pat = re.compile(
            r'Client identity set: (\S+) / (\S+)'
        )
        max_retries = re.compile(
            r'\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d+Z?)\].*Max retries exceeded'
        )
        disconnect = re.compile(
            r'\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d+Z?)\].*(Disconnection due to \S+)'
        )
        auth_ok = re.compile(
            r'\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d+Z?)\].*Authentication successful'
        )
        ack_timeout_pat = re.compile(
            r'\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d+Z?)\].*ACK timeout'
        )

        processed = 0
        for f in files:
            client_id = f.stem  # e.g. "client_0001"
            with open(f, 'r', errors='replace') as fp:
                for line in fp:
                    # Track timestamp range
                    ts_match = re.match(r'\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d+Z?)\]', line)
                    if ts_match:
                        ts = parse_timestamp(ts_match.group(1))
                        if self.first_ts is None or ts < self.first_ts:
                            self.first_ts = ts
                        if self.last_ts is None or ts > self.last_ts:
                            self.last_ts = ts

                    # Role
                    m = role_pat.search(line)
                    if m:
                        role, cid = m.groups()
                        self.client_roles[cid] = role
                        continue

                    # Auth success (connect time)
                    m = auth_ok.search(line)
                    if m:
                        self.client_connect_times[client_id] = parse_timestamp(m.group(1))
                        continue

                    # Message sent
                    m = msg_sent.search(line)
                    if m:
                        ts_str, msg_type = m.groups()
                        self.total_messages_sent += 1
                        self.sent_by_type[msg_type] += 1
                        minute = parse_timestamp(ts_str).strftime("%Y-%m-%d %H:%M")
                        self.sent_per_minute[minute] += 1
                        self.sent_type_per_minute[minute][msg_type] += 1
                        continue

                    # ACK timeout (individual)
                    m = ack_timeout_pat.search(line)
                    if m and "Max retries" not in line:
                        self.total_ack_timeouts += 1
                        minute = parse_timestamp(m.group(1)).strftime("%Y-%m-%d %H:%M")
                        self.ack_timeout_per_minute[minute] += 1
                        continue

                    # Max retries exceeded
                    m = max_retries.search(line)
                    if m:
                        self.total_max_retries += 1
                        continue

                    # Disconnect
                    m = disconnect.search(line)
                    if m:
                        ts_str, reason = m.groups()
                        self.total_disconnects += 1
                        self.disconnect_reasons[reason] += 1
                        disc_ts = parse_timestamp(ts_str)
                        self.client_disconnect_times[client_id] = disc_ts
                        minute = disc_ts.strftime("%Y-%m-%d %H:%M")
                        self.disconnect_per_minute[minute] += 1
                        continue

            processed += 1
            if processed % 500 == 0:
                print(f"  Parsed {processed}/{self.client_count} clients...")

        print(f"  Parsed {processed}/{self.client_count} clients.")

    def print_report(self):
        print("\n" + "="*70)
        print("CLIENT LOG ANALYSIS REPORT")
        print("="*70)

        if self.first_ts and self.last_ts:
            duration = (self.last_ts - self.first_ts).total_seconds()
            print(f"\nTime span: {self.first_ts} -> {self.last_ts}")
            print(f"Duration: {duration:.0f} seconds")

        # Roles
        warehouses = sum(1 for r in self.client_roles.values() if r == "WAREHOUSE")
        hubs = sum(1 for r in self.client_roles.values() if r == "HUB")
        print(f"\nClients: {self.client_count} total ({warehouses} warehouses, {hubs} hubs)")

        # Connection stats
        print("\n" + "-"*40)
        print("CONNECTION STATISTICS")
        print("-"*40)
        connected = len(self.client_connect_times)
        disconnected = self.total_disconnects
        survived = connected - disconnected
        print(f"Connected:             {connected}")
        print(f"Disconnected:          {disconnected}")
        print(f"Survived:              {survived}")
        if disconnected > 0:
            print(f"\nDisconnect reasons:")
            for reason, count in sorted(self.disconnect_reasons.items(), key=lambda x: -x[1]):
                print(f"  {reason}: {count}")

        # Session lifetimes for disconnected clients
        lifetimes = []
        for cid, disc_ts in self.client_disconnect_times.items():
            if cid in self.client_connect_times:
                lt = (disc_ts - self.client_connect_times[cid]).total_seconds()
                if lt > 0:
                    lifetimes.append(lt)
        if lifetimes:
            lifetimes.sort()
            print(f"\nDisconnected session lifetimes (seconds):")
            print(f"  Min:     {lifetimes[0]:.1f}")
            print(f"  Max:     {lifetimes[-1]:.1f}")
            print(f"  Avg:     {sum(lifetimes)/len(lifetimes):.1f}")
            print(f"  Median:  {lifetimes[len(lifetimes)//2]:.1f}")

        # ACK stats
        print("\n" + "-"*40)
        print("ACK TIMEOUT STATISTICS")
        print("-"*40)
        print(f"Total ACK timeouts:    {self.total_ack_timeouts}")
        print(f"Max retries exceeded:  {self.total_max_retries}")

        # Message stats
        print("\n" + "-"*40)
        print("MESSAGE STATISTICS")
        print("-"*40)
        print(f"Total messages sent:   {self.total_messages_sent}")
        if self.first_ts and self.last_ts:
            dur = max((self.last_ts - self.first_ts).total_seconds(), 1)
            print(f"Avg messages/sec:      {self.total_messages_sent / dur:.1f}")

        print(f"\nMessages by type:")
        for t, c in sorted(self.sent_by_type.items(), key=lambda x: -x[1]):
            print(f"  {t}: {c}")

        # Time-series
        self.print_timeseries()

        print("\n" + "="*70)

    def print_timeseries(self):
        all_minutes = sorted(set(
            list(self.sent_per_minute.keys()) +
            list(self.ack_timeout_per_minute.keys()) +
            list(self.disconnect_per_minute.keys())
        ))

        if not all_minutes:
            return

        print("\n" + "-"*40)
        print("CLIENT TIME-SERIES (per minute)")
        print("-"*40)
        print(f"{'Min':<6} {'Sent':>7} {'ACK_TO':>7} {'Disc':>5}")
        print("-" * 30)

        for minute in all_minutes:
            sent = self.sent_per_minute.get(minute, 0)
            ack_to = self.ack_timeout_per_minute.get(minute, 0)
            disc = self.disconnect_per_minute.get(minute, 0)

            flag = ""
            if ack_to > 100:
                flag += " <<<ACK_STORM"
            if disc > 10:
                flag += " <<<DISCON"

            print(f"{minute[-5:]:<6} {sent:>7} {ack_to:>7} {disc:>5}{flag}")

        # Sent type breakdown per minute (important types only)
        print("\n" + "-"*40)
        print("CLIENT SENT TYPE BREAKDOWN (per minute)")
        print("-"*40)

        all_types = set()
        for minute_types in self.sent_type_per_minute.values():
            all_types.update(minute_types.keys())

        def short_name(t):
            t = t.replace("WAREHOUSE_TO_SERVER__", "W:")
            t = t.replace("HUB_TO_SERVER__", "H:")
            t = t.replace("STOCK_RECEIPT_CONFIRMATION", "RECEIPT")
            t = t.replace("INVENTORY_UPDATE", "INV_UPD")
            t = t.replace("STOCK_REQUEST", "STK_REQ")
            t = t.replace("SHIPMENT_NOTICE", "SHIP")
            t = t.replace("REPLENISH_REQUEST", "REPLENISH")
            t = t.replace("AUTH_REQUEST", "AUTH")
            return t

        type_totals = defaultdict(int)
        for minute_types in self.sent_type_per_minute.values():
            for t, c in minute_types.items():
                type_totals[t] += c
        sorted_types = sorted(type_totals.keys(), key=lambda t: -type_totals[t])

        # Skip ACK for clarity
        important = [t for t in sorted_types if "ACK" not in t][:8]

        header = f"{'Min':<6}"
        for t in important:
            header += f" {short_name(t):>12}"
        print(header)
        print("-" * (6 + 13 * len(important)))

        for minute in all_minutes:
            types = self.sent_type_per_minute.get(minute, {})
            row = f"{minute[-5:]:<6}"
            for t in important:
                row += f" {types.get(t, 0):>12}"
            print(row)

    def run(self):
        print(f"Analyzing client logs in: {self.log_dir}")
        print()
        self.parse_all()
        self.print_report()


def next_filename(base_dir: Path) -> Path:
    i = 1
    while True:
        candidate = base_dir / f"client_analysis_{i}.txt"
        if not candidate.exists():
            return candidate
        i += 1


class TeeStream:
    def __init__(self, file_obj, original_stdout):
        self.file_obj = file_obj
        self.stdout = original_stdout

    def write(self, data):
        self.stdout.write(data)
        self.file_obj.write(data)

    def flush(self):
        self.stdout.flush()
        self.file_obj.flush()


def main():
    if len(sys.argv) > 1:
        log_dir = sys.argv[1]
    else:
        script_dir = Path(__file__).parent
        log_dir = script_dir.parent / "logs" / "clients"

    if not Path(log_dir).exists():
        print(f"Error: Log directory not found: {log_dir}")
        sys.exit(1)

    project_root = Path(__file__).parent.parent
    out_file = next_filename(project_root)

    original_stdout = sys.stdout
    with open(out_file, 'w') as f:
        sys.stdout = TeeStream(f, original_stdout)
        try:
            analyzer = ClientLogAnalyzer(log_dir)
            analyzer.run()
        finally:
            sys.stdout = original_stdout

    print(f"\nReport saved to: {out_file}")


if __name__ == "__main__":
    main()
