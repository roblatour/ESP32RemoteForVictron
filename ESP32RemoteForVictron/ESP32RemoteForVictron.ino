// ESP32 Victron Monitor (version 1)
//
// Copyright Rob Latour, 2024
// License: MIT
// https://github.com/roblatour/ESP32RemoteForVictron
//
// version 1.1 - integrated a timer for automatically turning the display on/off at specified times
// version 1   - initial release
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
const String programVersion = "(Version 1.1)";
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

enum multiplusMode { ChargerOnly,
                     InverterOnly,
                     On,
                     Off,
                     Unknown
};
multiplusMode currentMultiplusMode = Unknown;

enum multiplusFunction { Charger,
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
  SECRET_SETTINGS_MQTT_Broker,
  SECRET_SETTINGS_MQTT_UserID,
  SECRET_SETTINGS_MQTT_Password,
  SECRET_SETTINGS_MQTT_ClientName,
  SECRET_SETTINGS_MQTT_Port);

unsigned long lastMQTTUpdateReceived = 0;

// JSON
#include <ArduinoJson.h>  // Ardiuno Library Manager, by Benoit Blanchon, https://arduinojson.org/?utm_source=meta&utm_medium=library.properties (v7.0.4)

// Non blocking delay library
#include "FireTimer.h"  // Ardiuno Library Manager, by PowerBroker2, https://github.com/PowerBroker2/FireTimer (v1.0.5)
FireTimer msTimer;

// Time stuff
#include <ESP32Time.h>
#include <TimeLib.h>   // version 1.6.1  https://www.arduino.cc/reference/en/libraries/time/
#include <Timezone.h>  // Include the Timezone library
#include <WiFi.h>
#include <time.h>

const char *primaryNTPServer = GENERAL_SETTINGS_PRIMARY_TIME_SERVER;
const char *secondaryNTPServer = GENERAL_SETTINGS_SECONDARY_TIME_SERVER;
const char *tertiaryNTPSever = GENERAL_SETTINGS_TERTIARY_TIME_SERVER;
const char *timeZone = GENERAL_SETTINGS_MY_TIME_ZONE;

bool turnOnDisplayAtSpecificTimesOnly = GENERAL_SETTINGS_TURN_ON_DISPAY_AT_SPECIFIC_TIMES_ONLY;
unsigned long nextTimeCheck = 0;
unsigned long keepTheDisplayOnUntilAtLeastThisTime = 0;

bool theDisplayIsCurrentlyOn;
int sleepHour, sleepMinute, wakeHour, wakeMinute;

// Debug
bool generalDebugOutput = false;
bool verboseDebugOutput = false;

void SetGreenLEDOn(bool turnOn) {

  const int greenLEDPin = 38;
  static bool doOnce = true;

  if (doOnce) {

    pinMode(greenLEDPin, OUTPUT);
    doOnce = false;
  };

  if (turnOn)
    digitalWrite(greenLEDPin, HIGH);
  else
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

void RefreshDisplay() {
  lcd_PushColors(0, 0, TFT_WIDTH, TFT_HEIGHT, (uint16_t *)sprite.getPointer());
}

void ChangeMultiplusMode(multiplusFunction option) {

  // if no choice is made within this timeout period then return to the previous screen without making any changes
  int numberOfMinutesUserHasToMakeAChoiceBeforeTimeOut = 1;

  unsigned long holdkeepTheDisplayOnUntilAtLeastThisTime = keepTheDisplayOnUntilAtLeastThisTime;

  KeepTheDisplayOnForThisManyMinutes(numberOfMinutesUserHasToMakeAChoiceBeforeTimeOut);

  // show the opening prompt

  sprite.fillSprite(TFT_BLACK);

  // this should not happen, but throw an error if the current Multiplus mode is unknown

  if (currentMultiplusMode == Unknown) {
    sprite.setTextDatum(MC_DATUM);
    sprite.setTextColor(TFT_RED, TFT_BLACK);
    sprite.loadFont(NotoSansBold24);
    sprite.drawString("Multiplus mode cannot be changed", TFT_WIDTH / 2, TFT_HEIGHT / 2);
    RefreshDisplay();
    sprite.unloadFont();
    delay(5000);

    // keep the display on for at least one minute more
    if ((millis() + 60 * 1000) > holdkeepTheDisplayOnUntilAtLeastThisTime)
      KeepTheDisplayOnForThisManyMinutes(1);
    else
      keepTheDisplayOnUntilAtLeastThisTime = holdkeepTheDisplayOnUntilAtLeastThisTime;

    return;
  };

  // show opening prompt

  sprite.loadFont(NotoSansBold36);
  sprite.setTextDatum(MC_DATUM);
  sprite.setTextColor(TFT_SKYBLUE, TFT_BLACK);

  if (option == Charger) {
    sprite.drawString("Set charger on or off?", TFT_WIDTH / 2, TFT_HEIGHT / 2);
    RefreshDisplay();
  } else {
    sprite.drawString("Set inverter on or off?", TFT_WIDTH / 2, TFT_HEIGHT / 2);
    RefreshDisplay();
  };

  // wait here for one second and also, if needed, for the user to release the button that they had previousily pressed to get here
  msTimer.begin(1000);

  // ensure whichever button was last pressed is now released
  while (digitalRead(topButton) == 0)
    msTimer.begin(50);

  while (digitalRead(bottomButton) == 0)
    msTimer.begin(50);

  if (millis() > keepTheDisplayOnUntilAtLeastThisTime) {

    if (generalDebugOutput)
      Serial.println("Timed out waiting for the user to release the button, no change will be applied");

    // keep the display on for at least one minute more
    if ((millis() + 60 * 1000) > holdkeepTheDisplayOnUntilAtLeastThisTime)
      KeepTheDisplayOnForThisManyMinutes(1);
    else
      keepTheDisplayOnUntilAtLeastThisTime = holdkeepTheDisplayOnUntilAtLeastThisTime;

    return;
  };

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

  RefreshDisplay();

  sprite.unloadFont();

  // wait until a choice is made

  while ((digitalRead(topButton) != 0) && (digitalRead(bottomButton) != 0) && (millis() < keepTheDisplayOnUntilAtLeastThisTime))
    msTimer.begin(100);

  if (millis() > keepTheDisplayOnUntilAtLeastThisTime) {

    if (generalDebugOutput)
      Serial.println("Timed out waiting for the user to make a choice, no change will be applied");

    KeepTheDisplayOnForThisManyMinutes(1);
    return;
  };

  bool userChoseOn;

  if (digitalRead(topButton) == 0) {
    userChoseOn = true;
    if (generalDebugOutput)
      Serial.println("Choice is 'ON'");
  } else {
    userChoseOn = false;
    if (generalDebugOutput)
      Serial.println("Choice is 'OFF'");
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

    if (generalDebugOutput)
      Serial.println("No change required to the multiplus mode");

  } else {

    String modeCodeValue;

    switch (desiredMultiplusMode) {

      case ChargerOnly:
        modeCodeValue = "1";
        if (generalDebugOutput)
          Serial.println("Set multiplus mode to charger only");
        break;
      case InverterOnly:
        modeCodeValue = "2";
        if (generalDebugOutput)
          Serial.println("Set multiplus mode to inverter only");
        break;
      case On:
        modeCodeValue = "3";
        if (generalDebugOutput)
          Serial.println("Set multiplus mode to on");
        break;
      case Off:
        modeCodeValue = "4";
        if (generalDebugOutput)
          Serial.println("Set multiplus mode to off");
        break;
    };

    // set the Multiplus's mode to Unknown while it changes over
    // it will be reset to its current mode in the next MQTT publishing cycle
    currentMultiplusMode = Unknown;

    // change the mode
    client.publish("W/" + VictronInstallationID + "/vebus/" + MultiplusThreeDigitID + "/Mode", "{\"value\": " + modeCodeValue + "}");
    delay(250);
  };

  // keep the display on for at least one minute more
  if ((millis() + 60 * 1000) > holdkeepTheDisplayOnUntilAtLeastThisTime)
    KeepTheDisplayOnForThisManyMinutes(1);
  else
    keepTheDisplayOnUntilAtLeastThisTime = holdkeepTheDisplayOnUntilAtLeastThisTime;
};

void CheckButtons() {

  if (theDisplayIsCurrentlyOn) {

    if (GENERAL_SETTINGS_ALLOW_CHANGING_INVERTER_AND_CHARGER_MODES) {

      // The top button is used to turn on/off the charger
      // The bottom button is used to turn on/off the inverter

      if (digitalRead(topButton) == 0)
        ChangeMultiplusMode(Charger);

      if (digitalRead(bottomButton) == 0)
        ChangeMultiplusMode(Inverter);
    };

  } else {

    if ((digitalRead(topButton) == 0) || (digitalRead(bottomButton) == 0)) {

      SetTheDisplayOn(true);

      // ensure whichever button was last pressed is now released
      while (digitalRead(topButton) == 0)
        msTimer.begin(50);

      while (digitalRead(bottomButton) == 0)
        msTimer.begin(50);

      KeepTheDisplayOnForThisManyMinutes(1);
    };
  };
}

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

void printLocalTime() {

  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    if (generalDebugOutput)
      Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S zone %Z %z ");
  } else {
    if (generalDebugOutput)
      Serial.println("Failed to obtain time 1");
  };
}

bool SetTime() {

  bool returnValue;

  if (generalDebugOutput)
    Serial.println("Setting time ... ");

  // configure NTP Server
  configTime(0, 0, primaryNTPServer, secondaryNTPServer, tertiaryNTPSever);

  // set the time zone
  setenv("TZ", timeZone, 1);
  tzset();

  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {

    time_t t = mktime(&timeinfo);
    struct timeval now = { .tv_sec = t };
    settimeofday(&now, NULL);
    if (generalDebugOutput)
      Serial.printf("Time set to: %s", asctime(&timeinfo));
    returnValue = true;

  } else {

    if (generalDebugOutput)
      Serial.println("Failed to obtain time from time server");
    returnValue = false;
  };

  if (generalDebugOutput)
    msTimer.begin(200);

  return returnValue;
}

void KeepTheDisplayOnForThisManyMinutes(int minutes) {

  keepTheDisplayOnUntilAtLeastThisTime = millis() + minutes * 60 * 1000;

  if (generalDebugOutput) {
    Serial.print("Keep display active for this many minutes: ");
    Serial.println(minutes);
  };
}


void RefreshTimeOnceADay(bool initialTimeSetRequest = false) {

  static bool pendingInitialTimeSetRequest = true;

  if (pendingInitialTimeSetRequest)
    if (!initialTimeSetRequest)
      return;

  if (millis() > nextTimeCheck) {

    if (SetTime())
      nextTimeCheck = millis() + 24 * 60 * 60 * 1000;  // update the time again in 24 hours
    else {
      int checkAgainInThisManyMinutes = 20;
      nextTimeCheck = millis() + checkAgainInThisManyMinutes * 60 * 1000;
      // keep the display on until the time can be successfully retrieved from the NTP server
      KeepTheDisplayOnForThisManyMinutes(checkAgainInThisManyMinutes + 1);
    };
  };

  pendingInitialTimeSetRequest = false;
};

bool ShouldTheDisplayBeOn() {

  static int timeToWake = wakeHour * 60 + wakeMinute;
  static int timeToSleep = sleepHour * 60 + sleepMinute;

  static int lastMinuteChecked = -1;
  static bool theDisplayHadBeenPreviousilyKeptOn = true;
  static bool lastReturnValue = true;

  bool returnValue = lastReturnValue;

  if (turnOnDisplayAtSpecificTimesOnly) {

    if (timeToWake == timeToSleep) {

      // the display should only be set to on if the current time is less than or equal to keepTheDisplayOnUntilAtLeastThisTime
      returnValue = (millis() <= keepTheDisplayOnUntilAtLeastThisTime);

    } else {

      if (millis() <= keepTheDisplayOnUntilAtLeastThisTime) {

        theDisplayHadBeenPreviousilyKeptOn = true;
        returnValue = true;

      } else {

        // Get the utc time
        time_t utc_now = time(nullptr);

        // the detailed time checking logic below is only performed:
        //     once a minute when the seconds first reach zero
        //     or
        //     when the keepTheDisplayOnUntilAtLeastThisTime had previousily been kept on but no longer needs to be given the passing of time
        //  otherwise
        //     return the previousily returned value

        int iSecond = second(utc_now);

        if ((iSecond == 0) || (theDisplayHadBeenPreviousilyKeptOn)) {

          theDisplayHadBeenPreviousilyKeptOn = false;

          // get the current local time
          tm *timeinfo = localtime(&utc_now);

          int iMinute = timeinfo->tm_min;

          if (iMinute == lastMinuteChecked)

            returnValue = lastReturnValue;

          else {

            lastMinuteChecked = iMinute;

            int iHour = timeinfo->tm_hour;

            if (timeToSleep > timeToWake) {

              // as an example: wake up at 8 am and go to sleep at 10 pm

              if ((iHour < wakeHour) || ((iHour == wakeHour) && (iMinute < wakeMinute)) || (iHour > sleepHour) || ((iHour == sleepHour) && (iMinute >= sleepMinute)))
                returnValue = false;
              else
                returnValue = true;

            } else {

              // as an example: wake up at 10 pm and go to sleep at 8 am

              if ((iHour < sleepHour) || ((iHour == sleepHour) && (iMinute < sleepMinute)) || (iHour > wakeHour) || ((iHour == wakeHour) && (iMinute >= wakeMinute)))
                returnValue = true;
              else
                returnValue = false;
            };
          };
        };
      };
    };

    lastReturnValue = returnValue;
  };

  return returnValue;
}

void UpdateDisplay() {

  static unsigned long nextDisplayUpdate = 0;

  // turn on or off the display as needed
  bool theDisplayShouldBeOn = ShouldTheDisplayBeOn();

  if (theDisplayIsCurrentlyOn && !theDisplayShouldBeOn)
    SetTheDisplayOn(false);

  if (!theDisplayIsCurrentlyOn && theDisplayShouldBeOn)
    SetTheDisplayOn(true);

  // only update the display at intervals set by the value in GENERAL_SETTINGS_SECONDS_BETWEEN_DISPLAY_UPDATES
  if (millis() < (nextDisplayUpdate + GENERAL_SETTINGS_SECONDS_BETWEEN_DISPLAY_UPDATES * 1000))
    return;

  // only update the display if it is on
  if (!theDisplayShouldBeOn)
    return;

  // if the connection is not yet established, or had been lost then display an appropriate message

  if (!client.isWifiConnected()) {

    sprite.fillSprite(TFT_BLACK);
    sprite.loadFont(NotoSansBold36);
    sprite.setTextDatum(MC_DATUM);
    sprite.setTextColor(TFT_SKYBLUE, TFT_BLACK);
    sprite.drawString("Awaiting Wi-Fi connection", TFT_WIDTH / 2, TFT_HEIGHT / 2);
    RefreshDisplay();
    sprite.unloadFont();
    return;
  };

  if (!client.isMqttConnected()) {

    sprite.fillSprite(TFT_BLACK);
    sprite.loadFont(NotoSansBold36);
    sprite.setTextDatum(MC_DATUM);
    sprite.setTextColor(TFT_SKYBLUE, TFT_BLACK);
    sprite.drawString("Awaiting MQTT connection", TFT_WIDTH / 2, TFT_HEIGHT / 2);
    RefreshDisplay();
    sprite.unloadFont();
    return;
  };

  if (millis() > (lastMQTTUpdateReceived + 3 * GENERAL_SETTINGS_SECONDS_BETWEEN_KEEP_ALIVE_REQUESTS * 1000)) {

    sprite.fillSprite(TFT_BLACK);
    sprite.loadFont(NotoSansBold24);
    sprite.setTextDatum(MC_DATUM);
    sprite.setTextColor(TFT_RED, TFT_BLACK);
    sprite.drawString("MQTT data updates have stopped", TFT_WIDTH / 2, TFT_HEIGHT / 2);
    RefreshDisplay();
    sprite.unloadFont();
    return;
  };

  // wait until data for all datapoints has been received prior to showing the display
  // while waiting display an appropriate message

  if (awaitingInitialTrasmissionOfAllDataPoints) {

    for (int i = 0; i < dataPoints; i++)
      if (awaitingDataToBeReceived[i]) {

        if (verboseDebugOutput)
          Serial.println("Awating data on data point " + String(i));

        sprite.fillSprite(TFT_BLACK);
        sprite.loadFont(NotoSansBold36);
        sprite.setTextDatum(MC_DATUM);
        sprite.setTextColor(TFT_SKYBLUE, TFT_BLACK);
        sprite.drawString("Awaiting data", TFT_WIDTH / 2, TFT_HEIGHT / 2);
        RefreshDisplay();
        sprite.unloadFont();

        return;
      };
    // if we have reached this point data for all datapoints has been received
    awaitingInitialTrasmissionOfAllDataPoints = false;
  };

  nextDisplayUpdate = millis() + GENERAL_SETTINGS_SECONDS_BETWEEN_DISPLAY_UPDATES * 1000;

  int x, y;

  // Tabula rasa

  sprite.fillSprite(TFT_BLACK);

  // show charger and inverter status

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

  RefreshDisplay();
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

    if (verboseDebugOutput)
      Serial.println("Keep alive request sent");

    msTimer.begin(100);
  };

  // time out check
  if (millis() > (nextUpdate + (GENERAL_SETTINGS_SECONDS_BETWEEN_KEEP_ALIVE_REQUESTS * 1000) * 3)) {
    ResetGlobals();
  };
}

void MassSubscribe() {

  // at this point we have the VictronInstallationID and the MultiplusThreeDigitID so let's get the rest of the data

  if (generalDebugOutput)
    Serial.println("Subscribing");

  String commonTopicStartString = "N/" + VictronInstallationID + "/system/0/";
  String multiplusModeTopicString = "N/" + VictronInstallationID + "/vebus/" + MultiplusThreeDigitID + "/Mode";

  // reset global variables so we will not start displaying information until all the subscribed data has been received
  ResetGlobals();

  // get the data

  // Grid (L1)

  if (GENERAL_SETTINGS_GRID_IN_L1_IS_USED) {

    client.subscribe(commonTopicStartString + "Ac/Grid/L1/Power", [](const String &payload) {
      if (awaitingDataToBeReceived[0])
        awaitingDataToBeReceived[0] = false;
      String response = String(payload);
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, response);
      gridInL1Watts = doc["value"].as<float>();
      if (verboseDebugOutput)
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
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, response);
      solarWatts = doc["value"].as<float>();
      if (verboseDebugOutput)
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
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, response);
    batterySOC = doc["value"].as<float>();
    if (verboseDebugOutput)
      Serial.println("batterySOC " + String(batterySOC));
    lastMQTTUpdateReceived = millis();
  });

  msTimer.begin(100);

  client.subscribe(commonTopicStartString + "Dc/Battery/TimeToGo", [](const String &payload) {
    if (awaitingDataToBeReceived[3])
      awaitingDataToBeReceived[3] = false;
    String response = String(payload);
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, response);
    batteryTTG = doc["value"].as<float>();
    if (verboseDebugOutput)
      Serial.println("batteryTTG " + String(batteryTTG));
    lastMQTTUpdateReceived = millis();
  });

  msTimer.begin(100);

  client.subscribe(commonTopicStartString + "Dc/Battery/Power", [](const String &payload) {
    if (awaitingDataToBeReceived[4])
      awaitingDataToBeReceived[4] = false;
    String response = String(payload);
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, response);
    batteryPower = doc["value"].as<float>();
    if (verboseDebugOutput)
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
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, response);
      ACOutL1Watts = doc["value"].as<float>();
      if (verboseDebugOutput)
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
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, response);
      ACOutL2Watts = doc["value"].as<float>();
      if (verboseDebugOutput)
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
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, response);
    int workingMode = doc["value"].as<int>();
    switch (workingMode) {
      case 1:
        currentMultiplusMode = ChargerOnly;
        if (verboseDebugOutput)
          Serial.println("Multiplus is in charger only mode");
        break;
      case 2:
        currentMultiplusMode = InverterOnly;
        if (verboseDebugOutput)
          Serial.println("Multiplus is in inverter only mode");
        break;
      case 3:
        currentMultiplusMode = On;
        if (verboseDebugOutput)
          Serial.println("Multiplus is in on");
        break;
      case 4:
        currentMultiplusMode = Off;
        if (verboseDebugOutput)
          Serial.println("Multiplus is off");
        break;
      default:
        currentMultiplusMode = Unknown;
        if (verboseDebugOutput)
          Serial.println("Unknown multiplus mode: " + String(workingMode));
        break;
    };
    lastMQTTUpdateReceived = millis();
  });

  msTimer.begin(100);

  KeepMQTTAlive(true);
}

void MassUnsubscribe() {

  if (generalDebugOutput)
    Serial.println("Unsubscribing");

  String commonTopicStartString = "N/" + VictronInstallationID + "/system/0/";
  String multiplusModeTopicString = "N/" + VictronInstallationID + "/vebus/" + MultiplusThreeDigitID + "/Mode";

  client.unsubscribe(commonTopicStartString + "Ac/Grid/L1/Power");
  client.unsubscribe(commonTopicStartString + "Dc/Pv/Power");
  client.unsubscribe(commonTopicStartString + "Dc/Battery/Soc");
  client.unsubscribe(commonTopicStartString + "Dc/Battery/TimeToGo");
  client.unsubscribe(commonTopicStartString + "Dc/Battery/Power");
  client.unsubscribe(commonTopicStartString + "Ac/Consumption/L1/Power");
  client.unsubscribe(commonTopicStartString + "Ac/Consumption/L2/Power");
  client.unsubscribe(multiplusModeTopicString);

  msTimer.begin(100);
}

void onConnectionEstablished() {

  if (VictronInstallationID == "+") {

    // Let's find the Victron Installation ID

    client.subscribe("N/+/system/0/Serial", [](const String &topic, const String &payload) {
      client.unsubscribe("N/+/system/0/Serial");
      String mytopic = String(topic);
      VictronInstallationID = mytopic.substring(2, 14);
      if (generalDebugOutput)
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
      MultiplusThreeDigitID = mytopic.substring(21, 24);
      if (generalDebugOutput)
        Serial.println("*** Discovered Multiplus three digit ID: " + MultiplusThreeDigitID);

      onConnectionEstablished();
      return;
    });

    // a keep alive request is required for Venus to publish the topic subscribed to above
    KeepMQTTAlive(true);

    return;
  };

  // at this point we have the VictronInstallationID and the MultiplusThreeDigitID so let's get the rest of the data

  MassSubscribe();
}

void onWiFiConnectionEstablished(WiFiEvent_t event, WiFiEventInfo_t info) {

  if (generalDebugOutput) {

    Serial.print("Wi-Fi connected to: ");
    Serial.println(String(SECRET_SETTINGS_WIFI_SSID));

    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  };

  RefreshTimeOnceADay(true);
}

void SetupWiFiAndMQTT() {

  if (generalDebugOutput)
    Serial.println("Setting up Wi-Fi and MQTT");

  // if GENERAL_SETTINGS_TURN_ON_DISPAY_AT_SPECIFIC_TIMES_ONLY is true
  // then enable the on-connection event in order that the time from an NTP server once the Wifi connection has been established
  // otherwise this is not needed
  if (GENERAL_SETTINGS_TURN_ON_DISPAY_AT_SPECIFIC_TIMES_ONLY)
    WiFi.onEvent(onWiFiConnectionEstablished, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);

  if (GENERAL_SETTINGS_ENABLE_OVER_THE_AIR_UPDATES)
    client.enableOTA(SECRET_SETTINGS_OTA_PASSWORD, SECRET_SETTINGS_OTA_Port);

  if (verboseDebugOutput)
    client.enableDebuggingMessages();
}

bool isNumeric(String str) {

  for (size_t i = 0; i < str.length(); i++) {
    if (!isdigit(str.charAt(i))) {
      return false;
    }
  }
  return true;
}

void SetTheDisplayOn(bool theDisplayShouldBeOn) {

  if (theDisplayIsCurrentlyOn && !theDisplayShouldBeOn) {

    // turn the display off

    // an AMOLED screen has no backlight as each pixel is an individual LED, so filling the display with black effectively turns it off causing it to use the least current possible
    sprite.fillSprite(TFT_BLACK);
    RefreshDisplay();

    // if the display is off there is no use receiving MQTT information, therefore unsubscribe to it so network traffic may be reduced
    MassUnsubscribe();

    theDisplayIsCurrentlyOn = false;

    if (generalDebugOutput)
      Serial.println("Display turned off");

  } else {

    if (!theDisplayIsCurrentlyOn && theDisplayShouldBeOn) {

      // turn the display on
      // effectively just re-enable updates to it

      // subscribe so that the data will be updated now that the display is back on
      MassSubscribe();

      theDisplayIsCurrentlyOn = true;

      if (generalDebugOutput)
        Serial.println("Display turned on");
    };
  };
}

void SetDisplayOrientation() {

  if (GENERAL_SETTINGS_USB_ON_THE_LEFT)
    lcd_setRotation(3);
  else
    lcd_setRotation(1);
};


bool parseTimeString(String timeString, int &hours, int &minutes) {

  if (timeString.length() != 5) {  // Check if the length is not equal to "HH:MM"
    return false;
  }

  if (timeString.charAt(2) != ':') {  // Check if the separator is at the correct position
    return false;
  }

  String hourStr = timeString.substring(0, 2);  // Extract hours substring
  String minuteStr = timeString.substring(3);   // Extract minutes substring

  if (!isNumeric(hourStr) || !isNumeric(minuteStr)) {  // Check if both substrings are numeric
    return false;
  }

  hours = hourStr.toInt();      // Convert hour string to integer
  minutes = minuteStr.toInt();  // Convert minute string to integer

  if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59) {  // Check if hours and minutes are within valid range
    return false;
  }

  return true;  // Time string successfully parsed
}

void SetDisplayOnAndOffTimes() {

  if (turnOnDisplayAtSpecificTimesOnly) {

    String sleepTime = String(GENERAL_SETTINGS_SLEEP_TIME);
    String wakeTime = String(GENERAL_SETTINGS_WAKE_TIME);

    if (parseTimeString(sleepTime, sleepHour, sleepMinute)) {

      if (parseTimeString(wakeTime, wakeHour, wakeMinute)) {

        int timeToWake = wakeHour * 60 + wakeMinute;
        int timeToSleep = sleepHour * 60 + sleepMinute;
        theDisplayIsCurrentlyOn = (timeToWake < timeToSleep);

        if (verboseDebugOutput) {
          if (timeToWake < timeToSleep) {
            Serial.print("Wake time set to:  ");
            Serial.println(wakeTime);
            Serial.print("Sleep time set to: ");
            Serial.println(sleepTime);
          } else {
            Serial.print("Sleep time set to: ");
            Serial.println(sleepTime);
            Serial.print("Wake time set to:  ");
            Serial.println(wakeTime);
          };
        };

      } else {
        turnOnDisplayAtSpecificTimesOnly = false;
        if (generalDebugOutput)
          Serial.println("Problem with GENERAL_SETTINGS_WAKE_TIME setting value, should be HH:MM");
      }
    } else {
      turnOnDisplayAtSpecificTimesOnly = false;
      if (generalDebugOutput)
        Serial.println("Problem with GENERAL_SETTINGS_SLEEP_TIME setting value, should be HH:MM");
    };
  } else {
    if (generalDebugOutput)
      Serial.println("Display set as always on");
  };
};

void SetupDisplay() {

  sprite.createSprite(TFT_WIDTH, TFT_HEIGHT);
  sprite.setSwapBytes(1);

  rm67162_init();

  SetDisplayOrientation();

  SetDisplayOnAndOffTimes();

  KeepTheDisplayOnForThisManyMinutes(1);

  SetTheDisplayOn(true);
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

    RefreshDisplay();
    sprite.unloadFont();

    delay(5000);
  };
}

void SetDebugLevel() {

  generalDebugOutput = (GENERAL_SETTINGS_DEBUG_OUTPUT_LEVEL > 0);
  verboseDebugOutput = (GENERAL_SETTINGS_DEBUG_OUTPUT_LEVEL > 1);

  if (generalDebugOutput) {
    Serial.begin(GENERAL_SETTINGS_SERIAL_MONITOR_SPEED);
  };
};


void setup() {

  SetDebugLevel();

  if (generalDebugOutput) {
    Serial.println(programName + " " + programVersion);
    Serial.println(programURL);
  };

  SetGreenLEDOn(false);

  SetupTopAndBottomButtons();

  SetupDisplay();

  ResetGlobals();

  SetupWiFiAndMQTT();

  ShowOpeningWindow();
}

void loop() {

  client.loop();

  KeepMQTTAlive();

  CheckButtons();

  UpdateDisplay();

  RefreshTimeOnceADay();

  ArduinoOTA.handle();
}
