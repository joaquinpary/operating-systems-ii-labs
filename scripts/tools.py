#!/usr/bin/env python3
"""
LoRa-CHADS Admin Tools — unified interactive CLI.

Usage:
    python3 scripts/tools.py
"""

import sys
import os

# Ensure the scripts/ directory is in the path so 'modules' is importable
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from modules import gen_credentials
from modules import admin_cli
from modules import rest_api_client
from modules import benchmark
from modules import profiling_graph


# ── ANSI helpers ────────────────────────────────────────────────────────────

BOLD = "\033[1m"
DIM = "\033[2m"
CYAN = "\033[36m"
GREEN = "\033[32m"
YELLOW = "\033[33m"
RED = "\033[31m"
RESET = "\033[0m"


def _header():
    print()
    print(f"  {CYAN}{BOLD}╔═══════════════════════════════════════╗{RESET}")
    print(f"  {CYAN}{BOLD}║       LoRa-CHADS Admin Tools          ║{RESET}")
    print(f"  {CYAN}{BOLD}╠═══════════════════════════════════════╣{RESET}")
    print(f"  {CYAN}{BOLD}║{RESET}  {GREEN}1){RESET} Generate Credentials              {CYAN}{BOLD}║{RESET}")
    print(f"  {CYAN}{BOLD}║{RESET}  {GREEN}2){RESET} Admin CLI                         {CYAN}{BOLD}║{RESET}")
    print(f"  {CYAN}{BOLD}║{RESET}  {GREEN}3){RESET} REST API Client                   {CYAN}{BOLD}║{RESET}")
    print(f"  {CYAN}{BOLD}║{RESET}  {GREEN}4){RESET} Benchmark                         {CYAN}{BOLD}║{RESET}")
    print(f"  {CYAN}{BOLD}║{RESET}  {GREEN}5){RESET} Profiling Graph                   {CYAN}{BOLD}║{RESET}")
    print(f"  {CYAN}{BOLD}║{RESET}  {RED}0){RESET} Exit                              {CYAN}{BOLD}║{RESET}")
    print(f"  {CYAN}{BOLD}╚═══════════════════════════════════════╝{RESET}")
    print()


def _prompt(text, default=None):
    """Prompt for input with an optional default value."""
    if default is not None:
        raw = input(f"  {text} [{default}]: ").strip()
        return raw if raw else str(default)
    return input(f"  {text}: ").strip()


def _prompt_int(text, default=None):
    """Prompt for an integer."""
    while True:
        raw = _prompt(text, default)
        try:
            return int(raw)
        except ValueError:
            print(f"  {RED}Invalid number. Try again.{RESET}")


def _prompt_float(text, default=None):
    """Prompt for a float."""
    while True:
        raw = _prompt(text, default)
        try:
            return float(raw)
        except ValueError:
            print(f"  {RED}Invalid number. Try again.{RESET}")


def _pause():
    input(f"\n  {DIM}Press Enter to return to the menu...{RESET}")


def _print_result_error(result):
    if isinstance(result, dict) and result.get("status") == "error":
        print(f"  {RED}{result.get('message', 'Unknown error')}{RESET}")


# ── Option 1: Generate Credentials ─────────────────────────────────────────

def menu_generate_credentials():
    print(f"\n  {BOLD}── Generate Credentials ──────────────────{RESET}\n")
    num = _prompt_int("Number of clients", 2000)
    print()
    gen_credentials.generate_configs(num)
    _pause()


# ── Option 2: Admin CLI ────────────────────────────────────────────────────

def menu_admin_cli():
    print(f"\n  {BOLD}── Admin CLI ─────────────────────────────{RESET}\n")
    print(f"  Auth mode:")
    print(f"    {GREEN}1){RESET} CLI Admin (default)")
    print(f"    {GREEN}2){RESET} HUB client")
    print(f"    {GREEN}3){RESET} Warehouse client")
    print()

    choice = _prompt("Select", "1")

    try:
        if choice == "1":
            conf, path = admin_cli.find_cli_conf()
        elif choice == "2":
            num_str = _prompt("Client number (Enter for first available)", "")
            number = int(num_str) if num_str else None
            conf, path = admin_cli.find_client_conf("hub", number)
        elif choice == "3":
            num_str = _prompt("Client number (Enter for first available)", "")
            number = int(num_str) if num_str else None
            conf, path = admin_cli.find_client_conf("wh", number)
        else:
            print(f"  {RED}Invalid option.{RESET}")
            _pause()
            return
    except FileNotFoundError as e:
        print(f"  {RED}[ERROR] {e}{RESET}")
        _pause()
        return

    print(f"  {DIM}Config: {path}{RESET}")
    print(f"  {DIM}Client: id={conf['username']} type={conf['type']}{RESET}")
    print(f"  {DIM}Type 'quit' to return to the main menu.{RESET}\n")

    session = admin_cli.CLISession(
        client_id=conf['username'],
        client_type=conf['type'],
        username=conf['username'],
        password=conf['password'],
    )
    session.run()


# ── Option 3: REST API Client ──────────────────────────────────────────────

def _rest_header(base_url):
    print(f"\n  {BOLD}── REST API Client ───────────────────────{RESET}")
    print(f"  {DIM}Base URL: {base_url}{RESET}\n")
    print(f"    {GREEN}1){RESET} Upload Map")
    print(f"    {GREEN}2){RESET} Flow Capacity")
    print(f"    {GREEN}3){RESET} Circuit Solver")
    print(f"    {GREEN}4){RESET} Get Results")
    print(f"    {GREEN}5){RESET} Generate Map")
    print(f"    {RED}0){RESET} ← Back to main menu")
    print()


def menu_rest_api():
    print(f"\n  {BOLD}── REST API Client ───────────────────────{RESET}\n")
    base_url = _prompt("Base URL",
                       rest_api_client.DEFAULT_BASE_URL)

    while True:
        _rest_header(base_url)
        choice = _prompt("Select", "0")

        try:
            if choice == "1":
                file_path = _prompt("JSON file path")
                rest_api_client.upload_map(base_url, file_path)
                _pause()

            elif choice == "2":
                source = _prompt("Source node")
                sink = _prompt("Sink node")
                rest_api_client.flow_capacity(base_url, source, sink)
                _pause()

            elif choice == "3":
                start = _prompt("Start node")
                rest_api_client.circuit_solver(base_url, start)
                _pause()

            elif choice == "4":
                rest_api_client.get_results(base_url)
                _pause()

            elif choice == "5":
                nodes = _prompt_int("Number of nodes", 10)
                density = _prompt_float("Edge density (0.0-1.0)", 0.3)
                active_prob = _prompt_float("Active probability (0.0-1.0)", 0.9)
                secure_prob = _prompt_float("Secure probability (0.0-1.0)", 0.9)
                output = _prompt("Output file (Enter to print to stdout)", "")
                output = output if output else None

                upload_yn = _prompt("Upload to server? (y/N)", "N")
                upload = upload_yn.lower() in ("y", "yes")

                rest_api_client.generate_map(
                    base_url,
                    nodes=nodes,
                    density=density,
                    active_prob=active_prob,
                    secure_prob=secure_prob,
                    output=output,
                    upload=upload,
                )
                _pause()

            elif choice == "0":
                return
            else:
                print(f"  {RED}Invalid option.{RESET}")

        except KeyboardInterrupt:
            print()
            return


def menu_benchmark():
    print(f"\n  {BOLD}── Benchmark ───────────────────────────{RESET}\n")
    base_url = _prompt("Base URL", rest_api_client.DEFAULT_BASE_URL)
    print(f"  {GREEN}1){RESET} Fulfillment Flow")
    print(f"  {GREEN}2){RESET} Fulfillment Circuit")
    print(f"  {GREEN}3){RESET} Both")
    print(f"  {RED}0){RESET} Back")
    print()

    choice = _prompt("Select", "0")
    if choice == "0":
        return

    maps_dir = None
    reuse = _prompt("Reuse maps from previous benchmark? (y/N)", "N")
    if reuse.lower() in ("y", "yes"):
        maps_dir = _prompt("Maps directory path")
        if not maps_dir:
            print(f"  {RED}No path provided, generating new maps.{RESET}")
            maps_dir = None

    try:
        if choice in {"1", "3"}:
            max_nodes = _prompt_int("Flow max nodes", 1000)
            step = _prompt_int("Flow node step", 10)
            iterations = _prompt_int("Flow iterations", 5)
            density = _prompt_float("Flow edge density (0.0-1.0)", 0.3)
            result = benchmark.run_flow_benchmark(
                base_url,
                max_nodes=max_nodes,
                step=step,
                iterations=iterations,
                density=density,
                maps_dir=maps_dir,
            )
            _print_result_error(result)

        if choice in {"2", "3"}:
            max_nodes = _prompt_int("Circuit max nodes", 15)
            iterations = _prompt_int("Circuit iterations", 5)
            density = _prompt_float("Circuit edge density (0.0-1.0)", 0.5)
            result = benchmark.run_circuit_benchmark(
                base_url,
                max_nodes=max_nodes,
                iterations=iterations,
                density=density,
                maps_dir=maps_dir,
            )
            _print_result_error(result)

        if choice not in {"1", "2", "3"}:
            print(f"  {RED}Invalid option.{RESET}")
    except KeyboardInterrupt:
        print()

    _pause()


def menu_profiling_graph():
    print(f"\n  {BOLD}── Profiling Graph ─────────────────────{RESET}\n")
    print(f"  {GREEN}1){RESET} Fetch from server")
    print(f"  {GREEN}2){RESET} Load benchmark JSON")
    print(f"  {RED}0){RESET} Back")
    print()

    choice = _prompt("Select", "0")
    if choice == "0":
        return

    output_path = _prompt("Output PNG path (Enter for default)", "")
    output_path = output_path if output_path else None

    try:
        if choice == "1":
            base_url = _prompt("Base URL", rest_api_client.DEFAULT_BASE_URL)
            result = profiling_graph.plot_from_server(base_url, output_path=output_path)
            _print_result_error(result)
        elif choice == "2":
            file_path = _prompt("Benchmark JSON file path")
            result = profiling_graph.plot_from_file(file_path, output_path=output_path)
            _print_result_error(result)
        else:
            print(f"  {RED}Invalid option.{RESET}")
    except KeyboardInterrupt:
        print()

    _pause()


# ── Main loop ───────────────────────────────────────────────────────────────

def main():
    while True:
        _header()
        choice = _prompt("Select an option", "0")

        try:
            if choice == "1":
                menu_generate_credentials()
            elif choice == "2":
                menu_admin_cli()
            elif choice == "3":
                menu_rest_api()
            elif choice == "4":
                menu_benchmark()
            elif choice == "5":
                menu_profiling_graph()
            elif choice == "0":
                print(f"\n  {DIM}Bye!{RESET}\n")
                break
            else:
                print(f"  {RED}Invalid option.{RESET}")
        except KeyboardInterrupt:
            print()
            continue


if __name__ == "__main__":
    main()
