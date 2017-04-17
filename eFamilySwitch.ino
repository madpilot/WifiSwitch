#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include "Config.h"
#include "MQTT.h"
#include "WifiManager.h"

#include "Syslog.h"
Syslog Syslogger;

#define CONFIG_AP_SSID "coffee-machine"

#define ON_STATE  1
#define OFF_STATE 0

#define ON_COMMAND "ON"
#define OFF_COMMAND "OFF"

#define ON_PAYLOAD "ON"
#define OFF_PAYLOAD "OFF"

#define LED              4
#define RELAY            5
#define BUTTON           13

bool configMode = false;
bool otaMode = false;

int switchState = OFF_STATE;

Config config;
PubSub *pubSub = NULL;

int buttonPressed = false;
void switchOn();
void switchOff();
void toggleSwitch();

WifiManager wifiManager(&config);

void pubSubCallback(char* topic, byte* payload, unsigned int length) {
  char *p = (char *)malloc((length + 1) * sizeof(char *));
  if(p == NULL) {
    Syslogger.send(SYSLOG_WARNING, "Unable to read MQTT message: Payload too large.");
    return;
  }
  strncpy(p, (char *)payload, length);
  p[length] = '\0';
  
  if(strcmp(p, ON_COMMAND) == 0) {
    Syslogger.send(SYSLOG_INFO, "Turning Switch On");
    switchOn();
  } else if(strcmp(p, OFF_COMMAND) == 0) {
    Syslogger.send(SYSLOG_INFO, "Turning Switch Off");
    switchOff();
  }
  free(p);
}

int getSwitchState() {
  return switchState;
}

void setSwitchState(int state) {
  switch(state) {
    case ON_STATE:
      Syslogger.send(SYSLOG_INFO, "Switch is On");
      pubSub->publish(ON_PAYLOAD);
      break;
    case OFF_STATE:
      Syslogger.send(SYSLOG_INFO, "Switch is Off");
      pubSub->publish(OFF_PAYLOAD);
      break;
  }
  switchState = state;
}


void switchOn() {
  digitalWrite(RELAY, ON_STATE);
  setSwitchState(ON_STATE);
}

void switchOff() {
  digitalWrite(RELAY, OFF_STATE);
  setSwitchState(OFF_STATE);
}

void switchToggle() {
  if(getSwitchState() == ON_STATE) {
    switchOff();
  } else {
    switchOn();
  }
}

void buttonPress() {
  buttonPressed = true;
}

config_result configSetup() {
  config_result result = config.read();
  switch(result) {
    case E_CONFIG_OK:
      Serial.println("Config read");
      break;
    case E_CONFIG_FS_ACCESS:
      Serial.println("E_CONFIG_FS_ACCESS: Couldn't access file system");
      break;
    case E_CONFIG_FILE_NOT_FOUND:
      Serial.println("E_CONFIG_FILE_NOT_FOUND: File not found");
      break;
    case E_CONFIG_FILE_OPEN:
      Serial.println("E_CONFIG_FILE_OPEN: Couldn't open file");
      break;
    case E_CONFIG_PARSE_ERROR:
      Serial.println("E_CONFIG_PARSE_ERROR: File was not parsable");
      break;
  }
  return result;
}

void wifiSetup() {
  while(wifiManager.loop() != E_WIFI_OK) {
    Serial.println("Could not connect to WiFi. Will try again in 5 seconds");
    delay(5000);
  }
  Serial.println("Connected to WiFi");
}

WiFiUDP syslogSocket;
void syslogSetup() {
  if(config.get_syslog()) {
    Serial.println("Syslog enabled");
    Syslogger = Syslog(syslogSocket, config.get_syslogHost(), config.get_syslogPort(), config.get_deviceName(), config.get_deviceName());
    Syslogger.setMinimumSeverity(config.get_syslogLevel());
    Syslogger.send(SYSLOG_INFO, "Device booted.");
  }
}

void pubSubSetup() {  
  pubSub = new PubSub(config.get_mqttServerName(), config.get_mqttPort(), config.get_mqttTLS(), config.get_deviceName());
  pubSub->setCallback(pubSubCallback);
  
  pubSub->setSubscribeChannel(config.get_mqttSubscribeChannel());
  pubSub->setPublishChannel(config.get_mqttPublishChannel());

  pubSub->setAuthMode(config.get_mqttAuthMode());

  Syslogger.send(SYSLOG_DEBUG, "Loading certificate.");
  pubSub->setFingerprint(config.get_mqttFingerprint());
  
  switch(pubSub->loadCertificate("/client.crt.der")) {
    case E_MQTT_OK:
      Syslogger.send(SYSLOG_DEBUG, "Certificate loaded.");
      break;
    case E_MQTT_CERT_NOT_LOADED:
      Syslogger.send(SYSLOG_ERROR, "Certificate not loaded.");
      break;
    case E_MQTT_CERT_FILE_NOT_FOUND:
      Syslogger.send(SYSLOG_ERROR, "Couldn't find certificate file.");
      break;
    case E_MQTT_SPIFFS:
      Syslogger.send(SYSLOG_CRITICAL, "Unable to start SPIFFS.");
      break;
  }
  
  Syslogger.send(SYSLOG_DEBUG, "Loading private key.");    
  switch(pubSub->loadPrivateKey("/client.key.der")) {
     case E_MQTT_OK:
      Syslogger.send(SYSLOG_DEBUG, "Private key loaded.");
      break;
    case E_MQTT_PRIV_KEY_NOT_LOADED:
      Syslogger.send(SYSLOG_ERROR, "Private key not loaded.");
      break;
    case E_MQTT_PRIV_KEY_FILE_NOT_FOUND:
      Syslogger.send(SYSLOG_ERROR, "Couldn't find private key file.");
      break;
    case E_MQTT_SPIFFS:
      Syslogger.send(SYSLOG_CRITICAL, "Unable to start SPIFFS.");
      break;
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(LED, OUTPUT);
  pinMode(RELAY, OUTPUT);

  digitalWrite(LED, HIGH);
  digitalWrite(RELAY, LOW);

  pinMode(BUTTON, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON), buttonPress, FALLING);
 
  config_result configResult = configSetup();
  if(configResult != E_CONFIG_OK) {
    Syslogger.send(SYSLOG_DEBUG, "In configuration mode.");
    configMode = true;
    return;
  }


  wifiSetup();
  syslogSetup();

  if(digitalRead(BUTTON) == 0) {
    otaMode = true;
    digitalWrite(LED, LOW);
    ArduinoOTA.setPort(8266);
    ArduinoOTA.setHostname("coffee-machine");
    ArduinoOTA.onStart([]() {
      Syslogger.send(SYSLOG_DEBUG, "Starting OTA Update.");
    });
    ArduinoOTA.onEnd([]() {
      Syslogger.send(SYSLOG_DEBUG, "OTA Update complete.");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      unsigned int percent = (progress / (total / 100));
      if(percent % 2 == 0) {
        digitalWrite(LED, LOW);
      }
      if(percent % 2 == 1) {
        digitalWrite(LED, HIGH);
      }

      if(percent % 10 == 0) {
        char *str = "OTA Update progress: 100%";
        sprintf(str, "OTA Update progress: %u%%", percent);
        Syslogger.send(SYSLOG_DEBUG, str);
      }
    });
    ArduinoOTA.onError([](ota_error_t error) {
      switch(error) {
        case OTA_AUTH_ERROR:
          Syslogger.send(SYSLOG_DEBUG, "OTA Update failed: Authentication failed.");
          break;
        case OTA_BEGIN_ERROR:
          Syslogger.send(SYSLOG_DEBUG, "OTA Update failed: Begin failed.");
          break;
        case OTA_CONNECT_ERROR:
          Syslogger.send(SYSLOG_DEBUG, "OTA Update failed: Connect failed.");
          break;
        case OTA_RECEIVE_ERROR:
          Syslogger.send(SYSLOG_DEBUG, "OTA Update failed: Receive failed.");
          break;
        case OTA_END_ERROR:
          Syslogger.send(SYSLOG_DEBUG, "OTA Update failed: End failed.");
          break;
      }
    });
    ArduinoOTA.begin();
    
    Syslogger.send(SYSLOG_DEBUG, "Ready for OTA update. Push to coffee-machine.local:8266");
    return;
  }
    
  pubSubSetup();
  Syslogger.send(SYSLOG_DEBUG, "Ready for commands.");
}

bool firstRun = true;
void loop() {
  if(otaMode) {
    ArduinoOTA.handle();
    return;
  }

  if(configMode) {
    return;
  }

  if(buttonPressed) {
    buttonPressed = false;
    switchToggle();
  }

  int wifiRes = wifiManager.loop();
  if(wifiRes != E_WIFI_OK) {
    //Serial.print("Unable to connect to WiFI: ");
    //Serial.println(wifiRes);
  }

  int loopRes = pubSub->loop();
  if(loopRes != E_MQTT_OK) {
    if(firstRun) {
      firstRun = false;
    }
  } else if(firstRun) {
    firstRun = false;
    Syslogger.send(SYSLOG_DEBUG, "Successfully connected to MQTT Server.");
  }
}
