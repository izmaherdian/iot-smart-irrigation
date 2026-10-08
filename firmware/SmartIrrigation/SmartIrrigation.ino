// =====================================================================
// Praktikum BE4001 - Tugas Besar 2026/2027
// Board : ESP32 Dev Module (ESP32 DevKit 30 pin)
// Pin   : sensor kelembaban tanah (AOUT) -> GPIO34     DHT11 (OUT) -> GPIO22
//         driver L298N IN1 -> GPIO32, IN2 -> GPIO21
//
// Yang WAJIB diubah tiap kelompok (bagian "UBAH DI SINI"):
//   1. ssid dan password Wi-Fi
//   2. nomor kelompok pada delapan topik MQTT (ganti 00 dengan 01 sampai 07)
//   3. nilai kalibrasi sensor tanah SM_KERING dan SM_BASAH (hasil Modul 1)
// =====================================================================
#include "DHT.h"
#include <stdio.h>
#include <WiFi.h>
#include <PubSubClient.h>

// ============================ UBAH DI SINI ============================
// 1. Wi-Fi
const char* ssid = "...";
const char* password = "...";

// 2. topik MQTT: ganti 00 dengan nomor kelompok (01 sampai 07)
const char* mqtt_inTopic       = "BE4001/00/CMD/#";      // semua perintah untuk alat
const char* mqtt_inTopic_PUMP  = "BE4001/00/CMD/PUMP";   // '1' / '0' : pompa nyala / mati (hanya mode manual)
const char* mqtt_inTopic_MODE  = "BE4001/00/CMD/MODE";   // '1' / '0' : mode otomatis / manual
const char* mqtt_inTopic_PING  = "BE4001/00/CMD/PING";   // alat membalas PONG
const char* mqtt_outTopic_DATA = "BE4001/00/DATA";       // "SM,AM,AT" tiap 1 menit
const char* mqtt_outTopic_PUMP = "BE4001/00/PUMP";       // "mode,pompa" tiap kali berubah
const char* mqtt_outTopic_DHT  = "BE4001/00/DHT";        // status sensor DHT11
const char* mqtt_outTopic_AJAX = "BE4001/00/AJAX";       // pesan lain-lain

// 3. kalibrasi sensor kelembaban tanah (pakai hasil ukur sendiri)
#define SM_KERING 4095   // bacaan saat sensor kering di udara  (0 %)
#define SM_BASAH  1800   // bacaan saat sensor terendam air     (100 %)
// ======================================================================

// server MQTT (isi sesuai server/.env)
const char* mqtt_server = "YOUR_SERVER_IP";
const int mqtt_port = 1884;
const char* mqttUser = "YOUR_MQTT_USER";
const char* mqttPassword = "YOUR_MQTT_PASSWORD";

// alokasi pin
#define SM_pin 34
#define DHT_pin 22
#define pump_pin_in1 32
#define pump_pin_in2 21

// pengaturan otomasi
const unsigned long MeasurePeriod = 1000;    // baca sensor dan cek penyiraman tiap 1 detik
const unsigned long PublishPeriod = 60000;   // kirim data tiap 1 menit
const int BatasSiram = 50;                   // pompa menyala jika kelembaban tanah di bawah nilai ini (%)

DHT dht(DHT_pin, DHT11);
WiFiClient espClient;
PubSubClient client(espClient);
void callback(char* topic, byte* payload, unsigned int length);
void reconnect();

bool set_mode = HIGH, set_pump = LOW;   // kondisi awal: mode otomatis, pompa mati

float RelativeHumidity = 0.0, Temperature = 0.0, SoilMoisture = 0.0;
bool ModeAuto = !set_mode, Pump = set_pump;
bool LastModeAuto = ModeAuto, LastPump = Pump;
int DHTEvent = 4;

unsigned long Now = 0;
unsigned long LastMeasure = 0;
unsigned long LastPublish = 0;

char DataMeasurement[40];
char StatusDHT[30];
char StatusControl[10];

void setup() {
  Serial.begin(9600);
  dht.begin();
  delay(1500);

  pinMode(SM_pin, INPUT);
  pinMode(pump_pin_in1, OUTPUT);
  pinMode(pump_pin_in2, OUTPUT);
  digitalWrite(pump_pin_in1, LOW);
  digitalWrite(pump_pin_in2, LOW);

  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {   // tunggu sampai tersambung
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  reconnect();

  client.publish(mqtt_outTopic_AJAX, "SETUP FINISHED");
  Serial.println("kelembaban tanah (%),kelembaban udara (%),temperatur udara (C)");
}

void loop() {
  wifi_loop();
  Now = millis();

  // kirim data sensor tiap PublishPeriod
  if (Now - LastPublish > PublishPeriod) {
    LastPublish = Now;
    publish_measurement();
    Serial.println("Publish!!");
  }

  // baca sensor tiap MeasurePeriod
  if (Now - LastMeasure > MeasurePeriod) {
    LastMeasure = Now;
    SoilMoisture = calibratedSM();
    readDHT();
    sprintf(DataMeasurement, "%.2f,%.2f,%.2f", SoilMoisture, RelativeHumidity, Temperature);
    Serial.println(DataMeasurement);
  }

  // kendali pompa: otomatis dari kelembaban tanah, atau manual dari perintah MQTT
  ModeAuto = set_mode;
  if (ModeAuto) {
    Pump = pumpAUTO(SoilMoisture, BatasSiram);
  } else {
    Pump = pumpManual(set_pump);
  }

  // laporkan setiap perubahan mode atau kondisi pompa
  if ((ModeAuto != LastModeAuto) || (Pump != LastPump)) {
    publish_state(ModeAuto, Pump);
    Serial.print("Change Mode/Pump State : ");
    Serial.print(ModeAuto);
    Serial.println(Pump);
    LastModeAuto = ModeAuto;
    LastPump = Pump;
  }
}

// dijalankan setiap kali alat menerima pesan MQTT dari topik yang di-subscribe
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (unsigned int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  if (strcmp(topic, mqtt_inTopic_PUMP) == 0) {
    if (set_mode) {
      Serial.println("Mode is AUTO, Pompa cannot be set");
      client.publish(mqtt_outTopic_AJAX, "Mode is AUTO, Pompa cannot be set");
      return;
    }
    if (payload[0] == '1') {
      set_pump = HIGH;
      Serial.println("Pompa set to ON");
      client.publish(mqtt_outTopic_AJAX, "Pompa set to ON");
    } else if (payload[0] == '0') {
      set_pump = LOW;
      Serial.println("Pompa set to OFF");
      client.publish(mqtt_outTopic_AJAX, "Pompa set to OFF");
    }
  }

  else if (strcmp(topic, mqtt_inTopic_MODE) == 0) {
    if (payload[0] == '1') {
      set_mode = HIGH;
      Serial.println("Mode set to Auto");
      client.publish(mqtt_outTopic_AJAX, "Mode set to AUTO");
    } else if (payload[0] == '0') {
      set_mode = LOW;
      set_pump = LOW;
      Serial.println("Mode set to Manual");
      client.publish(mqtt_outTopic_AJAX, "Mode set to MANUAL");
    }
  }

  else if (strcmp(topic, mqtt_inTopic_PING) == 0) {
    publish_state(ModeAuto, Pump);
    Serial.println("PONG");
    client.publish(mqtt_outTopic_AJAX, "PONG");
  }
}

void wifi_loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
}

// menyambung (ulang) ke broker MQTT
void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // client ID acak supaya tidak bentrok dengan perangkat lain
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);
    if (client.connect(clientId.c_str(), mqttUser, mqttPassword)) {
      Serial.println("connected");
      client.publish(mqtt_outTopic_AJAX, "Connected");
      client.subscribe(mqtt_inTopic);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

// kirim "SM,AM,AT" ke topik DATA (dicatat server ke database)
void publish_measurement() {
  sprintf(DataMeasurement, "%.2f,%.2f,%.2f", SoilMoisture, RelativeHumidity, Temperature);
  client.publish(mqtt_outTopic_DATA, DataMeasurement);
}

// kirim "mode,pompa" ke topik PUMP (1,1 = otomatis dan pompa nyala)
void publish_state(bool ModeKontrol, bool Pompa) {
  sprintf(StatusControl, "%d,%d", ModeKontrol, Pompa);
  client.publish(mqtt_outTopic_PUMP, StatusControl);
}

// bacaan sensor tanah dalam persen: 0 = kering, 100 = terendam
float calibratedSM() {
  float sm = (float)(analogRead(SM_pin) - SM_KERING) * 100 / (SM_BASAH - SM_KERING);
  return constrain(sm, 0, 100);
}

// baca DHT11; nilai lama dipertahankan jika gagal, dan perubahan status dilaporkan
void readDHT() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  int Event;
  if (isnan(h) || isnan(t)) {
    sprintf(StatusDHT, "Error!");
    Event = 0;
  } else {
    RelativeHumidity = h;
    Temperature = t;
    sprintf(StatusDHT, "Working properly!");
    Event = 3;
  }
  if (DHTEvent != Event) {
    DHTEvent = Event;
    client.publish(mqtt_outTopic_DHT, StatusDHT);
  }
}

bool pumpON() {
  digitalWrite(pump_pin_in1, HIGH);
  return HIGH;
}

bool pumpOFF() {
  digitalWrite(pump_pin_in1, LOW);
  return LOW;
}

bool pumpAUTO(float SM, int limit) {
  if (SM > limit) {
    return pumpOFF();
  } else {
    return pumpON();
  }
}

bool pumpManual(bool state) {
  if (state) {
    return pumpON();
  } else {
    return pumpOFF();
  }
}
