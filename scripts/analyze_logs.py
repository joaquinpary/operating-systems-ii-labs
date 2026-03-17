#!/usr/bin/env python3
"""
Server Log Analyzer
Analyzes reactor and worker logs to extract performance metrics.

Usage:
    python analyze_logs.py [log_dir]
    
    log_dir: Directory containing server_reactor.log* and server_worker.log* files
             Defaults to ../logs/server/
"""

import io
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
        
        # Time-series tracking (per-minute buckets)
        self.reactor_recv_per_minute = defaultdict(int)   # minute -> count of TCP recv
        self.worker_done_per_minute = defaultdict(int)    # minute -> count of DONE messages
        self.disconnect_per_minute = defaultdict(int)     # minute -> disconnect count
        self.delays_per_minute = defaultdict(list)        # minute -> [delay_ms, ...]
        self.msg_type_per_minute = defaultdict(lambda: defaultdict(int))  # minute -> {type: count}
        self.ack_retries_per_minute = defaultdict(int)    # minute -> ACK retry count
        self.processing_per_minute = defaultdict(list)    # minute -> [proc_time_ms, ...]
        
        # Session lifetimes
        self.session_connects = {}  # session -> connect time
        self.session_disconnects = {}  # session -> disconnect time
        
        # Per-message-type processing times (IN -> DONE per thread)
        self.processing_times = defaultdict(list)  # msg_type -> [duration_ms, ...]
        
    def find_log_files(self, prefix: str) -> list:
        """Find all log files matching prefix, sorted by age (oldest first)"""
        base = self.log_dir / f"{prefix}.log"
        
        # Discover all numbered backups dynamically
        numbered = []
        for f in self.log_dir.glob(f"{prefix}.log.*"):
            # Extract the number suffix
            suffix = f.name[len(f"{prefix}.log."):]
            if suffix.isdigit():
                numbered.append((int(suffix), f))
        
        # Sort by number descending (highest = oldest)
        numbered.sort(key=lambda x: x[0], reverse=True)
        files = [f for _, f in numbered]
        
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
                        parsed_ts = parse_timestamp(ts)
                        self.reactor_events.append(('recv', parsed_ts, sess))
                        minute = parsed_ts.strftime("%Y-%m-%d %H:%M")
                        self.reactor_recv_per_minute[minute] += 1
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
                        parsed_ts = parse_timestamp(ts)
                        self.session_disconnects[sess] = parsed_ts
                        minute = parsed_ts.strftime("%Y-%m-%d %H:%M")
                        self.disconnect_per_minute[minute] += 1
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
                        ts_match2 = re.match(r'\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2})', line)
                        if ts_match2:
                            self.ack_retries_per_minute[ts_match2.group(1)] += 1
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
        # Pattern: [timestamp] [INFO] [T0] DONE type=... sess=...
        worker_done = re.compile(
            r'\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d+Z?)\].*\[T(\d+)\] DONE type=(\S+) sess=(\S+)'
        )
        
        # Per-thread state for IN->DONE correlation
        thread_state = {}  # thread_id -> (in_time, msg_type)
        
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
                        
                        # Track IN for processing time calculation
                        thread_state[thread_id] = (worker_time, msg_type)
                        
                        # Track message type per minute
                        minute = worker_time.strftime("%Y-%m-%d %H:%M")
                        self.msg_type_per_minute[minute][msg_type] += 1
                        continue
                    
                    # DONE line — correlate with IN on same thread
                    m = worker_done.search(line)
                    if m:
                        log_ts, thread_id, msg_type, sess = m.groups()
                        done_time = parse_timestamp(log_ts)
                        minute = done_time.strftime("%Y-%m-%d %H:%M")
                        self.worker_done_per_minute[minute] += 1
                        if thread_id in thread_state:
                            in_time, in_type = thread_state.pop(thread_id)
                            duration_ms = (done_time - in_time).total_seconds() * 1000
                            if 0 <= duration_ms < 300000:  # sanity: 0-5 min
                                self.processing_times[in_type].append(duration_ms)
                                self.processing_per_minute[minute].append(duration_ms)
                        continue
    
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
                delay_ms = (worker_ts - msg_time).total_seconds() * 1000
                if 0 < delay_ms < 300000:  # sanity check: 0-5 minutes
                    self.worker_delays_ms.append(delay_ms)
                    minute = worker_ts.strftime("%Y-%m-%d %H:%M")
                    self.delays_per_minute[minute].append(delay_ms)
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
        
        # Worker processing time per message type
        print("\n" + "-"*40)
        print("WORKER PROCESSING TIME PER MESSAGE TYPE")
        print("-"*40)
        if self.processing_times:
            # Summary table sorted by total time (biggest consumer first)
            type_stats = []
            for msg_type, times in self.processing_times.items():
                s = sorted(times)
                type_stats.append({
                    'type': msg_type,
                    'count': len(s),
                    'total': sum(s),
                    'min': min(s),
                    'max': max(s),
                    'avg': sum(s) / len(s),
                    'median': s[len(s) // 2],
                    'p95': s[int(len(s) * 0.95)] if len(s) >= 20 else s[-1],
                    'p99': s[int(len(s) * 0.99)] if len(s) >= 100 else s[-1],
                })
            type_stats.sort(key=lambda x: -x['total'])
            
            grand_total = sum(t['total'] for t in type_stats)
            total_msgs = sum(t['count'] for t in type_stats)
            print(f"Total messages:  {total_msgs}")
            print(f"Total time:      {grand_total:.0f} ms ({grand_total/1000:.1f} s)")
            
            print(f"\n{'Type':<50} {'Count':>7} {'Avg ms':>8} {'Med ms':>8} {'P95 ms':>8} {'P99 ms':>8} {'Max ms':>8} {'Total%':>7}")
            print("-" * 115)
            for t in type_stats:
                pct = 100 * t['total'] / grand_total if grand_total > 0 else 0
                print(f"{t['type']:<50} {t['count']:>7} {t['avg']:>8.1f} {t['median']:>8.1f} {t['p95']:>8.1f} {t['p99']:>8.1f} {t['max']:>8.1f} {pct:>6.1f}%")
            
            # Distribution of ALL processing times
            all_times = sorted(t for times in self.processing_times.values() for t in times)
            buckets = [0, 1, 5, 10, 50, 100, 500, 1000, float('inf')]
            bucket_names = ['<1ms', '1-5ms', '5-10ms', '10-50ms', '50-100ms', '100-500ms', '500ms-1s', '>1s']
            bucket_counts = [0] * len(bucket_names)
            for d in all_times:
                for i in range(len(buckets) - 1):
                    if buckets[i] <= d < buckets[i+1]:
                        bucket_counts[i] += 1
                        break
            print(f"\nProcessing time distribution (all types):")
            for name, count in zip(bucket_names, bucket_counts):
                pct = 100 * count / len(all_times)
                bar = '#' * int(pct / 2)
                print(f"  {name:>12}: {count:>6} ({pct:5.1f}%) {bar}")
        else:
            print("No DONE lines found (requires updated server with DONE logging)")
        
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
        
        # Time-series per-minute breakdown
        self.print_timeseries()
        
        print("\n" + "="*70)
    
    def print_timeseries(self):
        """Print per-minute time-series showing input/output rate, delay, disconnects, and message breakdown"""
        all_minutes = sorted(set(
            list(self.reactor_recv_per_minute.keys()) +
            list(self.worker_done_per_minute.keys()) +
            list(self.disconnect_per_minute.keys()) +
            list(self.delays_per_minute.keys()) +
            list(self.msg_type_per_minute.keys())
        ))
        
        if not all_minutes:
            return
        
        print("\n" + "-"*40)
        print("TIME-SERIES (per minute)")
        print("-"*40)
        print(f"{'Min':<6} {'Recv':>7} {'Done':>7} {'Delta':>7} {'DlyAvg':>7} {'DlyP95':>7} {'ProcAvg':>8} {'Retries':>7} {'Disc':>5}")
        print("-" * 73)
        
        for minute in all_minutes:
            recv = self.reactor_recv_per_minute.get(minute, 0)
            done = self.worker_done_per_minute.get(minute, 0)
            delta = recv - done
            disc = self.disconnect_per_minute.get(minute, 0)
            retries = self.ack_retries_per_minute.get(minute, 0)
            
            delays = self.delays_per_minute.get(minute, [])
            if delays:
                avg_delay = sum(delays) / len(delays)
                sorted_d = sorted(delays)
                p95_delay = sorted_d[int(len(sorted_d) * 0.95)] if len(sorted_d) >= 20 else sorted_d[-1]
                delay_avg_str = f"{avg_delay:.0f}"
                delay_p95_str = f"{p95_delay:.0f}"
            else:
                delay_avg_str = "-"
                delay_p95_str = "-"
            
            procs = self.processing_per_minute.get(minute, [])
            if procs:
                proc_avg_str = f"{sum(procs)/len(procs):.1f}"
            else:
                proc_avg_str = "-"
            
            # Flag warning
            flag = ""
            if delta > 500:
                flag += " <<<BACKLOG"
            if disc > 10:
                flag += " <<<DISCON"
            
            short_min = minute[-5:]
            print(f"{short_min:<6} {recv:>7} {done:>7} {delta:>+7} {delay_avg_str:>7} {delay_p95_str:>7} {proc_avg_str:>8} {retries:>7} {disc:>5}{flag}")
        
        # Message type breakdown per minute
        print("\n" + "-"*40)
        print("MESSAGE TYPE BREAKDOWN (per minute)")
        print("-"*40)
        
        # Collect all message types that appeared, use short names
        all_types = set()
        for minute_types in self.msg_type_per_minute.values():
            all_types.update(minute_types.keys())
        
        # Short name mapping
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
        
        # Sort types by total volume
        type_totals = defaultdict(int)
        for minute_types in self.msg_type_per_minute.values():
            for t, c in minute_types.items():
                type_totals[t] += c
        sorted_types = sorted(type_totals.keys(), key=lambda t: -type_totals[t])
        
        # Only show top types (skip ACK/KEEPALIVE for clarity)
        important_types = [t for t in sorted_types if "ACK" not in t and "KEEPALIVE" not in t][:6]
        
        # Header
        header = f"{'Min':<6}"
        for t in important_types:
            sn = short_name(t)
            header += f" {sn:>12}"
        print(header)
        print("-" * (6 + 13 * len(important_types)))
        
        for minute in all_minutes:
            types = self.msg_type_per_minute.get(minute, {})
            row = f"{minute[-5:]:<6}"
            for t in important_types:
                count = types.get(t, 0)
                row += f" {count:>12}"
            print(row)
    
    def run(self):
        """Run full analysis"""
        print(f"Analyzing logs in: {self.log_dir}")
        print()
        
        self.parse_reactor_logs()
        self.parse_worker_logs()
        self.calculate_worker_delays()
        self.print_report()


def next_analysis_filename(base_dir: Path) -> Path:
    """Find the next available data_analysis_X.txt filename"""
    i = 1
    while True:
        candidate = base_dir / f"data_analysis_{i}.txt"
        if not candidate.exists():
            return candidate
        i += 1


class TeeStream:
    """Write to both stdout and a file simultaneously"""
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
        # Default: look for logs relative to script location
        script_dir = Path(__file__).parent
        log_dir = script_dir.parent / "logs" / "server"
    
    if not Path(log_dir).exists():
        print(f"Error: Log directory not found: {log_dir}")
        sys.exit(1)
    
    # Determine output file
    project_root = Path(__file__).parent.parent
    out_file = next_analysis_filename(project_root)
    
    # Tee stdout to both console and file
    original_stdout = sys.stdout
    with open(out_file, 'w') as f:
        sys.stdout = TeeStream(f, original_stdout)
        try:
            analyzer = LogAnalyzer(log_dir)
            analyzer.run()
        finally:
            sys.stdout = original_stdout
    
    print(f"\nReport saved to: {out_file}")


if __name__ == "__main__":
    main()
