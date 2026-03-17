#!/usr/bin/env python3
"""
Server Log Analyzer
Analyzes reactor and worker logs to extract performance metrics.

Usage:
    python analyze_logs.py [log_dir]
    
    log_dir: Directory containing server_reactor.log* and server_worker.log* files
             Defaults to ../logs/server/
"""

import os
import re
import sys
from collections import defaultdict
from datetime import datetime
from pathlib import Path


def parse_timestamp(ts_str: str) -> datetime:
    """Parse log timestamp like '2026-03-12 06:42:22.557Z'"""
    # Remove trailing Z and parse
    ts_str = ts_str.rstrip('Z')
    try:
        return datetime.strptime(ts_str, "%Y-%m-%d %H:%M:%S.%f")
    except ValueError:
        return datetime.strptime(ts_str, "%Y-%m-%d %H:%M:%S")


def parse_msg_timestamp(ts_str: str) -> datetime:
    """Parse message timestamp like '2026-03-12T06:42:22.557Z'"""
    ts_str = ts_str.rstrip('Z')
    try:
        return datetime.strptime(ts_str, "%Y-%m-%dT%H:%M:%S.%f")
    except ValueError:
        return datetime.strptime(ts_str, "%Y-%m-%dT%H:%M:%S")


class LogAnalyzer:
    def __init__(self, log_dir: str):
        self.log_dir = Path(log_dir)
        
        # Metrics
        self.reactor_events = []
        self.worker_events = []
        
        # Per-message tracking for delay calculation
        self.msg_reactor_times = {}  # (session, ts) -> reactor receive time
        self.msg_worker_times = {}   # (session, ts) -> worker process time
        
        # Counters
        self.disconnects = 0
        self.keepalive_timeouts = 0
        self.ack_timeouts = 0
        self.ack_timeout_max_retries = 0
        self.send_fails = 0
        self.no_session_errors = 0
        
        # ACK timeout retries per session
        self.retry_counts = defaultdict(int)
        
        # Throughput tracking
        self.reactor_events_per_second = defaultdict(int)
        self.worker_events_per_second = defaultdict(int)
        
        # Worker delays (time from reactor receive to worker process)
        self.worker_delays_ms = []
        
        # Session lifetimes
        self.session_connects = {}  # session -> connect time
        self.session_disconnects = {}  # session -> disconnect time
        
    def find_log_files(self, prefix: str) -> list:
        """Find all log files matching prefix, sorted by age (oldest first)"""
        files = []
        base = self.log_dir / f"{prefix}.log"
        
        # Find numbered backups first (oldest)
        for i in range(10, 0, -1):
            f = self.log_dir / f"{prefix}.log.{i}"
            if f.exists():
                files.append(f)
        
        # Then current log (newest)
        if base.exists():
            files.append(base)
            
        return files
    
    def parse_reactor_logs(self):
        """Parse all reactor log files"""
        files = self.find_log_files("server_reactor")
        print(f"Found {len(files)} reactor log files")
        
        # Patterns
        tcp_recv = re.compile(r'\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d+Z?)\].*\[TCP\] <- sess=(\S+) len=')
        tcp_connect = re.compile(r'\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d+Z?)\].*\[TCP\] connect from=\S+ sess=(\S+)')
        tcp_disconnect = re.compile(r'\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d+Z?)\].*\[TCP\] disconnect user=(\S+) sess=(\S+)')
        keepalive_timeout = re.compile(r'\[KEEPALIVE_TIMEOUT\] sess=(\S+) disconnecting')
        ack_timeout = re.compile(r'\[ACK_TIMEOUT\] sess=(\S+) ts=(\S+) retry=(\d+)/(\d+)')
        ack_timeout_max = re.compile(r'\[ACK_TIMEOUT\] MAX RETRIES sess=(\S+)')
        send_fail = re.compile(r'\[SERVER\] send fail sess=(\S+)')
        no_session = re.compile(r'\[DISPATCH\] SEND no session for user=(\S+)')
        
        for f in files:
            print(f"  Parsing {f.name}...")
            with open(f, 'r', errors='replace') as fp:
                for line in fp:
                    # Track timestamp for throughput
                    ts_match = re.match(r'\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})', line)
                    if ts_match:
                        second = ts_match.group(1)
                        self.reactor_events_per_second[second] += 1
                    
                    # TCP receive (message arrival)
                    m = tcp_recv.search(line)
                    if m:
                        ts, sess = m.groups()
                        self.reactor_events.append(('recv', parse_timestamp(ts), sess))
                        continue
                    
                    # TCP connect
                    m = tcp_connect.search(line)
                    if m:
                        ts, sess = m.groups()
                        self.session_connects[sess] = parse_timestamp(ts)
                        continue
                    
                    # TCP disconnect
                    m = tcp_disconnect.search(line)
                    if m:
                        ts, user, sess = m.groups()
                        self.disconnects += 1
                        self.session_disconnects[sess] = parse_timestamp(ts)
                        continue
                    
                    # Keepalive timeout
                    if keepalive_timeout.search(line):
                        self.keepalive_timeouts += 1
                        continue
                    
                    # ACK timeout retry
                    m = ack_timeout.search(line)
                    if m:
                        sess, ts_key, retry, max_retry = m.groups()
                        self.ack_timeouts += 1
                        self.retry_counts[sess] += 1
                        continue
                    
                    # ACK timeout max retries (blacklist)
                    if ack_timeout_max.search(line):
                        self.ack_timeout_max_retries += 1
                        continue
                    
                    # Send fail
                    if send_fail.search(line):
                        self.send_fails += 1
                        continue
                    
                    # No session for user
                    if no_session.search(line):
                        self.no_session_errors += 1
                        continue
    
    def parse_worker_logs(self):
        """Parse all worker log files"""
        files = self.find_log_files("server_worker")
        print(f"Found {len(files)} worker log files")
        
        # Pattern: [timestamp] [INFO] [T0] IN type=... ts=... from=... sess=...
        worker_in = re.compile(
            r'\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d+Z?)\].*\[T(\d+)\] IN type=(\S+) ts=(\S+) from=(\S+) sess=(\S+)'
        )
        
        for f in files:
            print(f"  Parsing {f.name}...")
            with open(f, 'r', errors='replace') as fp:
                for line in fp:
                    # Track timestamp for throughput
                    ts_match = re.match(r'\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})', line)
                    if ts_match:
                        second = ts_match.group(1)
                        self.worker_events_per_second[second] += 1
                    
                    m = worker_in.search(line)
                    if m:
                        log_ts, thread_id, msg_type, msg_ts, source, sess = m.groups()
                        worker_time = parse_timestamp(log_ts)
                        self.worker_events.append(('process', worker_time, sess, msg_ts))
                        
                        # Store for delay calculation
                        key = (sess, msg_ts)
                        self.msg_worker_times[key] = worker_time
    
    def calculate_worker_delays(self):
        """
        Calculate delay between reactor receive and worker process.
        This is tricky because we need to correlate messages by session + timestamp.
        For now, we'll estimate based on message timestamps in the reactor vs worker.
        """
        # Build a map of (session, second) -> first reactor receive time
        reactor_by_sess_second = {}
        for event in self.reactor_events:
            if event[0] == 'recv':
                _, ts, sess = event
                key = (sess, ts.strftime("%Y-%m-%d %H:%M:%S"))
                if key not in reactor_by_sess_second:
                    reactor_by_sess_second[key] = ts
        
        # Match with worker process times
        for key, worker_ts in self.msg_worker_times.items():
            sess, msg_ts = key
            try:
                msg_time = parse_msg_timestamp(msg_ts)
                # The msg_ts is when the CLIENT created the message
                # reactor should receive it almost immediately
                # delay = worker_ts - msg_time gives us total client->worker latency
                delay_ms = (worker_ts - msg_time).total_seconds() * 1000
                if 0 < delay_ms < 300000:  # sanity check: 0-5 minutes
                    self.worker_delays_ms.append(delay_ms)
            except:
                pass
    
    def print_report(self):
        """Print analysis report"""
        print("\n" + "="*70)
        print("SERVER LOG ANALYSIS REPORT")
        print("="*70)
        
        # Time span
        all_seconds = sorted(set(self.reactor_events_per_second.keys()) | 
                            set(self.worker_events_per_second.keys()))
        if all_seconds:
            print(f"\nTime span: {all_seconds[0]} -> {all_seconds[-1]}")
            print(f"Duration: {len(all_seconds)} seconds of activity")
        
        # Connection stats
        print("\n" + "-"*40)
        print("CONNECTION STATISTICS")
        print("-"*40)
        print(f"Total TCP connects:        {len(self.session_connects)}")
        print(f"Total TCP disconnects:     {self.disconnects}")
        print(f"Keepalive timeouts:        {self.keepalive_timeouts}")
        print(f"ACK timeout max retries:   {self.ack_timeout_max_retries}")
        
        # Calculate session lifetimes
        lifetimes = []
        for sess, conn_time in self.session_connects.items():
            if sess in self.session_disconnects:
                lifetime = (self.session_disconnects[sess] - conn_time).total_seconds()
                lifetimes.append(lifetime)
        
        if lifetimes:
            print(f"\nSession lifetimes (seconds):")
            print(f"  Min:     {min(lifetimes):.1f}")
            print(f"  Max:     {max(lifetimes):.1f}")
            print(f"  Avg:     {sum(lifetimes)/len(lifetimes):.1f}")
            print(f"  Median:  {sorted(lifetimes)[len(lifetimes)//2]:.1f}")
        
        # Error stats
        print("\n" + "-"*40)
        print("ERROR STATISTICS")
        print("-"*40)
        print(f"ACK timeout retries:       {self.ack_timeouts}")
        print(f"Send fails (dead session): {self.send_fails}")
        print(f"No session for user:       {self.no_session_errors}")
        
        # Retry distribution
        if self.retry_counts:
            retry_values = list(self.retry_counts.values())
            print(f"\nRetries per session:")
            print(f"  Sessions with retries:   {len(retry_values)}")
            print(f"  Max retries per session: {max(retry_values)}")
            print(f"  Avg retries per session: {sum(retry_values)/len(retry_values):.1f}")
        
        # Worker delay stats
        print("\n" + "-"*40)
        print("WORKER QUEUE DELAY (client timestamp -> worker process)")
        print("-"*40)
        if self.worker_delays_ms:
            delays = sorted(self.worker_delays_ms)
            print(f"Samples:   {len(delays)}")
            print(f"Min:       {min(delays):.1f} ms")
            print(f"Max:       {max(delays):.1f} ms")
            print(f"Avg:       {sum(delays)/len(delays):.1f} ms")
            print(f"Median:    {delays[len(delays)//2]:.1f} ms")
            print(f"P95:       {delays[int(len(delays)*0.95)]:.1f} ms")
            print(f"P99:       {delays[int(len(delays)*0.99)]:.1f} ms")
            
            # Distribution buckets
            buckets = [0, 10, 100, 1000, 5000, 10000, 30000, 60000, float('inf')]
            bucket_names = ['<10ms', '10-100ms', '100ms-1s', '1-5s', '5-10s', '10-30s', '30-60s', '>60s']
            bucket_counts = [0] * len(bucket_names)
            for d in delays:
                for i in range(len(buckets) - 1):
                    if buckets[i] <= d < buckets[i+1]:
                        bucket_counts[i] += 1
                        break
            print(f"\nDelay distribution:")
            for name, count in zip(bucket_names, bucket_counts):
                pct = 100 * count / len(delays)
                bar = '#' * int(pct / 2)
                print(f"  {name:>10}: {count:>6} ({pct:5.1f}%) {bar}")
        else:
            print("No worker delay data available")
        
        # Throughput stats
        print("\n" + "-"*40)
        print("THROUGHPUT (events per second)")
        print("-"*40)
        if self.reactor_events_per_second:
            reactor_eps = list(self.reactor_events_per_second.values())
            print(f"Reactor events/sec:")
            print(f"  Min:     {min(reactor_eps)}")
            print(f"  Max:     {max(reactor_eps)}")
            print(f"  Avg:     {sum(reactor_eps)/len(reactor_eps):.1f}")
        
        if self.worker_events_per_second:
            worker_eps = list(self.worker_events_per_second.values())
            print(f"Worker events/sec:")
            print(f"  Min:     {min(worker_eps)}")
            print(f"  Max:     {max(worker_eps)}")
            print(f"  Avg:     {sum(worker_eps)/len(worker_eps):.1f}")
        
        # Find seconds with highest load
        print(f"\nBusiest seconds (reactor):")
        busiest = sorted(self.reactor_events_per_second.items(), key=lambda x: -x[1])[:5]
        for ts, count in busiest:
            print(f"  {ts}: {count} events")
        
        # Cascade detection
        print("\n" + "-"*40)
        print("CASCADE DETECTION")
        print("-"*40)
        
        # Find periods where disconnects spike
        disconnect_per_second = defaultdict(int)
        for sess, ts in self.session_disconnects.items():
            second = ts.strftime("%Y-%m-%d %H:%M:%S")
            disconnect_per_second[second] += 1
        
        if disconnect_per_second:
            max_disc_sec = max(disconnect_per_second.values())
            if max_disc_sec > 10:
                print(f"WARNING: Cascade detected! Max {max_disc_sec} disconnects/second")
                print(f"Seconds with >10 disconnects:")
                for ts, count in sorted(disconnect_per_second.items()):
                    if count > 10:
                        print(f"  {ts}: {count} disconnects")
            else:
                print(f"No cascade detected (max {max_disc_sec} disconnects/second)")
        
        print("\n" + "="*70)
    
    def run(self):
        """Run full analysis"""
        print(f"Analyzing logs in: {self.log_dir}")
        print()
        
        self.parse_reactor_logs()
        self.parse_worker_logs()
        self.calculate_worker_delays()
        self.print_report()


def main():
    if len(sys.argv) > 1:
        log_dir = sys.argv[1]
    else:
        # Default: look for logs relative to script location
        script_dir = Path(__file__).parent
        log_dir = script_dir.parent / "logs" / "server"
    
    if not Path(log_dir).exists():
        print(f"Error: Log directory not found: {log_dir}")
        sys.exit(1)
    
    analyzer = LogAnalyzer(log_dir)
    analyzer.run()


if __name__ == "__main__":
    main()
