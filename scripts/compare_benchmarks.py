#!/usr/bin/env python3
import json
import sys
from pathlib import Path

def main():
    if len(sys.argv) < 3:
        print("Uso: python3 compare_benchmarks.py <benchmark1.json> <benchmark2.json>")
        return

    path1 = Path(sys.argv[1])
    path2 = Path(sys.argv[2])

    if not path1.exists() or not path2.exists():
        print("Error: Uno o ambos archivos referenciados no existen.")
        return

    with open(path1, "r", encoding="utf-8") as f:
        data1 = json.load(f)

    with open(path2, "r", encoding="utf-8") as f:
        data2 = json.load(f)

    algo1 = data1.get("algorithm")
    algo2 = data2.get("algorithm")

    if algo1 != algo2:
        print(f"Error: Los algoritmos no coinciden. El Archivo 1 fue generado por '{algo1}', pero el Archivo 2 por '{algo2}'.")
        return

    is_flow = (algo1 == "fulfillment-flow")
    value_key = "max_flow" if is_flow else "circuit_count"
    value_name = "Flujo Máximo" if is_flow else "Caminos (Circuitos)"

    # Si hay iteraciones, agrupamos por node_count tomando el promedio de los valores o el valor principal
    def extract_records(data):
        records = {}
        for r in data.get("records", []):
            nc = r["node_count"]
            # Tomamos circuit_count o max_flow
            val = r.get(value_key, 0)
            if nc not in records:
                records[nc] = []
            records[nc].append(val)
        
        # Guardaremos el promedio en caso de múltiples iteraciones, aunque suele ser determinístico si reusamos el mismo mapa.
        return {nc: sum(vals)/len(vals) for nc, vals in records.items()}

    recs1 = extract_records(data1)
    recs2 = extract_records(data2)

    all_nodes = sorted(set(recs1.keys()) | set(recs2.keys()))
    
    print(f"\nComparando {algo1} ({value_name})")
    print(f"Archivo 1: {path1.name}")
    print(f"Archivo 2: {path2.name}\n")
    print(f"{'Nodos':>6} | {'Archivo 1':>15} | {'Archivo 2':>15} | {'Diferencia':>15} | ¿Coinciden?")
    print("-" * 75)

    match_count = 0
    total_compared = 0
    tolerance = 1e-4

    for nodes in all_nodes:
        val1 = recs1.get(nodes)
        val2 = recs2.get(nodes)

        v1_str = f"{val1:15.2f}" if val1 is not None else "            N/A"
        v2_str = f"{val2:15.2f}" if val2 is not None else "            N/A"
        
        match = "No"
        diff_str = "            N/A"
        
        if val1 is not None and val2 is not None:
            total_compared += 1
            diff = abs(val1 - val2)
            diff_str = f"{diff:15.2f}"
            if diff <= tolerance:
                match = "Sí"
                match_count += 1

        print(f"{nodes:6} | {v1_str} | {v2_str} | {diff_str} | {match}")

    print("-" * 75)
    if total_compared > 0:
        print(f"Resultados: Coincidieron en {match_count} de {total_compared} comparaciones ({(match_count/total_compared)*100:.1f}%)")
    else:
        print("No hubieron nodos en común para comparar.")
    print()

if __name__ == "__main__":
    main()
