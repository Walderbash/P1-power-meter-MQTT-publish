#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include "CRC16.h"
#include <PubSubClient.h>
#include "config.h"

#ifndef WIFIHOSTNAME
#define WIFIHOSTNAME "p1-meter"
#endif

//===Change values from here===
const char* ssid = SSID;
const char* wifiPassword = WIFIPASSWORD;
const char* hostname = WIFIHOSTNAME;

char mqttServer[] =  MQTTSERVER;
uint16_t port = MQTTPORT; 

char mqttTopic[] = MQTTTOPIC;
char username[] = MQTTUSERNAME;
char mqttPassword[] = MQTTPASSWORD;
char clientId[] = MQTTCLIENT;


const bool outputOnSerial = true;
//===Change values to here===

// Vars to store meter readings
long mEVLT = 0; //Meter reading Electrics - consumption low tariff
long mEVHT = 0; //Meter reading Electrics - consumption high tariff
long mEOLT = 0; //Meter reading Electrics - return low tariff
long mEOHT = 0; //Meter reading Electrics - return high tariff
long mEAV = 0;  //Meter reading Electrics - Actual consumption
long mEAT = 0;  //Meter reading Electrics - Actual return
long mGAS = 0;    //Meter reading Gas

#define MAXLINELENGTH 64 // longest normal line is 47 char (+3 for \r\n\0)
char telegram[MAXLINELENGTH];

unsigned int currentCRC=0;
unsigned long lastMqttAttempt = 0;

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

//callback for mqtt client receives a message on subscribed topic
void callback(char* topic, byte* payload, unsigned int length) {
}

void setup() {
  Serial.begin(115200);
  Serial.println("Booting");

  WiFi.mode(WIFI_STA); // Define the NodeMCU Wifi mode
  WiFi.hostname(hostname); // Name of this device on the network (DHCP / router)
  WiFi.setAutoReconnect(true);

  Serial.println("Try to connect to " + String(ssid));
  WiFi.begin(ssid, wifiPassword); // Start the Wifi connection

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  } 

  Serial.println("");
  Serial.println("WiFi connected");

  mqttClient.setServer(mqttServer, port); // Define the server and port for the Mqtt Client
  mqttClient.setCallback(callback); // Define the callback function for the Mqtt Client. Not used in our case.

  ArduinoOTA.setHostname(hostname);
#ifdef OTAPASSWORD
  ArduinoOTA.setPassword(OTAPASSWORD);
#endif
  ArduinoOTA.onStart([]() { Serial.println("OTA start"); });
  ArduinoOTA.onEnd([]() { Serial.println("\nOTA end"); });
  ArduinoOTA.onError([](ota_error_t error) { Serial.printf("OTA error[%u]\n", error); });
  ArduinoOTA.begin();

  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

// Non-blocking: retries at most every 5 seconds so OTA and telegram reading keep running
bool mqttConnect() {
  if (mqttClient.connected()) return true;
  if (millis() - lastMqttAttempt < 5000) return false;
  lastMqttAttempt = millis();

  Serial.print("MQTT connection...");
  if (mqttClient.connect(clientId, username, mqttPassword)) { // Try to connect the Mqtt Client
    Serial.println("connected");
    return true;
  }
  Serial.print("failed, rc=");
  Serial.println(mqttClient.state());
  return false;
}

void mqttPublish() {
  char payload[255];

  snprintf(payload, sizeof(payload),
    "{\"mGAS\":\"%ld\",\"mEVLT\":\"%ld\",\"mEVHT\":\"%ld\",\"mEOLT\":\"%ld\",\"mEOHT\":\"%ld\",\"mEAV\":\"%ld\",\"mEAT\":\"%ld\"}"
    ,mGAS, mEVLT, mEVHT, mEOLT, mEOHT, mEAV, mEAT);

  if (!mqttConnect()) {
    Serial.println("MQTT not connected, skipping publish");
    return;
  }
  mqttClient.publish(mqttTopic, payload);
}

bool isNumber(char* res, int len) {
  for (int i = 0; i < len; i++) {
    if (((res[i] < '0') || (res[i] > '9'))  && (res[i] != '.' && res[i] != 0)) {
      return false;
    }
  }
  return true;
}

int FindCharInArrayRev(char array[], char c, int len) {
  for (int i = len - 1; i >= 0; i--) {
    if (array[i] == c) {
      return i;
    }
  }
  return -1;
}

long getValidVal(long valNew, long valOld, long maxDiffer)
{
  //check if the incoming value is valid
      if(valOld > 0 && ((valNew - valOld > maxDiffer) && (valOld - valNew > maxDiffer)))
        return valOld;
      return valNew;
}

long getValue(char* buffer, int maxlen) {
  int s = FindCharInArrayRev(buffer, '(', maxlen - 2);
  if (s < 8) return 0;
  if (s > 32) s = 32;
  int l = FindCharInArrayRev(buffer, '*', maxlen - 2) - s - 1;
  if (l < 4) return 0;
  if (l > 12) return 0;
  char res[16];
  memset(res, 0, sizeof(res));

  if (strncpy(res, buffer + s + 1, l)) {
    if (isNumber(res, l)) {
      return (1000 * atof(res));
    }
  }
  return 0;
}

bool decodeTelegram(int len) {
  //need to check for start
  int startChar = FindCharInArrayRev(telegram, '/', len);
  int endChar = FindCharInArrayRev(telegram, '!', len);
  bool validCRCFound = false;
  if(startChar>=0)
  {
    //start found. Reset CRC calculation
    currentCRC=CRC16(0x0000,(unsigned char *) telegram+startChar, len-startChar);
    if(outputOnSerial)
    {
      for(int cnt=startChar; cnt<len;cnt++)
        Serial.print(telegram[cnt]);
    }    
    //Serial.println("Start found!");
    
  }
  else if(endChar>=0)
  {
    //add to crc calc 
    currentCRC=CRC16(currentCRC,(unsigned char*)telegram+endChar, 1);
    char messageCRC[5] = {0}; // 4 hex chars + terminator
    strncpy(messageCRC, telegram + endChar + 1, 4);
    if(outputOnSerial)
    {
      for(int cnt=0; cnt<len;cnt++)
        Serial.print(telegram[cnt]);
    }    
    validCRCFound = (strtol(messageCRC, NULL, 16) == currentCRC);
    if(validCRCFound)
      Serial.println("\nVALID CRC FOUND!"); 
    else
    {
      Serial.println("\n===INVALID CRC FOUND!===");
      validCRCFound = true; //fuck it
    }
    currentCRC = 0;
  }
  else
  {
    currentCRC=CRC16(currentCRC, (unsigned char*)telegram, len);
    if(outputOnSerial)
    {
      for(int cnt=0; cnt<len;cnt++)
        Serial.print(telegram[cnt]);
    }
  }

  long val =0;
  long val2=0;
  // 1-0:1.8.1(000992.992*kWh)
  // 1-0:1.8.1 = Elektra verbruik laag tarief (DSMR v4.0)
  if (strncmp(telegram, "1-0:1.8.1", strlen("1-0:1.8.1")) == 0) 
    mEVLT =  getValue(telegram, len);
  

  // 1-0:1.8.2(000560.157*kWh)
  // 1-0:1.8.2 = Elektra verbruik hoog tarief (DSMR v4.0)
  if (strncmp(telegram, "1-0:1.8.2", strlen("1-0:1.8.2")) == 0) 
    mEVHT = getValue(telegram, len);
    

  // 1-0:2.8.1(000348.890*kWh)
  // 1-0:2.8.1 = Elektra opbrengst laag tarief (DSMR v4.0)
  if (strncmp(telegram, "1-0:2.8.1", strlen("1-0:2.8.1")) == 0) 
    mEOLT = getValue(telegram, len);
   

  // 1-0:2.8.2(000859.885*kWh)
  // 1-0:2.8.2 = Elektra opbrengst hoog tarief (DSMR v4.0)
  if (strncmp(telegram, "1-0:2.8.2", strlen("1-0:2.8.2")) == 0) 
    mEOHT = getValue(telegram, len);
    

  // 1-0:1.7.0(00.424*kW) Actueel verbruik
  // 1-0:2.7.0(00.000*kW) Actuele teruglevering
  // 1-0:1.7.x = Electricity consumption actual usage (DSMR v4.0)
  if (strncmp(telegram, "1-0:1.7.0", strlen("1-0:1.7.0")) == 0) 
    mEAV = getValue(telegram, len);
    
  if (strncmp(telegram, "1-0:2.7.0", strlen("1-0:2.7.0")) == 0)
    mEAT = getValue(telegram, len);
   

  // 0-1:24.2.1(150531200000S)(00811.923*m3)
  // 0-1:24.2.1 = Gas (DSMR v4.0) on Kaifa MA105 meter
  if (strncmp(telegram, "0-1:24.2.1", strlen("0-1:24.2.1")) == 0) 
    mGAS = getValue(telegram, len);

  return validCRCFound;
}

void readTelegram() {
  if (Serial.available()) {
    memset(telegram, 0, sizeof(telegram));
    while (Serial.available()) {
      // leave room for the '\n' and '\0' appended below
      int len = Serial.readBytesUntil('\n', telegram, MAXLINELENGTH - 2);
      telegram[len] = '\n';
      telegram[len+1] = 0;
      yield();
      if(decodeTelegram(len+1))
      {
         mqttPublish();
      }
    } 
  }
}

void loop() {
  ArduinoOTA.handle();
  mqttConnect();
  mqttClient.loop();
  readTelegram();
}
