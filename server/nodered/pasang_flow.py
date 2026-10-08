#!/usr/bin/env python3
"""pasang_flow.py - memasang flow pencatat Tugas Besar ke Node-RED server (jalankan DI SERVER).
Akun MQTT dibaca dari ../.env dan token InfluxDB dari ../token_praktikan.txt, lalu flow dikirim
ke Admin API Node-RED di http://127.0.0.1:1880 (hanya bisa diakses dari server itu sendiri)."""
import json, os, urllib.request
D = os.path.dirname(os.path.abspath(__file__))
env = dict(b.strip().split('=', 1) for b in open(os.path.join(D, '..', '.env')) if '=' in b and not b.startswith('#'))
flow = json.load(open(os.path.join(D, 'flow_pencatat.json'), encoding='utf-8'))
for n in flow:
    if n['type'] == 'mqtt-broker':
        n['credentials'] = {'user': env['MQTT_USER'].strip('"'), 'password': env['MQTT_PASSWORD'].strip('"')}
    if n['type'] == 'influxdb':
        n['credentials'] = {'token': open(os.path.join(D, '..', 'token_praktikan.txt')).read().strip()}
r = urllib.request.Request('http://127.0.0.1:1880/flows', data=json.dumps(flow).encode(), method='POST',
                           headers={'Content-Type': 'application/json', 'Node-RED-Deployment-Type': 'full'})
print('flow terpasang, status', urllib.request.urlopen(r, timeout=30).status)
