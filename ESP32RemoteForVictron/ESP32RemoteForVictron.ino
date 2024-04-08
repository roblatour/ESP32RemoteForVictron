// ESP32 Victron Monitor (version 1)
//
// Copyright Rob Latour, 2024
// License: MIT
// https://github.com/roblatour/ESP32RemoteForVictron
//
// Design and tested with a Victron Multiplus II 12v system, monitored by a Rapsberry Pi Zero 2 W running Victron Venus Firmware v3.3
//
// Compile and upload using Arduino IDE (2.3.2 or greater)
//
// Physical board:                  LILYGO T-Display-S3 AMOLED
//
// Board in Arduino board manager:  ESP32S3 Dev Module
//
// Arduino Tools settings:
// USB CDC On Boot:                 Enabled
// CPU Frequency:                   240MHz (WiFi)
// Core Debug Level:                None
// USB DFU On Boot:                 Enabled
// Erase All Flash Before Upload:   Disabled
// Events Run On:                   Core 1
// Flash Mode:                      QIO 80Mhz
// Flash Size:                      16MB (128MB)
// JTAG Adapter:                    Disabled
// Arduino Runs On:                 Core 1
// USB Firmware MSC On Boot:        Disabled
// Partition Scheme:                16 M Flash (3MB APP/9.9MB FATFS)
// PSRAM:                           OPI PSRAM
// Upload Mode:                     UART0 / Hardware CDC
// Upload Speed:                    921600
// USB Mode:                        Hardware CDC and JTAG
//
// Programmer                       ESPTool

// User settings
#include "general_settings.h"
#include "secret_settings.h"

// Buttons
#define topButtonIfUSBIsOnTheLeft 21
#define bottomButtonIfUSBIsOnTheLeft 0

// Globals
const String programName = "ESP32 Remote for Victron";
const String programVersion = "(Version 1)";
const String programURL = "https://github.com/roblatour/ESP32RemoteForVictron";

String VictronInstallationID = "+";
String MultiplusThreeDigitID = "+";

const int dataPoints = 8;
bool awaitingDataToBeReceived[dataPoints];
bool awaitingInitialTrasmissionOfAllDataPoints;

float gridInL1Watts = 0.0;

float solarWatts = 0.0;

float batterySOC = 0.0;
float batteryTTG = 0.0;
float batteryPower = 0.0;

float ACOutL1Watts = 0.0;
float ACOutL2Watts = 0.0;

typedef enum multiplusMode { ChargerOnly,
                             InverterOnly,
                             On,
                             Off,
                             Unknown
};
multiplusMode currentMultiplusMode = Unknown;

typedef enum multiplusFunction { Charger,
                                 Inverter };

int topButton, bottomButton;

// Display
#include <TFT_eSPI.h>              // download and use the entire TFT_eSPI https://github.com/Xinyuan-LilyGO/T-Display-S3-AMOLED/tree/main/lib
#include "rm67162.h"               // included in the github package for this sketch, but also available from https://github.com/Xinyuan-LilyGO/T-Display-S3-AMOLED/tree/main/examples/factory
#include "fonts\NotoSansBold15.h"  // included in the github package for this sketch, based on https://fonts.google.com/noto/specimen/Noto+Sans
#include "fonts\NotoSansBold24.h"  // "
#include "fonts\NotoSansBold36.h"  // "
#include "fonts\NotoSansBold72.h"  // "

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite sprite = TFT_eSprite(&tft);

// MQTT
#include <EspMQTTClient.h>  // https://github.com/plapointe6/EspMQTTClient (v1.13.3)

#include <string.h>

EspMQTTClient client(
  SECRET_SETTINGS_WIFI_SSID,
  SECRET_SETTINGS_WIFI_PASSWORD,
  MQTTBroker,
  MQTTUserID,
  MQTTPassword,
  MQTTClientName,
  MQTTPort);

unsigned long lastMQTTUpdateReceived = 0;

// JSON
#include <ArduinoJson.h>  // Ardiuno Library Manager, by Benoit Blanchon, https://arduinojson.org/?utm_source=meta&utm_medium=library.properties (v7.0.4)

// Non blocking delay library
#include "FireTimer.h"  // Ardiuno Library Manager, by PowerBroker2, https://github.com/PowerBroker2/FireTimer (v1.0.5)
FireTimer msTimer;

void TurnOffGreenLED() {

  int greenLEDPin = 38;
  pinMode(greenLEDPin, OUTPUT);
  digitalWrite(greenLEDPin, LOW);
}

void SetupTopAndBottomButtons() {

  // Notes:
  // This sketch was built for a LILYGO T-Display-S3 AMOLED and as such:
  // 1. The two buttons on the device have built in hardware debounce protection, so debounce logic is not needed in this sketch
  // 2. The postion of the 'top' and 'bottom' buttons are relative to the side on which the USB cable is plugged into, so the code below adjusts for that

  // enable buttons
  pinMode(topButtonIfUSBIsOnTheLeft, INPUT);
  pinMode(bottomButtonIfUSBIsOnTheLeft, INPUT);

  if (GENERAL_SETTINGS_USB_ON_THE_LEFT) {
    topButton = bottomButtonIfUSBIsOnTheLeft;
    bottomButton = topButtonIfUSBIsOnTheLeft;
  } else {
    topButton = topButtonIfUSBIsOnTheLeft;
    bottomButton = bottomButtonIfUSBIsOnTheLeft;
  };
}

void RefreshScreen() {
  lcd_PushColors(0, 0, TFT_WIDTH, TFT_HEIGHT, (uint16_t *)sprite.getPointer());
}

void ChangeMultiplusMode(multiplusFunction option) {

  // show the opening prompt

  sprite.fillSprite(TFT_BLACK);

  // this should not happen, but throw an error if the current Multiplus mode is unknown

  if (currentMultiplusMode == Unknown) {
    sprite.setTextDatum(MC_DATUM);
    sprite.setTextColor(TFT_RED, TFT_BLACK);
    sprite.loadFont(NotoSansBold24);
    sprite.drawString("Multiplus mode cannot be changed", TFT_WIDTH / 2, TFT_HEIGHT / 2);
    RefreshScreen();
    sprite.unloadFont();
    delay(5000);
    return;
  };

  // show opening prompt

  sprite.loadFont(NotoSansBold36);
  sprite.setTextDatum(MC_DATUM);
  sprite.setTextColor(TFT_SKYBLUE, TFT_BLACK);

  if (option == Charger) {
    sprite.drawString("Set charger on or off?", TFT_WIDTH / 2, TFT_HEIGHT / 2);
    RefreshScreen();
  } else {
    sprite.drawString("Set inverter on or off?", TFT_WIDTH / 2, TFT_HEIGHT / 2);
    RefreshScreen();
  };

  // wait here for one second and also, if needed, for the user to release the button that they had previousily pressed to get here
  msTimer.begin(1000);

  // ensure whichever button was last pressed is now released
  while (digitalRead(topButton) == 0)
    msTimer.begin(50);

  while (digitalRead(bottomButton) == 0)
    msTimer.begin(50);

  // prompt for desired state

  int xPosition;
  if (GENERAL_SETTINGS_USB_ON_THE_LEFT) {
    xPosition = 0;
    sprite.setTextDatum(TL_DATUM);
  } else {
    xPosition = TFT_WIDTH;
    sprite.setTextDatum(TR_DATUM);
  };

  if (option == Charger) {
    if ((currentMultiplusMode == ChargerOnly) || (currentMultiplusMode == On)) {
      sprite.drawString("ON (current mode)", xPosition, 0);
      sprite.drawString("OFF", xPosition, TFT_HEIGHT - 30);
    } else {
      sprite.drawString("ON", xPosition, 0);
      sprite.drawString("OFF (current mode)", xPosition, TFT_HEIGHT - 30);
    }
  } else {
    if ((currentMultiplusMode == InverterOnly) || (currentMultiplusMode == On)) {
      sprite.drawString("ON (current mode)", xPosition, 0);
      sprite.drawString("OFF", xPosition, TFT_HEIGHT - 30);
    } else {
      sprite.drawString("ON", xPosition, 0);
      sprite.drawString("OFF (current mode)", xPosition, TFT_HEIGHT - 30);
    };
  };

  RefreshScreen();

  sprite.unloadFont();

  // wait until a choice is made

  while ((digitalRead(topButton) != 0) && (digitalRead(bottomButton) != 0))
    msTimer.begin(10);

  bool userChoseOn;

  if (digitalRead(topButton) == 0) {
    userChoseOn = true;
    if (GENERAL_SETTINGS_DEBUG_OUTPUT)
      Serial.println("choice is 'ON'");
  } else {
    userChoseOn = false;
    if (GENERAL_SETTINGS_DEBUG_OUTPUT)
      Serial.println("choice is 'OFF'");
  };

  // ensure whichever button was last pressed is now released
  while (digitalRead(topButton) == 0)
    msTimer.begin(50);

  while (digitalRead(bottomButton) == 0)
    msTimer.begin(50);

  multiplusMode desiredMultiplusMode;
  desiredMultiplusMode = currentMultiplusMode;

  if (option == Charger) {

    if (userChoseOn) {

      switch (currentMultiplusMode) {
        case ChargerOnly:
          break;
        case InverterOnly:
          desiredMultiplusMode = On;
          break;
        case On:
          break;
        case Off:
          desiredMultiplusMode = ChargerOnly;
          break;
      }
    } else {
      switch (currentMultiplusMode) {
        case ChargerOnly:
          desiredMultiplusMode = Off;
          break;
        case InverterOnly:
          break;
        case On:
          desiredMultiplusMode = InverterOnly;
          break;
        case Off:
          break;
      };
    };
  };

  if (option == Inverter) {

    if (userChoseOn) {

      switch (currentMultiplusMode) {
        case ChargerOnly:
          desiredMultiplusMode = On;
          break;
        case InverterOnly:
          break;
        case On:
          break;
        case Off:
          desiredMultiplusMode = InverterOnly;
          break;
      }
    } else {
      switch (currentMultiplusMode) {
        case ChargerOnly:
          break;
        case InverterOnly:
          desiredMultiplusMode = Off;
          break;
        case On:
          desiredMultiplusMode = ChargerOnly;
          break;
        case Off:
          break;
      };
    };
  };

  // apply the change if required

  if (currentMultiplusMode == desiredMultiplusMode) {

    if (GENERAL_SETTINGS_DEBUG_OUTPUT)
      Serial.println("no change required to the multiplus mode");

  } else {

    String modeCodeValue;

    switch (desiredMultiplusMode) {

      case ChargerOnly:
        modeCodeValue = "1";
        if (GENERAL_SETTINGS_DEBUG_OUTPUT)
          Serial.println("set multiplus mode to charger only");
        break;
      case InverterOnly:
        modeCodeValue = "2";
        if (GENERAL_SETTINGS_DEBUG_OUTPUT)
          Serial.println("set multiplus mode to inverter only");
        break;
      case On:
        modeCodeValue = "3";
        if (GENERAL_SETTINGS_DEBUG_OUTPUT)
          Serial.println("set multiplus mode to on");
        break;
      case Off:
        modeCodeValue = "4";
        if (GENERAL_SETTINGS_DEBUG_OUTPUT)
          Serial.println("set multiplus mode to off");
        break;
    };

    // set the Multiplus's mode to Unknown while it changes over
    // it will be reset to its current mode in the next MQTT publishing cycle
    currentMultiplusMode = Unknown;

    // change the mode
    client.publish("W/" + VictronInstallationID + "/vebus/" + MultiplusThreeDigitID + "/Mode", "{\"value\": " + modeCodeValue + "}");
    delay(250);
  };
};

void CheckButtons() {

  if (GENERAL_SETTINGS_ALLOW_CHANGING_INVERTER_AND_CHARGER_MODES) {

    // The top button is used to turn on/off the charger
    // The bottom button is used to turn on/off the inverter

    if (digitalRead(topButton) == 0)
      ChangeMultiplusMode(Charger);

    if (digitalRead(bottomButton) == 0)
      ChangeMultiplusMode(Inverter);
  };
}

void SetupDisplayOrientation() {

  if (GENERAL_SETTINGS_USB_ON_THE_LEFT)
    lcd_setRotation(3);
  else
    lcd_setRotation(1);
};

String ConvertSecondstoDayHoursMinutes(int n) {

  String sDays, sHours, sMinutes;

  int day = n / (24 * 3600);

  if ((day) > 0)
    sDays = String(day);
  else
    sDays = "";

  n = n % (24 * 3600);

  int hour = n / 3600;
  sHours = String(hour);

  n %= 3600;

  int minutes = n / 60;
  if (minutes < 10)
    sMinutes = "0" + String(minutes);
  else
    sMinutes = String(minutes);

  return (sDays + " " + sHours + ":" + sMinutes);
  //return (sDays + "D " + sHours + "H " + sMinutes + "M");
}

String ConvertToStringWithAFixedNumberOfDecimalPlaces(float f, int numberOfDecimalPlaces = 1) {

  char workingArray[20 + numberOfDecimalPlaces];
  char formattedArray[20 + numberOfDecimalPlaces];

  dtostrf(f, 15 + numberOfDecimalPlaces, numberOfDecimalPlaces, formattedArray);
  sprintf(workingArray, "%s", formattedArray);

  String returnValue = String(workingArray);

  returnValue.trim();

  return returnValue;
}

void UpdateScreen() {

  static unsigned long nextScreenUpdate = 0;

  // only update the screen at intervals set by the value in GENERAL_SETTINGS_SECONDS_BETWEEN_SCREEN_UPDATES
  if (millis() < (nextScreenUpdate + GENERAL_SETTINGS_SECONDS_BETWEEN_SCREEN_UPDATES * 1000))
    return;

  // if the connection is not yet established, or had been lost then display an appropriate message

  if (!client.isWifiConnected()) {

    sprite.fillSprite(TFT_BLACK);
    sprite.loadFont(NotoSansBold36);
    sprite.setTextDatum(MC_DATUM);
    sprite.setTextColor(TFT_SKYBLUE, TFT_BLACK);
    sprite.drawString("Awaiting Wi-Fi connection", TFT_WIDTH / 2, TFT_HEIGHT / 2);
    RefreshScreen();
    sprite.unloadFont();
    return;
  };

  if (!client.isMqttConnected()) {

    sprite.fillSprite(TFT_BLACK);
    sprite.loadFont(NotoSansBold36);
    sprite.setTextDatum(MC_DATUM);
    sprite.setTextColor(TFT_SKYBLUE, TFT_BLACK);
    sprite.drawString("Awaiting MQTT connection", TFT_WIDTH / 2, TFT_HEIGHT / 2);
    RefreshScreen();
    sprite.unloadFont();
    return;
  };

  if (millis() > (lastMQTTUpdateReceived + 3 * GENERAL_SETTINGS_SECONDS_BETWEEN_KEEP_ALIVE_REQUESTS * 1000)) {

    sprite.fillSprite(TFT_BLACK);
    sprite.loadFont(NotoSansBold24);
    sprite.setTextDatum(MC_DATUM);
    sprite.setTextColor(TFT_RED, TFT_BLACK);
    sprite.drawString("MQTT data updates have stopped", TFT_WIDTH / 2, TFT_HEIGHT / 2);
    RefreshScreen();
    sprite.unloadFont();
    return;
  };

  // wait until data for all datapoints has been received prior to showing the screen
  // while waiting display an appropriate message

  if (awaitingInitialTrasmissionOfAllDataPoints) {

    for (int i = 0; i < dataPoints; i++)
      if (awaitingDataToBeReceived[i]) {

        if (GENERAL_SETTINGS_DEBUG_OUTPUT)
          Serial.println("Awating data on data point " + String(i));

        sprite.fillSprite(TFT_BLACK);
        sprite.loadFont(NotoSansBold36);
        sprite.setTextDatum(MC_DATUM);
        sprite.setTextColor(TFT_SKYBLUE, TFT_BLACK);
        sprite.drawString("Awaiting data", TFT_WIDTH / 2, TFT_HEIGHT / 2);
        RefreshScreen();
        sprite.unloadFont();

        return;
      };
    // if we have reached this point data for all datapoints has been received
    awaitingInitialTrasmissionOfAllDataPoints = false;
  };

  nextScreenUpdate = millis() + GENERAL_SETTINGS_SECONDS_BETWEEN_SCREEN_UPDATES * 1000;

  int x, y;

  // Tabula rasa

  sprite.fillSprite(TFT_BLACK);

  // show charger and inveter status

  if (GENERAL_SETTINGS_USB_ON_THE_LEFT)
    sprite.setTextDatum(TL_DATUM);
  else
    sprite.setTextDatum(TR_DATUM);

  sprite.loadFont(NotoSansBold24);
  sprite.setTextColor(TFT_SKYBLUE, TFT_BLACK);

  String chargerStatus;
  if (currentMultiplusMode == Unknown)
    chargerStatus = "?";
  else if ((currentMultiplusMode == On) || (currentMultiplusMode == ChargerOnly))
    chargerStatus = "on";
  else
    chargerStatus = "off";

  String inverterSatus;
  if (currentMultiplusMode == Unknown)
    inverterSatus = "?";
  else if ((currentMultiplusMode == On) || (currentMultiplusMode == InverterOnly))
    inverterSatus = "on";
  else
    inverterSatus = "off";

  if (GENERAL_SETTINGS_USB_ON_THE_LEFT)
    x = 0;
  else
    x = TFT_WIDTH;

  y = 5;
  sprite.drawString("Charger " + chargerStatus, x, y);

  y = TFT_HEIGHT - 30;
  sprite.drawString("Inverter " + inverterSatus, x, y);

  sprite.unloadFont();

  // show solar info

  sprite.loadFont(NotoSansBold24);
  sprite.setTextColor(TFT_YELLOW, TFT_BLACK);

  y = TFT_HEIGHT / 2 - 36;
  sprite.drawString("Solar", x, y);

  sprite.loadFont(NotoSansBold36);
  solarWatts = int(solarWatts);

  y = TFT_HEIGHT / 2 + 4;
  if (GENERAL_SETTINGS_IF_OVER_1000_WATTS_REPORT_KW && (solarWatts >= 1000.0F)) {
    solarWatts = solarWatts / 1000.0F;
    sprite.drawString(ConvertToStringWithAFixedNumberOfDecimalPlaces(solarWatts, GENERAL_SETTINGS_NUMBER_DECIMAL_PLACES_FOR_KW_REPORTING) + " KW", x, y);
  } else {
    sprite.drawString(String(int(solarWatts)) + " W", x, y);
  };

  sprite.unloadFont();

  // show grid info

  sprite.loadFont(NotoSansBold24);
  sprite.setTextColor(TFT_GOLD, TFT_BLACK);

  if (GENERAL_SETTINGS_USB_ON_THE_LEFT)
    sprite.setTextDatum(TR_DATUM);
  else
    sprite.setTextDatum(TL_DATUM);

  if (GENERAL_SETTINGS_USB_ON_THE_LEFT)
    x = TFT_WIDTH;
  else
    x = 0;

  y = 0;
  sprite.drawString("Grid", x, y);
  sprite.unloadFont();

  sprite.loadFont(NotoSansBold36);

  float TotalGridWatts = int(gridInL1Watts);

  y = 43;
  if (GENERAL_SETTINGS_IF_OVER_1000_WATTS_REPORT_KW && (TotalGridWatts >= 1000.0F)) {
    TotalGridWatts = TotalGridWatts / 1000.0F;
    sprite.drawString(ConvertToStringWithAFixedNumberOfDecimalPlaces(TotalGridWatts, GENERAL_SETTINGS_NUMBER_DECIMAL_PLACES_FOR_KW_REPORTING) + " KW", x, y);
  } else {
    TotalGridWatts = int(TotalGridWatts);
    sprite.drawString(String(int(TotalGridWatts)) + " W", x, y);
  };

  sprite.unloadFont();

  // show AC consumption info

  sprite.loadFont(NotoSansBold24);
  sprite.setTextColor(TFT_SILVER, TFT_BLACK);

  y = TFT_HEIGHT - 73;
  sprite.drawString("AC Load", x, y);

  sprite.unloadFont();
  sprite.loadFont(NotoSansBold36);

  y = TFT_HEIGHT - 30;
  float TotalACConsumptionWatts = int(ACOutL1Watts + ACOutL2Watts);
  if (GENERAL_SETTINGS_IF_OVER_1000_WATTS_REPORT_KW && (TotalACConsumptionWatts >= 1000.0F)) {
    TotalACConsumptionWatts = TotalACConsumptionWatts / 1000.0F;
    sprite.drawString(ConvertToStringWithAFixedNumberOfDecimalPlaces(TotalACConsumptionWatts, GENERAL_SETTINGS_NUMBER_DECIMAL_PLACES_FOR_KW_REPORTING) + " KW", x, y);
  } else {
    TotalACConsumptionWatts = int(TotalACConsumptionWatts);
    sprite.drawString(String(int(TotalACConsumptionWatts)) + " W", x, y);
  };

  sprite.unloadFont();

  // show battery info

  int midX, midY, outterRadius, innerRadius, startAngle, endAngle;
  unsigned short batteryColour;

  midX = TFT_WIDTH / 2;
  midY = TFT_HEIGHT / 2;

  if (midX < midY)
    outterRadius = midX;
  else
    outterRadius = midY;

  innerRadius = outterRadius - 8;

  startAngle = 180;
  endAngle = int(batterySOC * 3.6 + 180) % 360;

  if (batterySOC <= GENERAL_SETTINGS_SHOW_BATTERY_AS_RED) {
    batteryColour = TFT_RED;
  } else if (batterySOC <= GENERAL_SETTINGS_SHOW_BATTERY_AS_YELLOW) {
    batteryColour = TFT_YELLOW;
  } else {
    batteryColour = TFT_GREEN;
  };

  sprite.drawSmoothArc(midX, midY, outterRadius, innerRadius, startAngle, endAngle, batteryColour, TFT_BLACK);

  sprite.loadFont(NotoSansBold72);
  sprite.setTextDatum(MC_DATUM);
  sprite.setTextColor(batteryColour, TFT_BLACK);

  // show battery percent without a decimal place
  int ibatterySOC = int(batterySOC);
  sprite.drawString(String(ibatterySOC) + "%", midX, midY);

  sprite.unloadFont();

  sprite.loadFont(NotoSansBold24);
  sprite.drawString("Battery", midX, midY - 60);

  if (batteryTTG != 0) {
    String TimeToGo = ConvertSecondstoDayHoursMinutes(int(batteryTTG));
    sprite.drawString(TimeToGo, midX, midY + 50);
  };

  sprite.unloadFont();

  // Draw an upward triangle if the battery is charging or a downward triagle if it is discharging
  // However, if it is neither charging or discharging then do not draw any triangle at all

  if (batteryPower > 0.0F) {

    // Draw a upward triangle
    int16_t centerX = TFT_WIDTH / 2;
    int16_t centerY = TFT_HEIGHT / 2 + 88;

    int16_t arrowWidth = 20;
    int16_t arrowHeight = 25;

    sprite.fillTriangle(centerX - arrowWidth / 2, centerY + arrowHeight / 2,
                        centerX + arrowWidth / 2, centerY + arrowHeight / 2,
                        centerX, centerY - arrowHeight / 2, batteryColour);
  } else {

    if (batteryPower < 0.0F) {

      // Draw a downward triangle
      int16_t centerX = TFT_WIDTH / 2;
      int16_t centerY = TFT_HEIGHT / 2 + 88;

      int16_t arrowWidth = 20;
      int16_t arrowHeight = 25;

      sprite.fillTriangle(centerX - arrowWidth / 2, centerY - arrowHeight / 2,
                          centerX + arrowWidth / 2, centerY - arrowHeight / 2,
                          centerX, centerY + arrowHeight / 2, batteryColour);
    };
  };

  RefreshScreen();
}

void ResetGlobals() {

  if (VictronInstallationID.length() == 1)
    VictronInstallationID = SECRET_SETTING_VICTRON_INSTALLATION_ID;

  if (MultiplusThreeDigitID.length() == 1)
    MultiplusThreeDigitID = SECRET_SETTING_VICTRON_MULTIPLUS_ID;

  awaitingInitialTrasmissionOfAllDataPoints = true;
  for (int i = 0; i < dataPoints; i++)
    awaitingDataToBeReceived[i] = true;

  gridInL1Watts = 0.0;

  solarWatts = 0.0;

  batterySOC = 0.0;
  batteryTTG = 0.0;
  batteryPower = 0.0;

  ACOutL1Watts = 0.0;
  ACOutL2Watts = 0.0;

  currentMultiplusMode = Unknown;
};

void KeepMQTTAlive(bool forceKeepAliveRequestNow = false) {

  static unsigned long nextUpdate = 0;

  // send keep alive request as needed
  if ((millis() > nextUpdate) || (forceKeepAliveRequestNow)) {

    nextUpdate = millis() + GENERAL_SETTINGS_SECONDS_BETWEEN_KEEP_ALIVE_REQUESTS * 1000;

    client.publish("R/" + VictronInstallationID + "/keepalive", "");

    if (GENERAL_SETTINGS_DEBUG_OUTPUT)
      Serial.println("keep alive request sent");

    msTimer.begin(100);
  };

  // time out check
  if (millis() > (nextUpdate + (GENERAL_SETTINGS_SECONDS_BETWEEN_KEEP_ALIVE_REQUESTS * 1000) * 3)) {
    ResetGlobals();
  };
}

void onConnectionEstablished() {

  if (VictronInstallationID == "+") {

    // Let's find the Victron Installation ID

    client.subscribe("N/+/system/0/Serial", [](const String &topic, const String &payload) {
      client.unsubscribe("N/+/system/0/Serial");
      String mytopic = String(topic);
      Serial.println("Topic: " + mytopic);
      VictronInstallationID = mytopic.substring(2, 14);
      Serial.println("*** Discovered Installation ID: " + VictronInstallationID);

      onConnectionEstablished();
      return;
    });

    return;
  };

  if (MultiplusThreeDigitID == "+") {

    // Let's find the Multiplus three digit ID

    client.subscribe("N/" + VictronInstallationID + "/vebus/+/Mode", [](const String &topic, const String &payload) {
      client.unsubscribe("N/" + VictronInstallationID + "/vebus/+/Mode");
      String mytopic = String(topic);
      Serial.println("Topic: " + mytopic);
      MultiplusThreeDigitID = mytopic.substring(21, 24);
      Serial.println("*** Discovered Multiplus three digit ID: " + MultiplusThreeDigitID);

      onConnectionEstablished();
      return;
    });

    // a keep alive request is required for Venus to publish the topic subscribed to above
    KeepMQTTAlive(true);

    return;
  };

  // at this point we have the VictronInstallationID and the MultiplusThreeDigitID so let's get the rest of the data

  String commonTopicStartString = "N/" + VictronInstallationID + "/system/0/";
  String multiplusModeTopicString = "N/" + VictronInstallationID + "/vebus/" + MultiplusThreeDigitID + "/Mode";

  // Grid (L1)

  if (GENERAL_SETTINGS_GRID_IN_L1_IS_USED) {

    client.subscribe(commonTopicStartString + "Ac/Grid/L1/Power", [](const String &payload) {
      if (awaitingDataToBeReceived[0])
        awaitingDataToBeReceived[0] = false;
      String response = String(payload);
      DynamicJsonDocument doc(256);
      DeserializationError error = deserializeJson(doc, response);
      gridInL1Watts = doc["value"].as<float>();
      if (GENERAL_SETTINGS_DEBUG_OUTPUT)
        Serial.println("gridInL1Watts " + String(gridInL1Watts));
      lastMQTTUpdateReceived = millis();
    });

    msTimer.begin(100);
  } else {
    if (awaitingDataToBeReceived[0])
      awaitingDataToBeReceived[0] = false;
  };

  // Solar
  if (GENERAL_SETTINGS_PV_IS_USED) {
    client.subscribe(commonTopicStartString + "Dc/Pv/Power", [](const String &payload) {
      if (awaitingDataToBeReceived[1])
        awaitingDataToBeReceived[1] = false;
      String response = String(payload);
      DynamicJsonDocument doc(256);
      DeserializationError error = deserializeJson(doc, response);
      solarWatts = doc["value"].as<float>();
      if (GENERAL_SETTINGS_DEBUG_OUTPUT)
        Serial.println("solarWatts " + String(solarWatts));
      lastMQTTUpdateReceived = millis();
    });

    msTimer.begin(100);
  } else {
    if (awaitingDataToBeReceived[1])
      awaitingDataToBeReceived[1] = false;
  };

  // Battery

  client.subscribe(commonTopicStartString + "Dc/Battery/Soc", [](const String &payload) {
    if (awaitingDataToBeReceived[2])
      awaitingDataToBeReceived[2] = false;
    String response = String(payload);
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, response);
    batterySOC = doc["value"].as<float>();
    if (GENERAL_SETTINGS_DEBUG_OUTPUT)
      Serial.println("batterySOC " + String(batterySOC));
    lastMQTTUpdateReceived = millis();
  });

  msTimer.begin(100);

  client.subscribe(commonTopicStartString + "Dc/Battery/TimeToGo", [](const String &payload) {
    if (awaitingDataToBeReceived[3])
      awaitingDataToBeReceived[3] = false;
    String response = String(payload);
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, response);
    batteryTTG = doc["value"].as<float>();
    if (GENERAL_SETTINGS_DEBUG_OUTPUT)
      Serial.println("batteryTTG " + String(batteryTTG));
    lastMQTTUpdateReceived = millis();
  });

  msTimer.begin(100);

  client.subscribe(commonTopicStartString + "Dc/Battery/Power", [](const String &payload) {
    if (awaitingDataToBeReceived[4])
      awaitingDataToBeReceived[4] = false;
    String response = String(payload);
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, response);
    batteryPower = doc["value"].as<float>();
    if (GENERAL_SETTINGS_DEBUG_OUTPUT)
      Serial.println("batteryPower " + String(batteryPower));
    lastMQTTUpdateReceived = millis();
  });

  msTimer.begin(100);

  // AC Out (L1, L2)

  if (GENERAL_SETTINGS_AC_OUT_L1_IS_USED) {
    client.subscribe(commonTopicStartString + "Ac/Consumption/L1/Power", [](const String &payload) {
      if (awaitingDataToBeReceived[5])
        awaitingDataToBeReceived[5] = false;
      String response = String(payload);
      DynamicJsonDocument doc(256);
      DeserializationError error = deserializeJson(doc, response);
      ACOutL1Watts = doc["value"].as<float>();
      if (GENERAL_SETTINGS_DEBUG_OUTPUT)
        Serial.println("ACOutL1Watts " + String(ACOutL1Watts));
      lastMQTTUpdateReceived = millis();
    });

    msTimer.begin(100);
  } else {
    if (awaitingDataToBeReceived[5])
      awaitingDataToBeReceived[5] = false;
  };

  if (GENERAL_SETTINGS_AC_OUT_L2_IS_USED) {
    client.subscribe(commonTopicStartString + "Ac/Consumption/L2/Power", [](const String &payload) {
      if (awaitingDataToBeReceived[6])
        awaitingDataToBeReceived[6] = false;
      String response = String(payload);
      DynamicJsonDocument doc(256);
      DeserializationError error = deserializeJson(doc, response);
      ACOutL2Watts = doc["value"].as<float>();
      if (GENERAL_SETTINGS_DEBUG_OUTPUT)
        Serial.println("ACOutL2Watts " + String(ACOutL2Watts));
      lastMQTTUpdateReceived = millis();
    });

    msTimer.begin(100);
  } else {
    if (awaitingDataToBeReceived[6])
      awaitingDataToBeReceived[6] = false;
  };

  // Multiplus mode

  currentMultiplusMode = Unknown;

  client.subscribe(multiplusModeTopicString, [](const String &payload) {
    if (awaitingDataToBeReceived[7])
      awaitingDataToBeReceived[7] = false;
    String response = String(payload);
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, response);
    int workingMode = doc["value"].as<int>();
    switch (workingMode) {
      case 1:
        currentMultiplusMode = ChargerOnly;
        if (GENERAL_SETTINGS_DEBUG_OUTPUT)
          Serial.println("Multiplus is in charger only mode");
        break;
      case 2:
        currentMultiplusMode = InverterOnly;
        if (GENERAL_SETTINGS_DEBUG_OUTPUT)
          Serial.println("Multiplus is in inveter only mode");
        break;
      case 3:
        currentMultiplusMode = On;
        if (GENERAL_SETTINGS_DEBUG_OUTPUT)
          Serial.println("Multiplus is in on");
        break;
      case 4:
        currentMultiplusMode = Off;
        if (GENERAL_SETTINGS_DEBUG_OUTPUT)
          Serial.println("Multiplus is off");
        break;
      default:
        currentMultiplusMode = Unknown;
        if (GENERAL_SETTINGS_DEBUG_OUTPUT)
          Serial.println("Unknown multiplus mode: " + String(workingMode));
        break;
    };
    lastMQTTUpdateReceived = millis();
  });

  msTimer.begin(100);

  KeepMQTTAlive(true);
}

void SetupWiFi() {
  if (GENERAL_SETTINGS_DEBUG_OUTPUT)
    Serial.println("Setting up Wi-Fi");

  // Enable debugging messages sent to serial output
  client.enableDebuggingMessages();

  // Enable Over The Air (OTA) updates
  client.enableOTA(SECRET_SETTINGS_OTA_PASSWORD, SECRET_SETTINGS_OTA_Port);
}

void SetupDisplay() {
  sprite.createSprite(TFT_WIDTH, TFT_HEIGHT);
  sprite.setSwapBytes(1);

  rm67162_init();

  SetupDisplayOrientation();
}

void ShowOpeningWindow() {

  if (GENERAL_SETTINGS_SHOW_SPLASH_SCREEN) {
    sprite.fillSprite(TFT_BLACK);

    sprite.loadFont(NotoSansBold36);
    sprite.setTextDatum(MC_DATUM);
    sprite.setTextColor(TFT_SKYBLUE, TFT_BLACK);
    sprite.drawString(programName, TFT_WIDTH / 2, TFT_HEIGHT / 2 - 50);
    sprite.loadFont(NotoSansBold24);
    sprite.drawString(programVersion, TFT_WIDTH / 2, TFT_HEIGHT / 2);
    sprite.loadFont(NotoSansBold15);
    sprite.drawString(programURL, TFT_WIDTH / 2, TFT_HEIGHT / 2 + 45);

    RefreshScreen();
    sprite.unloadFont();

    delay(5000);
  };
}

void setup() {

  if (GENERAL_SETTINGS_DEBUG_OUTPUT) {
    Serial.begin(GENERAL_SETTINGS_SERIAL_MONITOR_SPEED);
    Serial.println(programName + " " + programVersion);
    Serial.println(programURL);
  };

  TurnOffGreenLED();

  SetupTopAndBottomButtons();

  ResetGlobals();

  SetupDisplay();

  ShowOpeningWindow();

  SetupWiFi();

  if (GENERAL_SETTINGS_DEBUG_OUTPUT)
    Serial.println("setup complete");
}

void loop() {

  client.loop();

  KeepMQTTAlive();

  CheckButtons();

  UpdateScreen();

  ArduinoOTA.handle();
}
