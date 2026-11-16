#include "piezo_app.hpp"

#include "config.hpp"

#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <cstring>
#include <stdint.h>

#define PIEZO_PIN 34
#define LED_PIN 22
#define PIEZO2_PIN 35
#define LED2_PIN 23

const int THRESHOLD = 500;
const unsigned long COOLDOWN = 150;
const unsigned long PEAK_WINDOW = 30;
const unsigned long LED_DURATION = 200;
const uint16_t HTTP_TIMEOUT_MS = 2500;
const unsigned long RETRY_DELAY_MS = 3000;
const uint32_t POST_MIN_INTERVAL_MS = 150;

#define SENSOR_COUNT 2

struct Sensor
{
  uint8_t pin;
  uint8_t ledPin;
  uint8_t sensorId;
  bool detecting;
  bool ledActive;
  int peakValue;
  unsigned long lastHit;
  unsigned long hitStart;
  unsigned long ledStart;
  volatile bool pending;
  volatile int pendingPeak;
};

Sensor sensors[SENSOR_COUNT] = {
    {PIEZO_PIN, LED_PIN, 1, false, false, 0, 0, 0, 0, false, 0},
    {PIEZO2_PIN, LED2_PIN, 2, false, false, 0, 0, 0, 0, false, 0},
};

SemaphoreHandle_t queueLock;

bool kirimKeServer(uint8_t sensorId, int peak)
{
  Config config = loadConfig();
  if (WiFi.status() != WL_CONNECTED)
    return false;

  char payload[64];
  snprintf(payload, sizeof(payload),
           "{\"sensor_id\":%u,\"frequency\":%d}", (unsigned)sensorId, peak);

  char url[128];
  snprintf(url, sizeof(url), "http://%s:%u%s",
           config.server_host, (unsigned)config.server_port, config.server_path);

  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setReuse(false);

  if (!http.begin(client, url))
    return false;
  http.addHeader("Content-Type", "application/json");

  unsigned long t0 = millis();
  int code = http.POST((uint8_t *)payload, strlen(payload));
  unsigned long lama = millis() - t0;

  bool ok = (code >= 200 && code < 300);

  if (code > 0)
  {
    Serial.print(ok ? "POST OK " : "POST ditolak ");
    Serial.print(code);
    Serial.print(" (");
    Serial.print(lama);
    Serial.print(" ms) ");
    Serial.println(http.getString());
  }
  else
  {
    Serial.print("POST gagal (");
    Serial.print(lama);
    Serial.print(" ms): ");
    Serial.println(http.errorToString(code));
  }

  http.end();
  return ok;
}

void taskKirim(void *param)
{
  (void)param;
  unsigned long lastPost = 0;

  for (;;)
  {
    if (WiFi.status() != WL_CONNECTED)
    {
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    for (uint8_t i = 0; i < SENSOR_COUNT; i++)
    {
      bool ada = false;
      int data = 0;
      uint8_t id = 0;

      xSemaphoreTake(queueLock, portMAX_DELAY);
      if (sensors[i].pending)
      {
        ada = true;
        data = sensors[i].pendingPeak;
        id = sensors[i].sensorId;
      }
      xSemaphoreGive(queueLock);

      if (!ada)
        continue;

      unsigned long now = millis();
      unsigned long jeda = now - lastPost;
      if (jeda < POST_MIN_INTERVAL_MS)
      {
        vTaskDelay(pdMS_TO_TICKS(POST_MIN_INTERVAL_MS - jeda));
      }

      lastPost = millis();
      if (kirimKeServer(id, data))
      {
        xSemaphoreTake(queueLock, portMAX_DELAY);
        sensors[i].pending = false;
        xSemaphoreGive(queueLock);
      }
      else
      {
        vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS)); // gagal -> tunggu sebelum ulang
      }
      break;
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void piezoSetup()
{
  Config config = loadConfig();
  Serial.begin(115200);
  delay(400);

  for (uint8_t i = 0; i < SENSOR_COUNT; i++)
  {
    pinMode(sensors[i].ledPin, OUTPUT);
    digitalWrite(sensors[i].ledPin, LOW);
  }

  analogReadResolution(12);
  analogSetPinAttenuation(PIEZO_PIN, ADC_11db);
  analogSetPinAttenuation(PIEZO2_PIN, ADC_11db);

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);

  Serial.print("Menyambung WiFi");
  WiFi.begin(config.wifi_ssid, config.wifi_password);
  unsigned long mulai = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - mulai < 15000)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.print("WiFi OK, IP ESP32: ");
    Serial.println(WiFi.localIP());
  }
  else
  {
    Serial.println("WiFi belum tersambung, task akan coba lagi otomatis.");
  }

  queueLock = xSemaphoreCreateMutex();

  xTaskCreatePinnedToCore(taskKirim, "taskKirim", 8192, NULL, 1, NULL, 0);

  Serial.println("Piezoelectric Sensor Ready");
}

void piezoLoop()
{
  Config config = loadConfig();
  unsigned long now = millis();

  static unsigned long lastWifiCheck = 0;
  if (WiFi.status() != WL_CONNECTED && now - lastWifiCheck > 10000)
  {
    lastWifiCheck = now;
    WiFi.disconnect();
    WiFi.begin(config.wifi_ssid, config.wifi_password);
  }

  for (uint8_t i = 0; i < SENSOR_COUNT; i++)
  {
    Sensor &s = sensors[i];
    int nilai = analogRead(s.pin);

    if (!s.detecting && nilai >= THRESHOLD && now - s.lastHit >= COOLDOWN)
    {
      s.detecting = true;
      s.hitStart = now;
      s.lastHit = now;
      s.peakValue = nilai;

      digitalWrite(s.ledPin, HIGH);
      s.ledStart = now;
      s.ledActive = true;
    }

    if (s.detecting)
    {
      if (nilai > s.peakValue)
        s.peakValue = nilai;

      if (now - s.hitStart >= PEAK_WINDOW)
      {
        Serial.print("Getaran Terdeteksi (sensor ");
        Serial.print(s.sensorId);
        Serial.print(")! Peak: ");
        Serial.println(s.peakValue);

        xSemaphoreTake(queueLock, portMAX_DELAY);
        s.pending = true;
        s.pendingPeak = s.peakValue;
        xSemaphoreGive(queueLock);

        s.detecting = false;
      }
    }

    if (s.ledActive && now - s.ledStart >= LED_DURATION)
    {
      digitalWrite(s.ledPin, LOW);
      s.ledActive = false;
    }
  }

  delay(1);
}
