#include <ESPDomotic.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>

void initSensor();
void readSensors();
void readSensor(DHT_Unified *sensor, Channel *channel);
bool publishValue(const char* name, float value, Channel *channel);
void mqttConnectionCallback();
void receiveMqttMessage(char* topic, uint8_t* payload, unsigned int length);

#define DHTTYPE DHT22 

#ifdef ESP01
// usable pins are GPIO2 and GPIO as output (both must be pulled up to boot normal )
// If using SERIAL_TX_ONLY over GPIO1, GPIO3 becomes available
const uint8_t SENSOR_PIN  = 3;
const uint8_t RELAY_PIN   = 2;
const uint8_t TX_PIN      = 1;
#elif NODEMCUV2
// usable pins D0,D1,D2,D5,D6,D7 (D10 is TX (GPIO1), D9 is RX (GPIO3), D3 is GPIO0, D4 is GPIO2, D8 is GPIO15)
const uint8_t SENSOR_PIN_1  = D5;
const uint8_t SENSOR_PIN_2  = D6;
const uint8_t SENSOR_PIN_3  = D7;
const uint8_t RELAY_PIN   = D1;
const uint8_t LED_PIN     = D0;
#endif

Channel _sensor1Channel ("dht22_1", "dht22_1", SENSOR_PIN_1, INPUT, LOW); 
Channel _sensor2Channel ("dht22_2", "dht22_2", SENSOR_PIN_1, INPUT, LOW); 
Channel _sensor3Channel ("dht22_3", "dht22_3", SENSOR_PIN_1, INPUT, LOW); 

unsigned long lastInputTs       = 0;
unsigned int  inputChainLength  = 0;

template <class T> void log (T text) {
  #ifdef LOGGING
  Serial.print(F("*SS: "));
  Serial.println(text);
  #endif
}

template <class T, class U> void log (T key, U value) {
  #ifdef LOGGING
  Serial.print(F("*SS: "));
  Serial.print(key);
  Serial.print(F(": "));
  Serial.println(value);
  #endif
}

ESPDomotic  _domoticModule;
uint8_t     _switchState = LOW;

DHT_Unified dht1(SENSOR_PIN_1, DHTTYPE);
DHT_Unified dht2(SENSOR_PIN_2, DHTTYPE);
DHT_Unified dht3(SENSOR_PIN_3, DHTTYPE);

uint32_t delayMS;

void setup() {
#ifdef ESP01
  //to avoid using pin 0 as input
  Serial.begin(115200, SERIAL_8N1, SERIAL_TX_ONLY, TX_PIN);
#else
  Serial.begin(115200);
#endif
  delay(500);
  Serial.println();
  //pinMode(SENSOR_PIN, INPUT_PULLUP);
  log(F("Starting module"));
  String ssid = "Sensor " + String(ESP.getChipId());
  _domoticModule.setPortalSSID(ssid.c_str());
  #ifndef ESP01
  _domoticModule.setFeedbackPin(LED_PIN); // PIN 0 can be used as output
  #endif
  _domoticModule.setMqttConnectionCallback(mqttConnectionCallback);
  _domoticModule.setMqttMessageCallback(receiveMqttMessage);
  _domoticModule.setConfigPortalTimeout(CONFIG_PORTAL_TIMEOUT);
  _domoticModule.setWifiConnectTimeout(WIFI_CONNECT_TIMEOUT);
  _domoticModule.setConfigFileSize(CONFIG_FILE_SIZE);
  _domoticModule.setModuleType("sensor");
  _domoticModule.addChannel(&_sensor1Channel);
  _domoticModule.addChannel(&_sensor2Channel);
  _domoticModule.addChannel(&_sensor3Channel);
  _domoticModule.init();

  initSensor();
}

void initSensor() {
    dht1.begin();
    dht2.begin();
    dht3.begin();
    sensor_t sensor;
    dht1.temperature().getSensor(&sensor);
    Serial.println(F("------------------------------------"));
    Serial.println(F("Temperature Sensor"));
    Serial.print  (F("Sensor Type: ")); Serial.println(sensor.name);
    Serial.print  (F("Driver Ver:  ")); Serial.println(sensor.version);
    Serial.print  (F("Unique ID:   ")); Serial.println(sensor.sensor_id);
    Serial.print  (F("Max Value:   ")); Serial.print(sensor.max_value); Serial.println(F("°C"));
    Serial.print  (F("Min Value:   ")); Serial.print(sensor.min_value); Serial.println(F("°C"));
    Serial.print  (F("Resolution:  ")); Serial.print(sensor.resolution); Serial.println(F("°C"));
    Serial.println(F("------------------------------------"));
    // Print humidity sensor details.
    dht1.humidity().getSensor(&sensor);
    Serial.println(F("Humidity Sensor"));
    Serial.print  (F("Sensor Type: ")); Serial.println(sensor.name);
    Serial.print  (F("Driver Ver:  ")); Serial.println(sensor.version);
    Serial.print  (F("Unique ID:   ")); Serial.println(sensor.sensor_id);
    Serial.print  (F("Max Value:   ")); Serial.print(sensor.max_value); Serial.println(F("%"));
    Serial.print  (F("Min Value:   ")); Serial.print(sensor.min_value); Serial.println(F("%"));
    Serial.print  (F("Resolution:  ")); Serial.print(sensor.resolution); Serial.println(F("%"));
    Serial.println(F("------------------------------------"));
    // Set delay between sensor readings based on sensor details.
    delayMS = max(sensor.min_delay / 1000, SENSE_THRESHOLD);
}

void loop() {
  _domoticModule.loop();
  readSensors();
}

void readSensors() {
  // Delay between measurements.
  delay(delayMS);
  // Get temperature event and print its value.
  readSensor(&dht1, &_sensor1Channel);
  readSensor(&dht2, &_sensor2Channel);
  readSensor(&dht3, &_sensor3Channel);
}

void readSensor(DHT_Unified *sensor, Channel *channel) {
  sensors_event_t event;
  sensor->temperature().getEvent(&event);
  publishValue("temperature", event.temperature, channel);
  sensor->humidity().getEvent(&event);
  publishValue("humidity", event.relative_humidity, channel);
}

bool publishValue(const char* name, float value, Channel *channel) {
    if (!isnan(value)) {
        _domoticModule.getMqttClient()->publish(
            _domoticModule.getChannelTopic(channel, name).c_str(), 
            String(value).c_str());
        return true;
    }
    return false;
}

void mqttConnectionCallback() {
  // no additional subsription is needed
}

void receiveMqttMessage(char* topic, uint8_t* payload, unsigned int length) {
  // no additional message to process
}