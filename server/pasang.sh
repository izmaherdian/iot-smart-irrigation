#!/usr/bin/env bash
# pasang.sh - memasang / memperbarui Server Praktikum di server (jalankan DI SERVER, di dalam folder ini).
#   ./pasang.sh           : buat file password MQTT dari .env lalu nyalakan broker
#   ./pasang.sh influxdb  : sama, ditambah InfluxDB (lalu jalankan siapkan_influx.sh sekali)
#   ./pasang.sh influxdb tubes : ditambah Node-RED pencatat Tugas Besar (lalu jalankan siapkan_nodered.sh sekali)
set -euo pipefail
cd "$(dirname "$0")"
set -a; . ./.env; set +a

# file password Mosquitto (di-hash) dibuat dari MQTT_USER dan MQTT_PASSWORD
docker run --rm -v "$PWD/mosquitto/config:/mosquitto/config" eclipse-mosquitto:2 \
  sh -c "rm -f /mosquitto/config/passwd && mosquitto_passwd -b -c /mosquitto/config/passwd '$MQTT_USER' '$MQTT_PASSWORD' \
         && chown mosquitto:mosquitto /mosquitto/config/passwd && chmod 600 /mosquitto/config/passwd"

# profil tambahan: influxdb, tubes (boleh keduanya: ./pasang.sh influxdb tubes)
PROFIL=""
for p in "$@"; do PROFIL="$PROFIL --profile $p"; done
docker compose $PROFIL up -d
docker compose restart mosquitto
docker compose $PROFIL ps
