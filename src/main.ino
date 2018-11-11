#include <ESPDomotic.h>

#ifdef ESP01
Channel _channelA ("A", "Light", 2, INPUT, LOW);
Channel _channelB ("B", "Switch", 3, INPUT, LOW);

const uint8_t TX_PIN          = 1;
const uint8_t LED_PIN         = -1;
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

ESPDomotic _domoticModule;

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
 String ssid = "Light switch " + String(ESP.getChipId());
  _domoticModule.setPortalSSID(ssid.c_str());
  #ifndef ESP01
  _domoticModule.setFeedbackPin(LED_PIN);
  #endif
  // _domoticModule.setMqttConnectionCallback(mqttConnectionCallback);
  _domoticModule.setMqttMessageCallback(receiveMqttMessage);
  _domoticModule.setModuleType("sensor");
  _domoticModule.addChannel(&_channelA);
  _domoticModule.addChannel(&_channelB);
  _domoticModule.init();
  _domoticModule.getHttpServer()->on("/config", showConfig);
}

void showConfig () {
  String configPage = F("<div><h2>Channels config</h2>");
  for (uint8_t  i = 0; i < _domoticModule.getChannelsCount(); ++i) {
    configPage.concat(getChannelConfigHtml(_domoticModule.getChannel(i)));
  }
  configPage.concat(F("</div>"));
  _domoticModule.getHttpServer()->send(200, "text/html", configPage);
}

String getChannelConfigHtml(Channel *c) {
  String html = F("<div>");
  html.concat(c->name);
  html.concat(F("</div>"));
  return html;
}

void loop() {
  _domoticModule.getHttpServer()->handleClient();
  processPhysicalInput();
  _domoticModule.loop();
  delay(READ_INTERVAL);
}

void receiveMqttMessage(char* topic, unsigned char* payload, unsigned int length) {
}

void processPhysicalInput() {
  for (size_t i = 0; i < _domoticModule.getChannelsCount(); ++i) {
    if (_domoticModule.getChannel(i)->isEnabled()) {
      processChannelInput(_domoticModule.getChannel(i));
    }
  }
}

void processChannelInput(Channel *c) {
  int read = digitalRead(c->pin);
  log("Read from sensor [" + String(c->name) + "]", read);
  if (read != c->state) {
    log(F("Phisical switch state has changed. Updating channel"), c->name);
    c->state = read;
    _domoticModule.getMqttClient()->publish(_domoticModule.getChannelTopic(c, "state").c_str(), String(c->state).c_str());
  }
}