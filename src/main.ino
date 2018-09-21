#include <FS.h> //this needs to be first, or it all crashes and burns...
#include <ESP8266WiFi.h>     
#include <ESP8266WebServer.h>
#include <WiFiClient.h>
#include <ESP8266HTTPUpdateServer.h>
#include <PubSubClient.h>
#include <ESPConfig.h>

#ifndef ESP01
#include <ArduinoJson.h>
#include <ESP8266mDNS.h>
#endif

const char* CONFIG_FILE   = "/config.json";

/* Possible switch states */
const char STATE_OFF      = '0';
const char STATE_ON       = '1';

#ifndef ESP01
const String MODULE_TYPE = "sensor";
#else
const String MODULE_TYPE = "sensor";
#endif


char          _stationName[PARAM_LENGTH * 3 + 4];

struct Channel {
  ESPConfigParam *param;
  uint8_t sensorPin;
  int sensorState;
};

WiFiClient espClient;
PubSubClient mqttClient(espClient);
ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;

ESPConfigParam mqttServer(Text, "mqttServer", "MQTT Server", "192.168.0.105", PARAM_LENGTH, "required");
ESPConfigParam mqttPort(Text, "mqttPort", "MQTT Port", "1883", 6, "required");
ESPConfigParam moduleLocation(Text, "moduleLocation", "Module location", "cabin", PARAM_LENGTH, "required");
ESPConfigParam moduleName(Text, "moduleName", "Module name", "gas", PARAM_LENGTH, "required");
#ifdef ESP01
ESPConfigParam ch_A_name(Text, "ch_A_name", "Channel A name", "ch_A", PARAM_LENGTH, "required");
ESPConfigParam ch_B_name(Text, "ch_B_name", "Channel B name", "ch_B", PARAM_LENGTH, "required");
ESPConfigParam ch_C_name(Text, "ch_C_name", "Channel C name", "ch_C", PARAM_LENGTH, "required");
#else
ESPConfigParam ch_A_name(Text, "ch_A_name", "Channel A name", "ch_A", PARAM_LENGTH, "required");
ESPConfigParam ch_B_name(Text, "ch_B_name", "Channel B name", "ch_B", PARAM_LENGTH, "required");
ESPConfigParam ch_C_name(Text, "ch_C_name", "Channel C name", "ch_C", PARAM_LENGTH, "required");
#endif

#ifdef ESP01
Channel channels[] = {
  {&ch_A_name, 2, LOW},
  {&ch_B_name, 3, LOW},
  {&ch_C_name, 0, LOW}
};
const uint8_t CHANNELS_COUNT  = 3;
const uint8_t TX_PIN          = 1;
const uint8_t LED_PIN         = INVALID_PIN_NO;
#elif NODEMCUV2
Channel channels[] = {
  {&ch_A_name, D7, LOW},
  {&ch_B_name, D6, LOW},
  {&ch_C_name, D0, LOW}
};
const uint8_t LED_PIN         = D1;
const uint8_t CHANNELS_COUNT  = 3;
#else
Channel channels[] = {
  {&ch_A_name, 13, LOW},
  {&ch_B_name, 12, LOW},
  {&ch_C_name, 16, LOW}
};
const uint8_t LED_PIN         = 5;
const uint8_t CHANNELS_COUNT  = 3;
#endif

unsigned long nextBrokerConnAtte = 0;

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
  log(F("Starting module"));
  for (size_t i = 0; i < CHANNELS_COUNT; ++i) {
    pinMode(channels[i].sensorPin, INPUT);
  }
  ESPConfig moduleConfig;
  moduleConfig.setSaveConfigCallback(saveConfigCallback);
  moduleConfig.setStationNameCallback(getStationName);
  moduleConfig.setMinimumSignalQuality(WIFI_MIN_SIGNAL);
  moduleConfig.setConnectionTimeout(WIFI_CONN_TIMEOUT);
  moduleConfig.addParameter(&mqttServer);
  moduleConfig.addParameter(&mqttPort);
  moduleConfig.addParameter(&moduleLocation);
  moduleConfig.addParameter(&moduleName);
  moduleConfig.setPortalSSID("ESP-Irrigation");
  moduleConfig.setFeedbackPin(LED_PIN);
  moduleConfig.setAPStaticIP(IPAddress(10,10,10,10),IPAddress(IPAddress(10,10,10,10)),IPAddress(IPAddress(255,255,255,0)));
  for (uint8_t i = 0; i < CHANNELS_COUNT; ++i) {
    moduleConfig.addParameter(channels[i].param);
  }
  moduleConfig.connectWifiNetwork(loadConfig());
  moduleConfig.blockingFeedback(LED_PIN, 100, 8);
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
  
  httpServer.on("/config", showConfig);
}

void showConfig () {
  String configPage = F("<div><h2>Channels config</h2>");
  for (uint8_t  i = 0; i < CHANNELS_COUNT; ++i) {
    configPage.concat(getChannelConfigHtml(&channels[i]));
  }
  configPage.concat(F("</div>"));
  httpServer.send(200, "text/html", configPage);
}

String getChannelConfigHtml(Channel *c) {
  String html = F("<div>");
  html.concat(c->param->getValue());
  html.concat("</div>");
  return html;
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
            mqttServer.updateValue(json[mqttServer.getName()]);
            mqttPort.updateValue(json[mqttPort.getName()]);
            moduleName.updateValue(json[moduleName.getName()]);
            moduleLocation.updateValue(json[moduleLocation.getName()]);
            for (uint8_t i = 0; i < CHANNELS_COUNT; ++i) {
              channels[i].param->updateValue(json[channels[i].param->getName()]);
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
            unsigned int ioc = line.indexOf('=');
            if (ioc >= 0 && ioc + 1 < line.length()) {
              String key = line.substring(0, ioc++);
              log("Read key", key);
              String val = line.substring(ioc, line.length());
              log("Key value", val);
              if (key.equals(mqttPort.getName())) {
                mqttPort.updateValue(val.c_str());
              } else if (key.equals(mqttServer.getName())) {
                mqttServer.updateValue(val.c_str());
              } else if (key.equals(moduleLocation.getName())) {
                moduleLocation.updateValue(val.c_str());
              } else if (key.equals(moduleName.getName())) {
                moduleName.updateValue(val.c_str());
              } else {
                for (uint8_t i = 0; i < CHANNELS_COUNT; ++i) {
                  if (key.equals(channels[i].param->getName())) {
                    channels[i].param->updateValue(val.c_str());
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
    json[mqttServer.getName()] = mqttServer.getValue();
    json[mqttPort.getName()] = mqttPort.getValue();
    json[moduleName.getName()] = moduleName.getValue();
    json[moduleLocation.getName()] = moduleLocation.getValue();
    for (uint8_t i = 0; i < CHANNELS_COUNT; ++i) {
      json[channels[i].param->getName()] = channels[i].param->getValue();
    }
    json.printTo(configFile);
  #else
    String line = String(mqttServer.getName()) + "=" + String(mqttServer.getValue());
    configFile.println(line);
    line = String(mqttPort.getName()) + "=" + String(mqttPort.getValue());
    configFile.println(line);
    line = String(moduleName.getName()) + "=" + String(moduleName.getValue());
    configFile.println(line);
    line = String(moduleLocation.getName()) + "=" + String(moduleLocation.getValue());
    configFile.println(line);
    for (uint8_t i = 0; i < CHANNELS_COUNT; ++i) {
      line = String(channels[i].param->getName()) + "=" + String(channels[i].param->getValue());
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
  WiFi.disconnect();
  delay(200);
  ESP.restart();
}

void publishState (Channel *c) {
  mqttClient.publish(getChannelTopic(c, "state").c_str(), String(c->sensorState).c_str());
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
  if (strlen(_stationName) <= 0) {
    size_t size = MODULE_TYPE.length() + moduleLocation.getValueLength() + moduleName.getValueLength() + 4;
    String sn;
    sn.concat(MODULE_TYPE);
    sn.concat("_");
    sn.concat(moduleLocation.getValue()); 
    sn.concat("_");
    sn.concat(moduleName.getValue());
    sn.toCharArray(_stationName, size);
  } 
  return _stationName;
}

void connectBroker() {
  if (nextBrokerConnAtte <= millis()) {
    nextBrokerConnAtte = millis() + MQTT_BROKER_CONNECTION_RETRY;
    log(F("Connecting MQTT broker as"), getStationName());
    if (mqttClient.connect(getStationName())) {
      log(F("MQTT broker connected"));
      mqttClient.subscribe(getStationTopic("#").c_str());
    }
  } else {
    log(F("Failed. RC:"), mqttClient.state());
  }
}

String getChannelTopic (Channel *c, String cmd) {
  return MODULE_TYPE + F("/") + moduleLocation.getValue() + F("/") + c->param->getValue() + F("/") + cmd;
}

String getStationTopic (String cmd) {
  return MODULE_TYPE + F("/") + moduleLocation.getValue() + F("/") + moduleName.getValue() + F("/") + cmd;
}
