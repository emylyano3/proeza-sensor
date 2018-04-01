#include <FS.h>                   //this needs to be first, or it all crashes and burns...

#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino

#include <ESP8266WebServer.h>

// Alternativa a WifiManager
// https://github.com/chriscook8/esp-arduino-apboot/blob/master/ESP-wifiboot.ino
#include <WiFiManager.h>          

#include <WiFiClient.h>
#include <ESP8266HTTPUpdateServer.h>

#include <PubSubClient.h>

#ifndef ESP01
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include <ESP8266mDNS.h>
#endif

const char* CONFIG_FILE   = "/config.json";

/* Possible switch states */
const char STATE_OFF      = '0';
const char STATE_ON       = '1';

#ifndef ESP01
const String MODULE_TYPE  = "senseStation";
const String CHANNEL_TYPE = "sensor";
#else
const String CHANNEL_TYPE = "sensor";
const String MODULE_TYPE  = CHANNEL_TYPE;
#endif

struct Channel {
  WiFiManagerParameter *param;
  uint8_t sensorPin;
  int sensorState;
};

WiFiClient espClient;
PubSubClient mqttClient(espClient);
ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;

WiFiManagerParameter mqttServer("mqttServer", "MQTT Server", "192.168.0.105", 16);
WiFiManagerParameter mqttPort("mqttPort", "MQTT Port", "1883", 6);
WiFiManagerParameter moduleLocation("moduleLocation", "Module location", "cabin", PARAM_LENGTH);
WiFiManagerParameter moduleName("moduleName", "Module name", "gas", PARAM_LENGTH);
#ifdef ESP01
WiFiManagerParameter ch_A_name("ch_A_name", "Channel A name", "ch_A", PARAM_LENGTH);
WiFiManagerParameter ch_B_name("ch_B_name", "Channel B name", "ch_B", PARAM_LENGTH);
WiFiManagerParameter ch_C_name("ch_C_name", "Channel C name", "ch_C", PARAM_LENGTH);
#else
WiFiManagerParameter ch_A_name("ch_A_name", "Channel A name", "ch_A", PARAM_LENGTH);
WiFiManagerParameter ch_B_name("ch_B_name", "Channel B name", "ch_B", PARAM_LENGTH);
WiFiManagerParameter ch_C_name("ch_C_name", "Channel C name", "ch_C", PARAM_LENGTH);
#endif

#ifdef ESP01
Channel channels[] = {
  {&ch_A_name, 2, LOW},
  {&ch_B_name, 3, LOW},
  {&ch_C_name, 0, LOW}
};
const uint8_t CHANNELS_COUNT  = 3;
const uint8_t TX_PIN          = 1;
#elif NODEMCUV2
Channel channels[] = {
  {&ch_A_name, D7, LOW},
  {&ch_B_name, D6, LOW},
  {&ch_C_name, D0, LOW}
};
const uint8_t CHANNELS_COUNT = 3;
#else
Channel channels[] = {
  {&ch_A_name, 13, LOW},
  {&ch_B_name, 12, LOW},
  {&ch_C_name, 16, LOW}
};
const uint8_t CHANNELS_COUNT = 3;
#endif

long nextBrokerConnAtte = 0;

template <class T> void log (T text) {
  if (LOGGING) {
    Serial.print("*SW: ");
    Serial.println(text);
  }
}

template <class T, class U> void log (T key, U value) {
  if (LOGGING) {
    Serial.print("*SW: ");
    Serial.print(key);
    Serial.print(": ");
    Serial.println(value);
  }
}

void setup() {
#ifdef ESP01
  //to avoid using pin 0 as input
  Serial.begin(115200, SERIAL_8N1, SERIAL_TX_ONLY, TX_PIN);
#else
  Serial.begin(115200);
#endif
  delay(500);
  Serial.println();
  log("Starting module");
  bool existConfig = loadConfig();
    
  // pins settings
  for (size_t i = 0; i < CHANNELS_COUNT; ++i) {
    pinMode(channels[i].sensorPin, INPUT);
  }
  
  // WiFi Manager Config  
  WiFiManager wifiManager;
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.setStationNameCallback(getStationName);
  wifiManager.setMinimumSignalQuality(WIFI_MIN_SIGNAL);
  if (existConfig) {
    wifiManager.setConnectTimeout(WIFI_CONN_TIMEOUT);
  } else {
    // If no previous config, no reason to try to connect to saved network. Wifi.diconnect() erases saved credentials
    WiFi.disconnect();
  }
  wifiManager.addParameter(&mqttServer);
  wifiManager.addParameter(&mqttPort);
  wifiManager.addParameter(&moduleLocation);
  wifiManager.addParameter(&moduleName);
  for (uint8_t i = 0; i < CHANNELS_COUNT; ++i) {
    wifiManager.addParameter(channels[i].param);
  }
  if (!wifiManager.autoConnect(("ESP_" + String(ESP.getChipId())).c_str(), "12345678")) {
    log(F("Failed to connect and hit timeout"));
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }
  log(F("Connected to wifi network. Local IP"), WiFi.localIP());
  log(F("Configuring MQTT broker"));
  String port = String(mqttPort.getValue());
  log(F("Port"), port);
  log(F("Server"), mqttServer.getValue());
  mqttClient.setServer(mqttServer.getValue(), (uint16_t) port.toInt());
  mqttClient.setCallback(receiveMqttMessage);

  // OTA Update Stuff
  WiFi.mode(WIFI_STA);
#ifndef ESP01
  MDNS.begin(getStationName());
  MDNS.addService("http", "tcp", 80);
#endif
  httpUpdater.setup(&httpServer);
  httpServer.begin();
  Serial.print(F("HTTPUpdateServer ready! Open http://"));
  Serial.print(WiFi.localIP().toString());
  Serial.println(F("/update in your browser"));
}

void loop() {
  httpServer.handleClient();
  processPhysicalInput();
  if (!mqttClient.connected()) {
    connectBroker();
  }
  delay(READ_INTERVAL);
  mqttClient.loop();
}

bool loadConfig() { 
  //read configuration from FS json
  if (SPIFFS.begin()) {
    if (SPIFFS.exists(CONFIG_FILE)) {
      //file exists, reading and loading
      File configFile = SPIFFS.open(CONFIG_FILE, "r");
      if (configFile) {
        size_t size = configFile.size();
        if (size > 0) {
        #ifndef ESP01
          // Allocate a buffer to store contents of the file.
          char buf[size];
          configFile.readBytes(buf, size);
          DynamicJsonBuffer jsonBuffer;
          JsonObject& json = jsonBuffer.parseObject(buf);
          json.printTo(Serial);
          if (json.success()) {
            mqttServer.update(json[mqttServer.getID()]);
            mqttPort.update(json[mqttPort.getID()]);
            moduleName.update(json[moduleName.getID()]);
            moduleLocation.update(json[moduleLocation.getID()]);
            for (uint8_t i = 0; i < CHANNELS_COUNT; ++i) {
              channels[i].param->update(json[channels[i].param->getID()]);
            }
            return true;
          } else {
            log(F("Failed to load json config"));
          }
        #else
          // Avoid using json to reduce build size
          while (configFile.position() < size) {
            String line = configFile.readStringUntil('\n');
            line.trim();
            uint16_t ioc = line.indexOf('=');
            if (ioc >= 0 && ioc + 1 < line.length()) {
              String key = line.substring(0, ioc++);
              log("Read key", key);
              String val = line.substring(ioc, line.length());
              log("Key value", val);
              if (key.equals(mqttPort.getID())) {
                mqttPort.update(val.c_str());
              } else if (key.equals(mqttServer.getID())) {
                mqttServer.update(val.c_str());
              } else if (key.equals(moduleLocation.getID())) {
                moduleLocation.update(val.c_str());
              } else if (key.equals(moduleName.getID())) {
                moduleName.update(val.c_str());
              } else {
                for (uint8_t i = 0; i < CHANNELS_COUNT; ++i) {
                  if (key.equals(channels[i].param->getID())) {
                    channels[i].param->update(val.c_str());
                  }
                }
              }
            } else {
              log("Config bad format", line);
            }
          }
          return true;
        #endif
        } else {
          log(F("Config file empty"));
        }
      } else {
        log(F("No config file found"));
      }
      configFile.close();
    } else {
      log(F("No config file found"));
    }
  } else {
    log(F("Failed to mount FS"));
  }
  return false;
}

/** callback notifying the need to save config */
void saveConfigCallback () {
  File configFile = SPIFFS.open(CONFIG_FILE, "w");
  if (configFile) {
  #ifndef ESP01
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    //TODO Trim param values
    json[mqttServer.getID()] = mqttServer.getValue();
    json[mqttPort.getID()] = mqttPort.getValue();
    json[moduleName.getID()] = moduleName.getValue();
    json[moduleLocation.getID()] = moduleLocation.getValue();
    for (uint8_t i = 0; i < CHANNELS_COUNT; ++i) {
      json[channels[i].param->getID()] = channels[i].param->getValue();
    }
    json.printTo(configFile);
  #else
    String line = String(mqttServer.getID()) + "=" + String(mqttServer.getValue());
    configFile.println(line);
    line = String(mqttPort.getID()) + "=" + String(mqttPort.getValue());
    configFile.println(line);
    line = String(moduleName.getID()) + "=" + String(moduleName.getValue());
    configFile.println(line);
    line = String(moduleLocation.getID()) + "=" + String(moduleLocation.getValue());
    configFile.println(line);
    for (uint8_t i = 0; i < CHANNELS_COUNT; ++i) {
      line = String(channels[i].param->getID()) + "=" + String(channels[i].param->getValue());
      configFile.println(line);
    }
  #endif
  } else {
    log(F("Failed to open config file for writing"));
  }
  configFile.close();
}

void receiveMqttMessage(char* topic, unsigned char* payload, unsigned int length) {
  log(F("Received mqtt message topic"), topic);
  if (String(topic).equals(getStationTopic("hrst"))) {
    hardReset();
  } else {
    log(F("Unknown topic"));
  }
}

void hardReset () {
  log(F("Doing a module hard reset"));
  SPIFFS.format();
  WiFiManager wifiManager;
  wifiManager.resetSettings();
  delay(200);
  ESP.restart();
}

void publishState (Channel *c) {
  mqttClient.publish(getChannelTopic(c, "state").c_str(), new char[2]{c->sensorState, '\0'});
}

void processPhysicalInput() {
  for (size_t i = 0; i < CHANNELS_COUNT; ++i) {
    if (isChannelEnabled(&channels[i])) {
      processChannelInput(&channels[i]);
    }
  }
}

void processChannelInput(Channel *c) {
  int read = digitalRead(c->sensorPin);
  log("Read from sensor [" + String(c->param->getValue()) + "]", read);
  if (read != c->sensorState) {
    log(F("Phisical switch state has changed. Updating channel"), c->param->getValue());
    c->sensorState = read;
    publishState(c);
  }
}

bool isChannelEnabled (Channel *c) {
  return c->param->getValueLength() > 0;
}

char* getStationName () {
  int size = MODULE_TYPE.length() + moduleLocation.getValueLength() + moduleName.getValueLength() + 4;
  String type(MODULE_TYPE);
  String location(moduleLocation.getValue()); 
  String name(moduleName.getValue());
  char sn[size+1];
  (type + "_" + location + "_" + name).toCharArray(sn, size+1);
  return sn;
}

void connectBroker() {
  if (nextBrokerConnAtte <= millis()) {
    nextBrokerConnAtte = millis() + MQTT_BROKER_CONNECTION_RETRY;
    log(F("Connecting MQTT broker as"), getStationName());
    if (mqttClient.connect(getStationName())) {
      log(F("MQTT broker connected"));
      subscribeTopic(getStationTopic("#").c_str());
    }
  } else {
    log(F("Failed. RC:"), mqttClient.state());
  }
}

void subscribeTopic(const char *t) {
  log("Subscribing mqtt topic", t);
  mqttClient.subscribe(t);
}

String getChannelTopic (Channel *c, String cmd) {
  return CHANNEL_TYPE + F("/") + moduleLocation.getValue() + F("/") + c->param->getValue() + F("/") + cmd;
}

String getStationTopic (String cmd) {
  return MODULE_TYPE + F("/") + moduleLocation.getValue() + F("/") + moduleName.getValue() + F("/") + cmd;
}
