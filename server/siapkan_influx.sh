#!/usr/bin/env bash
# siapkan_influx.sh - dijalankan SEKALI di server setelah InfluxDB menyala (./pasang.sh influxdb).
# Membuat bucket draft, akun web bersama untuk praktikan, dan token baca-tulis untuk kedua bucket.
# Token praktikan disimpan ke token_praktikan.txt (inilah yang ditulis di modul).
set -euo pipefail
cd "$(dirname "$0")"
set -a; . ./.env; set +a
ix() { docker compose exec -T influxdb influx "$@" --org "$INFLUX_ORG" --token "$INFLUX_TOKEN"; }

# tunggu InfluxDB siap (paling lama 2 menit)
for i in $(seq 1 60); do docker compose exec -T influxdb influx ping >/dev/null 2>&1 && break; sleep 2; done
docker compose exec -T influxdb influx ping >/dev/null || { echo "InfluxDB belum menyala: jalankan ./pasang.sh influxdb"; exit 1; }

ix bucket list --name "$INFLUX_BUCKET_DRAFT" >/dev/null 2>&1 \
  || ix bucket create --name "$INFLUX_BUCKET_DRAFT" --retention "$INFLUX_RETENTION_DRAFT"
id_utama=$(ix bucket list --name "$INFLUX_BUCKET" --hide-headers | awk '{print $1}')
id_draft=$(ix bucket list --name "$INFLUX_BUCKET_DRAFT" --hide-headers | awk '{print $1}')

# akun web bersama (anggota organisasi, bukan pemilik)
docker compose exec -T influxdb influx user list --token "$INFLUX_TOKEN" --hide-headers | grep -qw "$INFLUX_USER_PRAKTIKAN" \
  || ix user create --name "$INFLUX_USER_PRAKTIKAN" --password "$INFLUX_PASSWORD_PRAKTIKAN"

id_user=$(docker compose exec -T influxdb influx user list --token "$INFLUX_TOKEN" --hide-headers | awk -v u="$INFLUX_USER_PRAKTIKAN" '$2==u{print $1}')
docker compose exec -T influxdb influx org members list --name "$INFLUX_ORG" --token "$INFLUX_TOKEN" --hide-headers | grep -qw "$id_user"   || docker compose exec -T influxdb influx org members add --name "$INFLUX_ORG" --member "$id_user" --token "$INFLUX_TOKEN"

# token baca-tulis hanya untuk kedua bucket
if [ ! -s token_praktikan.txt ]; then
  ix auth create --description "Praktikan BE4001: baca-tulis bucket $INFLUX_BUCKET dan $INFLUX_BUCKET_DRAFT" \
     --read-bucket "$id_utama" --write-bucket "$id_utama" --read-bucket "$id_draft" --write-bucket "$id_draft" \
     --hide-headers | awk '{for(i=1;i<=NF;i++) if(length($i)>60) print $i}' > token_praktikan.txt
fi
echo "bucket  : $INFLUX_BUCKET ($id_utama), $INFLUX_BUCKET_DRAFT ($id_draft)"
echo "token   : $(cat token_praktikan.txt)"
