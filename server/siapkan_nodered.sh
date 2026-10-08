#!/usr/bin/env bash
# siapkan_nodered.sh - dijalankan SEKALI di server setelah ./pasang.sh influxdb tubes.
# Memasang palette InfluxDB ke Node-RED server lalu memasang flow pencatat Tugas Besar.
set -euo pipefail
cd "$(dirname "$0")"
P="--profile influxdb --profile tubes"
docker compose $P exec -T -w /data nodered sh -c "[ -d node_modules/node-red-contrib-influxdb ] || npm install --omit=dev --no-audit --no-fund node-red-contrib-influxdb"
docker compose $P restart nodered
for i in $(seq 1 60); do curl -s -o /dev/null http://127.0.0.1:1880/flows && break; sleep 3; done
python3 nodered/pasang_flow.py
