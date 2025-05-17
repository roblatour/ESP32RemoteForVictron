// ESP32 Victron Monitor (version 1.9.7)
//
// Copyright Rob Latour, 2025
// License: MIT
// https://github.com/roblatour/ESP32RemoteForVictron
//
// version 1.9.7 - added option to show wattage coming into / going out of battery
// version 1.9.6 - corrected problem with displaying total AC Load; ShouldTheDisplayBeOn tweaked to ensure SleepTime is correctly calculated
// version 1.9.5 - adjusted calculation of solar watts and total grid watts when over 100kw to avoid displaying wrong results if updates not received when expected
// version 1.9.4 - Made the use of millis roll over safe
// version 1.9.3 - removed reference to timezone.h; refactoring of variable names; updated comments for SECRET_SETTINGS_MQTT_Broker
// version 1.9.2 - after further review removed unneeded millis() roll over logic;
//                 thanks to Nonstle for pointing this out at: https://github.com/roblatour/ESP32RemoteForVictron/issues/4
// version 1.9.1 - corrected for millis() roll over
// version 1.9   - corrected for screen refresh timings, deep sleep duration beyond 35 minutes, millis() roll over (which happens approximately every 49 days)
// version 1.8   - updated code for better memory management, included a few additional comments
// version 1.7   - updated to support both v1 and v2 of the Lily-go T-DISPLAY S3 AMOLED boards
// version 1.6   - updated to take charging state from Multiplus if in ESS mode
// version 1.5   - added an option to specify what is displayed under the battery percent charged; added an option to round numbers
// version 1.4   - added deep sleep option to save power when the screen doesn't need to be on, added option to allow another system to send the periodic keep alive requests
// version 1.3   - added options to show/hide charger and/or inverter status
// version 1.2   - added data gathering and reporting for Grid 2 input, Grid 3 input, and AC Load 3
// version 1.1   - integrated a timer for automatically turning the display on/off at specified times
// version 1     - initial release
//
// Design and tested with a Victron Multiplus II 12v system, monitored by a Raspberry Pi Zero 2 W running Victron Venus Firmware v3.3
//
// Compile and upload using Arduino IDE (2.3.2 or greater)
//
// Physical board:                  LILYGO T-Display-S3 AMOLED
// Arduino - Board Manager:         esp32 by Espressif Systems : version 2.0.11  (ref: https://forum.arduino.cc/t/esp32-s3-sudden-compile-error/1177237/24)
// Arduino Board selection:         ESP32S3 Dev Module
//
// Arduino - File - Preferences - Additional Board Manager URLs: https://dl.espressif.com/dl/package_esp32_index.json
//
// Arduino Tools settings:
// USB CDC On Boot:                 Enabled
// CPU Frequency:                   240MHz (WiFi)
// Core Debug Level:                None
// USB DFU On Boot:                 Enabled (Requires USB-OTG Mode)
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
const String programVersion = "(Version 1.9.7)";
const String programURL = "https://github.com/roblatour/ESP32RemoteForVictron";

RTC_DATA_ATTR bool initialStartupShowSplashScreen = true;

RTC_DATA_ATTR bool initialStartupLoadVictronInstallationAndMultiplusIDs = true;
RTC_DATA_ATTR char VictronInstallationIDArray[13];
RTC_DATA_ATTR char MultiplusThreeDigitIDArray[4];
RTC_DATA_ATTR char SolarChargerThreeDigitIDArray[4];
RTC_DATA_ATTR bool ESSIsBeingUsed = false;

String VictronInstallationID;
String MultiplusThreeDigitID;
String SolarChargerThreeDigitID;

const int dataPoints = 13;
bool awaitingDataToBeReceived[dataPoints];
bool awaitingInitialTransmissionOfAllDataPoints;

float gridInL1Watts = 0.0;
float gridInL2Watts = 0.0;
float gridInL3Watts = 0.0;

float solarWatts = 0.0;

float batterySOC = 0.0;
float batteryTTG = 0.0;
float batteryPower = 0.0;
float batteryTemperature = 0.0;
String chargingState = "Unknown";

float ACOutL1Watts = 0.0;
float ACOutL2Watts = 0.0;
float ACOutL3Watts = 0.0;

enum multiplusMode
{
  ChargerOnly,
  InverterOnly,
  On,
  Off,
  Unknown
};
multiplusMode currentMultiplusMode = Unknown;

enum multiplusFunction
{
  Charger,
  Inverter
};

int topButton, bottomButton;

// Display
#include <TFT_eSPI.h>             // download and use the entire TFT_eSPI https://github.com/Xinyuan-LilyGO/LilyGo-AMOLED-Series/tree/master/libdeps
#include "rm67162.h"              // included in the github package for this sketch, but also available from https://github.com/Xinyuan-LilyGO/T-Display-S3-AMOLED/tree/main/examples/factory
#include "fonts/NotoSansBold15.h" // included in the github package for this sketch, based on https://fonts.google.com/noto/specimen/Noto+Sans
#include "fonts/NotoSansBold24.h" // "
#include "fonts/NotoSansBold36.h" // "
#include "fonts/NotoSansBold72.h" // "

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite sprite = TFT_eSprite(&tft);

// MQTT
#include <EspMQTTClient.h> // https://github.com/plapointe6/EspMQTTClient (v1.13.3)
#include <string.h>

EspMQTTClient client(
    SECRET_SETTINGS_WIFI_SSID,
    SECRET_SETTINGS_WIFI_PASSWORD,
    SECRET_SETTINGS_MQTT_Broker,
    SECRET_SETTINGS_MQTT_UserID,
    SECRET_SETTINGS_MQTT_Password,
    SECRET_SETTINGS_MQTT_ClientName,
    SECRET_SETTINGS_MQTT_Port);

unsigned long lastMQTTUpdateReceived = 0UL;

// JSON
#include <ArduinoJson.h> // Ardiuno Library Manager, by Benoit Blanchon, https://arduinojson.org/?utm_source=meta&utm_medium=library.properties (v7.4.1)

// Non blocking delay library
#include "FireTimer.h" // Ardiuno Library Manager, by PowerBroker2, https://github.com/PowerBroker2/FireTimer (v1.0.5)
FireTimer msTimer;

// Time stuff
#include <ESP32Time.h>
#include <TimeLib.h> // version 1.6.1  https://www.arduino.cc/reference/en/libraries/time/
#include <WiFi.h>
#include <time.h>

const char *primaryNTPServer = GENERAL_SETTINGS_PRIMARY_TIME_SERVER;
const char *secondaryNTPServer = GENERAL_SETTINGS_SECONDARY_TIME_SERVER;
const char *tertiaryNTPSever = GENERAL_SETTINGS_TERTIARY_TIME_SERVER;
const char *timeZone = GENERAL_SETTINGS_MY_TIME_ZONE;

bool turnOnDisplayAtSpecificTimesOnly = GENERAL_SETTINGS_TURN_ON_DISPLAY_AT_SPECIFIC_TIMES_ONLY;
unsigned long keepDisplayOnStartTime = 0UL;
unsigned long keepDisplayOnTimeOut = 0UL;

// report a timeout after this many seconds of no data being received
const int timeOutInSeconds = 61;
const int timeOutInMilliSeconds = timeOutInSeconds * 1000;

bool theDisplayIsCurrentlyOn;
int sleepHour, sleepMinute, wakeHour, wakeMinute;

// Debug
bool generalDebugOutput = false;
bool verboseDebugOutput = false;

void SetGreenLEDOff()
{

  // With the AMOLED v1 board pin 38 should be set to LOW to turn off the green LED

  const int greenLEDPin = 38;
  pinMode(greenLEDPin, OUTPUT);
  digitalWrite(greenLEDPin, LOW);
}

void SetDisplayOn()
{

  // On the AMOLED v2 board pin 38 must be set to HIGH for the display to work
  const int DisplayPin = 38;
  pinMode(DisplayPin, OUTPUT);
  digitalWrite(DisplayPin, HIGH);
}

void SetupTopAndBottomButtons()
{

  // Notes:
  // This sketch was built for a LILYGO T-Display-S3 AMOLED and as such:
  // 1. The two buttons on the device have built in hardware debounce protection, so debounce logic is not needed in this sketch
  // 2. The postion of the 'top' and 'bottom' buttons are relative to the side on which the USB cable is plugged into, so the code below adjusts for that

  // enable buttons
  pinMode(topButtonIfUSBIsOnTheLeft, INPUT);
  pinMode(bottomButtonIfUSBIsOnTheLeft, INPUT);

  if (GENERAL_SETTINGS_USB_ON_THE_LEFT)
  {
    topButton = bottomButtonIfUSBIsOnTheLeft;
    bottomButton = topButtonIfUSBIsOnTheLeft;
  }
  else
  {
    topButton = topButtonIfUSBIsOnTheLeft;
    bottomButton = bottomButtonIfUSBIsOnTheLeft;
  };
}

void RefreshDisplay()
{
  lcd_PushColors(0, 0, TFT_WIDTH, TFT_HEIGHT, (uint16_t *)sprite.getPointer());
}

void ChangeMultiplusMode(multiplusFunction option)
{

  // if no choice is made within this timeout period then return to the previous screen without making any changes
  int numberOfMinutesUserHasToMakeAChoiceBeforeTimeOut = 1;

  SetKeepDisplayOnTimeOut(numberOfMinutesUserHasToMakeAChoiceBeforeTimeOut);

  // show the opening prompt

  sprite.fillSprite(TFT_BLACK);

  // this should not happen, but throw an error if the current Multiplus mode is unknown

  if (currentMultiplusMode == Unknown)
  {
    sprite.setTextDatum(MC_DATUM);
    sprite.setTextColor(TFT_RED, TFT_BLACK);
    sprite.loadFont(NotoSansBold24);
    sprite.drawString("Multiplus mode cannot be changed", TFT_WIDTH / 2, TFT_HEIGHT / 2);
    RefreshDisplay();
    sprite.unloadFont();
    delay(5000);

    // keep the display on for one minute
    SetKeepDisplayOnTimeOut(1);

    return;
  };

  // show opening prompt

  sprite.loadFont(NotoSansBold36);
  sprite.setTextDatum(MC_DATUM);
  sprite.setTextColor(TFT_SKYBLUE, TFT_BLACK);

  if (option == Charger)
  {
    sprite.drawString("Set charger on or off?", TFT_WIDTH / 2, TFT_HEIGHT / 2);
    RefreshDisplay();
  }
  else
  {
    sprite.drawString("Set inverter on or off?", TFT_WIDTH / 2, TFT_HEIGHT / 2);
    RefreshDisplay();
  };

  // wait here for one second and also, if needed, for the user to release the button that they had previously pressed to get here
  msTimer.begin(1000);

  // ensure whichever button was last pressed is now released
  while (digitalRead(topButton) == 0)
    msTimer.begin(50);

  while (digitalRead(bottomButton) == 0)
    msTimer.begin(50);

  if (IsKeepDisplayOnTimedOut())
  {

    if (generalDebugOutput)
      Serial.println("Timed out waiting for the user to release the button, no change will be applied");

    // keep the display on for one minute
    SetKeepDisplayOnTimeOut(1);

    return;
  };

  // prompt for desired state

  int xPosition;
  if (GENERAL_SETTINGS_USB_ON_THE_LEFT)
  {
    xPosition = 0;
    sprite.setTextDatum(TL_DATUM);
  }
  else
  {
    xPosition = TFT_WIDTH;
    sprite.setTextDatum(TR_DATUM);
  };

  if (option == Charger)
  {
    if ((currentMultiplusMode == ChargerOnly) || (currentMultiplusMode == On))
    {
      sprite.drawString("ON (current mode)", xPosition, 0);
      sprite.drawString("OFF", xPosition, TFT_HEIGHT - 30);
    }
    else
    {
      sprite.drawString("ON", xPosition, 0);
      sprite.drawString("OFF (current mode)", xPosition, TFT_HEIGHT - 30);
    }
  }
  else
  {
    if ((currentMultiplusMode == InverterOnly) || (currentMultiplusMode == On))
    {
      sprite.drawString("ON (current mode)", xPosition, 0);
      sprite.drawString("OFF", xPosition, TFT_HEIGHT - 30);
    }
    else
    {
      sprite.drawString("ON", xPosition, 0);
      sprite.drawString("OFF (current mode)", xPosition, TFT_HEIGHT - 30);
    };
  };

  RefreshDisplay();

  sprite.unloadFont();

  // wait until a choice is made

  while ((digitalRead(topButton) != 0) && (digitalRead(bottomButton) != 0) && !IsKeepDisplayOnTimedOut())
    msTimer.begin(100);

  if (IsKeepDisplayOnTimedOut())
  {

    if (generalDebugOutput)
      Serial.println("Timed out waiting for the user to make a choice, no change will be applied");

    SetKeepDisplayOnTimeOut(1);
    return;
  };

  bool userChoseOn;

  if (digitalRead(topButton) == 0)
  {
    userChoseOn = true;
    if (generalDebugOutput)
      Serial.println("Choice is 'ON'");
  }
  else
  {
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

  if (option == Charger)
  {

    if (userChoseOn)
    {

      switch (currentMultiplusMode)
      {
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
    }
    else
    {
      switch (currentMultiplusMode)
      {
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

  if (option == Inverter)
  {

    if (userChoseOn)
    {

      switch (currentMultiplusMode)
      {
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
    }
    else
    {
      switch (currentMultiplusMode)
      {
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

  if (currentMultiplusMode == desiredMultiplusMode)
  {

    if (generalDebugOutput)
      Serial.println("No change required to the multiplus mode");
  }
  else
  {

    String modeCodeValue;

    switch (desiredMultiplusMode)
    {

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

  // keep the display on for one minute
  SetKeepDisplayOnTimeOut(1);
};

void CheckButtons()
{

  if (theDisplayIsCurrentlyOn)
  {

    if (GENERAL_SETTINGS_ALLOW_CHANGING_INVERTER_AND_CHARGER_MODES)
    {

      // The top button is used to turn on/off the charger
      // The bottom button is used to turn on/off the inverter

      if (digitalRead(topButton) == 0)
        ChangeMultiplusMode(Charger);

      if (digitalRead(bottomButton) == 0)
        ChangeMultiplusMode(Inverter);
    };
  }
  else
  {

    if ((digitalRead(topButton) == 0) || (digitalRead(bottomButton) == 0))
    {

      SetTheDisplayOn(true);

      // ensure whichever button was last pressed is now released
      while (digitalRead(topButton) == 0)
        msTimer.begin(50);

      while (digitalRead(bottomButton) == 0)
        msTimer.begin(50);

      SetKeepDisplayOnTimeOut(1);
    };
  };
}

String ConvertSecondsToDayHoursMinutes(int n)
{

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
  // return (sDays + "D " + sHours + "H " + sMinutes + "M");
}

float roundFloat(float value, int decimals)
{
  float multiplier = pow(10, decimals);
  return round(value * multiplier) / multiplier;
}

String ConvertToStringWithAFixedNumberOfDecimalPlaces(float f, int numberOfDecimalPlaces = 1)
{

  // if round = true then round to the specified number of decimal places, otherwise truncate to the specified number of decimal places

  char workingArray[20 + numberOfDecimalPlaces];
  char formattedArray[20 + numberOfDecimalPlaces];

  if (GENERAL_SETTINGS_ROUND_NUMBERS)
    f = roundFloat(f, numberOfDecimalPlaces);

  dtostrf(f, 15 + numberOfDecimalPlaces, numberOfDecimalPlaces, formattedArray);
  sprintf(workingArray, "%s", formattedArray);

  String returnValue = String(workingArray);

  returnValue.trim();

  return returnValue;
}

void printLocalTime()
{

  struct tm timeinfo;
  if (getLocalTime(&timeinfo))
  {
    if (generalDebugOutput)
      Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S zone %Z %z ");
  }
  else
  {
    if (generalDebugOutput)
      Serial.println("Failed to obtain time 1");
  };
}

bool SetTime()
{

  // Configure NTP Server
  configTime(0, 0, primaryNTPServer, secondaryNTPServer, tertiaryNTPSever);

  // Set the time zone
  if (setenv("TZ", timeZone, 1) != 0)
  {
    if (generalDebugOutput)
      Serial.println("Error setting time zone");
    return false;
  };

  tzset();

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    if (generalDebugOutput)
      Serial.println("Failed to obtain time from time server");
    return false;
  };

  time_t t = mktime(&timeinfo);
  if (t == -1)
  {
    if (generalDebugOutput)
      Serial.println("Error converting time to time_t");
    return false;
  };

  struct timeval now = {.tv_sec = t};
  if (settimeofday(&now, NULL) != 0)
  {
    if (generalDebugOutput)
      Serial.println("Error setting system time");
    return false;
  };

  if (generalDebugOutput)
  {
    if (generalDebugOutput)
      Serial.printf("Time set to: %s", asctime(&timeinfo));
  };

  if (generalDebugOutput)
  {
    // wait for console updates to complete
    const int timerInterval = 200;
    msTimer.begin(timerInterval);
  };

  return true;
}

void SetKeepDisplayOnTimeOut(unsigned int minutes)
{
  // Set the timeout after which IsKeepDisplayOnTimedOut() will become true
  keepDisplayOnTimeOut = (unsigned long)minutes * 60UL * 1000UL;
  ResetKeepDisplayOnStartTime();

  if (generalDebugOutput)
  {
    Serial.print("Keep display active for this many minutes: ");
    Serial.println(minutes);
  };
}

void ResetKeepDisplayOnStartTime()
{
  // Reset the start time of the display timeout to now
  keepDisplayOnStartTime = millis();
}

bool IsKeepDisplayOnTimedOut()
{
  // Returns true if the display timeout time has passed and false when not.
  if (millis() - keepDisplayOnStartTime >= keepDisplayOnTimeOut)
  {
    return true;
  }
  return false;
}

void RefreshTimeOnceADay(bool forceTimeSet = false)
{

  const int RETRY_INTERVAL_IN_MINUTES = 20;
  const unsigned long RETRY_INTERVAL_IN_MILLIS = RETRY_INTERVAL_IN_MINUTES * 60UL * 1000UL;
  const unsigned long ONE_DAY_IN_MILLIS = 24UL * 60UL * 60UL * 1000UL;
  static unsigned long lastTimeCheck = 0UL;
  static unsigned long checkInterval = 0UL;

  if (forceTimeSet || (millis() - lastTimeCheck >= checkInterval))
  {
    lastTimeCheck = millis();
    if (SetTime())
    {
      checkInterval = ONE_DAY_IN_MILLIS;
    }
    else
    {
      checkInterval = RETRY_INTERVAL_IN_MILLIS;
      // keep the display on until the time can be successfully set from the NTP server
      SetKeepDisplayOnTimeOut(RETRY_INTERVAL_IN_MINUTES + 1);
    };
  };
};

bool ShouldTheDisplayBeOn()
{

  int timeToWake = wakeHour * 60 + wakeMinute;
  int timeToSleep = sleepHour * 60 + sleepMinute;

  static int lastMinuteChecked = -1;
  static bool theDisplayHadBeenPreviouslyKeptOn = true;
  static bool lastReturnValue = true;

  bool returnValue = lastReturnValue;

  if (turnOnDisplayAtSpecificTimesOnly)
  {

    if (timeToWake == timeToSleep)
    {

      // the display should only be set to on if the KeepDisplayOnTimeOut has not happened yet
      returnValue = IsKeepDisplayOnTimedOut();
    }
    else
    {

      if (!IsKeepDisplayOnTimedOut())
      {

        theDisplayHadBeenPreviouslyKeptOn = true;
        returnValue = true;
      }
      else
      {

        // Get the utc time
        time_t utc_now = time(nullptr);

        // the detailed time checking logic below is only performed:
        //     once a minute when the seconds first reach zero
        //     or
        //     when the keepTheDisplayTimeOut had previously been kept on but no longer needs to be given the passing of time
        //  otherwise
        //     return the previously returned value

        int iSecond = second(utc_now);

        if ((iSecond == 0) || (theDisplayHadBeenPreviouslyKeptOn))
        {

          theDisplayHadBeenPreviouslyKeptOn = false;

          // get the current local time
          tm *timeinfo = localtime(&utc_now);

          int iMinute = timeinfo->tm_min;

          if (iMinute == lastMinuteChecked)

            returnValue = lastReturnValue;

          else
          {

            lastMinuteChecked = iMinute;

            int iHour = timeinfo->tm_hour;

            if (timeToSleep > timeToWake)
            {

              // as an example: wake up at 8 am and go to sleep at 10 pm

              if ((iHour < wakeHour) || ((iHour == wakeHour) && (iMinute < wakeMinute)) || (iHour > sleepHour) || ((iHour == sleepHour) && (iMinute >= sleepMinute)))
                returnValue = false;
              else
                returnValue = true;
            }
            else
            {

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

void UpdateDisplay()
{

  static unsigned long lastDisplayUpdate = 0UL;
  static bool tryToRestoreConnection = true;
  static bool MQTTTransmissionLost = false;

  // turn on or off the display as needed
  bool theDisplayShouldBeOn = ShouldTheDisplayBeOn();

  if (theDisplayIsCurrentlyOn && !theDisplayShouldBeOn)
    SetTheDisplayOn(false);

  if (!theDisplayIsCurrentlyOn && theDisplayShouldBeOn)
    SetTheDisplayOn(true);

  // only update the display when its time has come
  if (millis() - lastDisplayUpdate < ((unsigned long)GENERAL_SETTINGS_SECONDS_BETWEEN_DISPLAY_UPDATES * 1000UL))
    return;

  // only update the display if it is on
  if (!theDisplayShouldBeOn)
    return;

  // if the connection is not yet established, or had been lost then display an appropriate message

  if (!client.isWifiConnected())
  {

    sprite.fillSprite(TFT_BLACK);
    sprite.loadFont(NotoSansBold36);
    sprite.setTextDatum(MC_DATUM);
    sprite.setTextColor(TFT_SKYBLUE, TFT_BLACK);
    sprite.drawString("Awaiting Wi-Fi connection", TFT_WIDTH / 2, TFT_HEIGHT / 2);
    RefreshDisplay();
    sprite.unloadFont();
    return;
  };

  if (!client.isMqttConnected())
  {

    sprite.fillSprite(TFT_BLACK);
    sprite.loadFont(NotoSansBold36);
    sprite.setTextDatum(MC_DATUM);
    sprite.setTextColor(TFT_SKYBLUE, TFT_BLACK);
    sprite.drawString("Awaiting MQTT connection", TFT_WIDTH / 2, TFT_HEIGHT / 2);
    RefreshDisplay();
    sprite.unloadFont();
    return;
  };

  // deal with the case that no data has arrived beyond the timeout period
  // see the notes in the general_settings.h file for more information

  if (millis() - lastMQTTUpdateReceived >= timeOutInMilliSeconds)
  {

    sprite.fillSprite(TFT_BLACK);
    sprite.loadFont(NotoSansBold24);
    sprite.setTextDatum(MC_DATUM);
    sprite.setTextColor(TFT_RED, TFT_BLACK);
    sprite.drawString("MQTT data updates have stopped", TFT_WIDTH / 2, TFT_HEIGHT / 2);
    RefreshDisplay();

    sprite.unloadFont();

    if (!GENERAL_SETTINGS_SEND_PERIODICAL_KEEP_ALIVE_REQUESTS)
    {
      // another system is responsible for sending the keep alive requests
      // keep the message "MQTT data updates have stopped" on the screen for a brief period
      // note: although the code below only delays for 1 second, the message will stay on the screen
      // for longer than that while attempts (below) are made to reconnect
      delay(1000);
    };

    if (tryToRestoreConnection)
    {

      // only try to restore the connection once
      tryToRestoreConnection = false;

      if (generalDebugOutput)
        Serial.println("MQTT data updates have stopped");

      ResetGlobals();

      if (!GENERAL_SETTINGS_SEND_PERIODICAL_KEEP_ALIVE_REQUESTS)
      {

        // if GENERAL_SETTINGS_SEND_PERIODICAL_KEEP_ALIVE_REQUESTS is false it means that this program was counting on another system to send
        // the keep alive request.  However, as this does not seem to be happening at the moment, the program will try resubscribing and sending
        // a keep alive request itself (which is part of the mass subscribe process) to temporarily get things going again

        MQTTTransmissionLost = true;

        if (generalDebugOutput)
          Serial.println("Attempting to restore MQTT data updates");

        MassSubscribe();
      };
    };
    return;
  };

  tryToRestoreConnection = true;

  // wait until data for all data points have been received prior to showing the display
  // while waiting display an appropriate message

  if (awaitingInitialTransmissionOfAllDataPoints)
  {

    for (int i = 0; i < dataPoints; i++)
      if (awaitingDataToBeReceived[i])
      {

        if (verboseDebugOutput)
          Serial.println("Awaiting data on data point " + String(i));

        sprite.fillSprite(TFT_BLACK);
        sprite.loadFont(NotoSansBold36);
        sprite.setTextDatum(MC_DATUM);
        sprite.setTextColor(TFT_SKYBLUE, TFT_BLACK);
        sprite.drawString("Awaiting data", TFT_WIDTH / 2, TFT_HEIGHT / 2);
        RefreshDisplay();
        sprite.unloadFont();

        return;
      };
    // if we have reached this point data for all data points have been received
    awaitingInitialTransmissionOfAllDataPoints = false;

    if (MQTTTransmissionLost)
    {
      MQTTTransmissionLost = false;
      if (generalDebugOutput)
        Serial.println("MQTT data updates restored");
    };
  };

  lastDisplayUpdate = millis();

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

  String inverterStatus;
  if (currentMultiplusMode == Unknown)
    inverterStatus = "?";
  else if ((currentMultiplusMode == On) || (currentMultiplusMode == InverterOnly))
    inverterStatus = "on";
  else
    inverterStatus = "off";

  if (GENERAL_SETTINGS_USB_ON_THE_LEFT)
    x = 0;
  else
    x = TFT_WIDTH;

  if (GENERAL_SETTINGS_SHOW_CHARGER_MODE)
  {
    y = 5;
    sprite.drawString("Charger " + chargerStatus, x, y);
  };

  if (GENERAL_SETTINGS_SHOW_INVERTER_MODE)
  {
    y = TFT_HEIGHT - 30;
    sprite.drawString("Inverter " + inverterStatus, x, y);
  };

  sprite.unloadFont();

  // show solar info

  sprite.loadFont(NotoSansBold24);
  sprite.setTextColor(TFT_YELLOW, TFT_BLACK);

  y = TFT_HEIGHT / 2 - 36;
  sprite.drawString("Solar", x, y);

  sprite.loadFont(NotoSansBold36);
  solarWatts = int(solarWatts);

  y = TFT_HEIGHT / 2 + 4;
  if (GENERAL_SETTINGS_IF_OVER_1000_WATTS_REPORT_KW && (solarWatts >= 1000.0F))
  {
    float adjustedSolarWatts = solarWatts / 1000.0F;
    sprite.drawString(ConvertToStringWithAFixedNumberOfDecimalPlaces(adjustedSolarWatts, GENERAL_SETTINGS_NUMBER_DECIMAL_PLACES_FOR_KW_REPORTING) + " KW", x, y);
  }
  else
  {
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

  float totalGridWatts = int(gridInL1Watts) + int(gridInL2Watts) + int(gridInL3Watts);

  y = 43;
  if (GENERAL_SETTINGS_IF_OVER_1000_WATTS_REPORT_KW && (totalGridWatts >= 1000.0F))
  {
    float adjustedTotalGridWatts = totalGridWatts / 1000.0F;
    sprite.drawString(ConvertToStringWithAFixedNumberOfDecimalPlaces(adjustedTotalGridWatts, GENERAL_SETTINGS_NUMBER_DECIMAL_PLACES_FOR_KW_REPORTING) + " KW", x, y);
  }
  else
  {
    totalGridWatts = int(totalGridWatts);
    sprite.drawString(String(int(totalGridWatts)) + " W", x, y);
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
  float totalACConsumptionWatts = int(ACOutL1Watts + ACOutL2Watts + ACOutL3Watts);
  if (GENERAL_SETTINGS_IF_OVER_1000_WATTS_REPORT_KW && (totalACConsumptionWatts >= 1000.0F))
  {
    totalACConsumptionWatts = totalACConsumptionWatts / 1000.0F;
    sprite.drawString(ConvertToStringWithAFixedNumberOfDecimalPlaces(totalACConsumptionWatts, GENERAL_SETTINGS_NUMBER_DECIMAL_PLACES_FOR_KW_REPORTING) + " KW", x, y);
  }
  else
  {
    totalACConsumptionWatts = int(totalACConsumptionWatts);
    sprite.drawString(String(int(totalACConsumptionWatts)) + " W", x, y);
  };

  sprite.unloadFont();

  // show battery info

  int midX, midY, outerRadius, innerRadius, startAngle, endAngle;
  unsigned short batteryColour;

  midX = TFT_WIDTH / 2;
  midY = TFT_HEIGHT / 2;

  if (midX < midY)
    outerRadius = midX;
  else
    outerRadius = midY;

  innerRadius = outerRadius - 8;

  startAngle = 180;
  endAngle = int(batterySOC * 3.6 + 180) % 360;

  if (batterySOC <= GENERAL_SETTINGS_SHOW_BATTERY_AS_RED)
  {
    batteryColour = TFT_RED;
  }
  else if (batterySOC <= GENERAL_SETTINGS_SHOW_BATTERY_AS_YELLOW)
  {
    batteryColour = TFT_YELLOW;
  }
  else
  {
    batteryColour = TFT_GREEN;
  };

  sprite.drawSmoothArc(midX, midY, outerRadius, innerRadius, startAngle, endAngle, batteryColour, TFT_BLACK);

  sprite.loadFont(NotoSansBold72);
  sprite.setTextDatum(MC_DATUM);
  sprite.setTextColor(batteryColour, TFT_BLACK);

  // show battery percent without a decimal place

  int ibatterySOC = ConvertToStringWithAFixedNumberOfDecimalPlaces(batterySOC, 0).toInt();
  sprite.drawString(String(ibatterySOC) + "%", midX, midY);

  sprite.unloadFont();

  sprite.loadFont(NotoSansBold24);
  sprite.drawString("Battery", midX, midY - 60);

  if ((GENERAL_SETTINGS_ADDITIONAL_INFO == 1) && (batteryTTG != 0))
  {

    // show time to go
    String TimeToGo = ConvertSecondsToDayHoursMinutes(int(batteryTTG));
    sprite.drawString(TimeToGo, midX, midY + 50);
  }
  else if (GENERAL_SETTINGS_ADDITIONAL_INFO == 2)
  {

    // show charger state
    sprite.drawString(chargingState, midX, midY + 50);
  }
  else if (GENERAL_SETTINGS_ADDITIONAL_INFO == 3)
  {

    // show battery temperature (with one decimal place)

    String temperatureString = ConvertToStringWithAFixedNumberOfDecimalPlaces(batteryTemperature, 1) + String("  ");

    sprite.drawString(temperatureString, midX, midY + 50);

    // add the degree symbol
    sprite.unloadFont();
    sprite.loadFont(NotoSansBold15);
    int degreePosx = sprite.textWidth(temperatureString) / 2 + 4;
    sprite.drawString(String("o"), midX + degreePosx, midY + 43);
  }  
  else if (GENERAL_SETTINGS_ADDITIONAL_INFO == 4) 
  {

    //show battery power
    if (GENERAL_SETTINGS_IF_OVER_1000_WATTS_REPORT_KW && ((batteryPower >= 1000.0F) || (batteryPower <= -1000.0F))) {
      sprite.drawString(ConvertToStringWithAFixedNumberOfDecimalPlaces(batteryPower / 1000.0F, GENERAL_SETTINGS_NUMBER_DECIMAL_PLACES_FOR_KW_REPORTING) + " kW", midX, midY + 50);
    } else {
      sprite.drawString(String(int(batteryPower)) + " W", midX, midY + 50);
    };
  }

  sprite.unloadFont();

  // Draw an upward triangle if the battery is charging or a downward triangle if it is discharging
  // However, if it is neither charging or discharging then do not draw any triangle at all

  if (batteryPower > 0.0F)
  {

    // Draw a upward triangle
    int16_t centerX = TFT_WIDTH / 2;
    int16_t centerY = TFT_HEIGHT / 2 + 88;

    int16_t arrowWidth = 20;
    int16_t arrowHeight = 25;

    sprite.fillTriangle(centerX - arrowWidth / 2, centerY + arrowHeight / 2,
                        centerX + arrowWidth / 2, centerY + arrowHeight / 2,
                        centerX, centerY - arrowHeight / 2, batteryColour);
  }
  else
  {

    if (batteryPower < 0.0F)
    {

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

  // used for testing only - display the date and time
  //
  // sprite.loadFont(NotoSansBold24);
  // sprite.setTextColor(TFT_RED, TFT_BLACK);
  //
  // if (GENERAL_SETTINGS_USB_ON_THE_LEFT) {
  //  sprite.setTextDatum(TL_DATUM);
  //} else {
  //  sprite.setTextDatum(TR_DATUM);
  //};
  //
  // struct tm timeinfo;
  // getLocalTime(&timeinfo);
  // String currentTimeString = (String)asctime(&timeinfo) + " ";
  //
  // sprite.drawString(currentTimeString, 0, 50);
  // sprite.unloadFont();
  //
  // end of testing block

  RefreshDisplay();
}

void ResetGlobals()
{

  if (initialStartupLoadVictronInstallationAndMultiplusIDs)
  {

    initialStartupLoadVictronInstallationAndMultiplusIDs = false;

    String(SECRET_SETTING_VICTRON_INSTALLATION_ID).toCharArray(VictronInstallationIDArray, String(SECRET_SETTING_VICTRON_INSTALLATION_ID).length() + 1);
    String(SECRET_SETTING_VICTRON_MULTIPLUS_ID).toCharArray(MultiplusThreeDigitIDArray, String(SECRET_SETTING_VICTRON_MULTIPLUS_ID).length() + 1);
    String(SECRET_SETTING_VICTRON_SOLAR_CHARGER_ID).toCharArray(SolarChargerThreeDigitIDArray, String(SECRET_SETTING_VICTRON_SOLAR_CHARGER_ID).length() + 1);
  };

  VictronInstallationID = String(VictronInstallationIDArray);
  MultiplusThreeDigitID = String(MultiplusThreeDigitIDArray);
  SolarChargerThreeDigitID = String(SolarChargerThreeDigitIDArray);

  awaitingInitialTransmissionOfAllDataPoints = true;
  for (int i = 0; i < dataPoints; i++)
    awaitingDataToBeReceived[i] = true;

  gridInL1Watts = 0.0;
  gridInL2Watts = 0.0;
  gridInL3Watts = 0.0;

  solarWatts = 0.0;

  batterySOC = 0.0;
  batteryTTG = 0.0;
  batteryPower = 0.0;

  ACOutL1Watts = 0.0;
  ACOutL2Watts = 0.0;
  ACOutL3Watts = 0.0;

  currentMultiplusMode = Unknown;

  chargingState = "";
};

void KeepMQTTAlive(bool forceKeepAliveRequestNow = false)
{

  static unsigned long lastMqttUpdate = 0UL;

  if ((forceKeepAliveRequestNow) || (GENERAL_SETTINGS_SEND_PERIODICAL_KEEP_ALIVE_REQUESTS))
  {

    if ((forceKeepAliveRequestNow) || (millis() - lastMqttUpdate >= GENERAL_SETTINGS_SEND_PERIODICAL_KEEP_ALIVE_REQUESTS_INTERVAL))
    {

      lastMqttUpdate = millis();

      client.publish("R/" + VictronInstallationID + "/keepalive", "");

      if (verboseDebugOutput)
        Serial.println("Keep alive request sent");

      msTimer.begin(100);
    };
  };
}

void SubscribeToGetChargingStateFromMultiplus()
{

  String commonTopic = "N/" + VictronInstallationID;
  String ledsTopic = commonTopic + "/vebus/" + MultiplusThreeDigitID + "/Leds";

  client.subscribe(ledsTopic + "/Bulk", [](const String &payload)
                   {
    awaitingDataToBeReceived[7] = false;
    String response = String(payload);
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, response);
    int LEDIndicator = doc["value"].as<int>();
    if (LEDIndicator == 1)
      chargingState = "Bulk";
    if (verboseDebugOutput)
      Serial.println("Multiplus Bulk LED is on");
    lastMQTTUpdateReceived = millis();
    doc.clear(); });

  msTimer.begin(100);

  client.subscribe(ledsTopic + "/Absorption", [](const String &payload)
                   {
    awaitingDataToBeReceived[7] = false;
    String response = String(payload);
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, response);
    int LEDIndicator = doc["value"].as<int>();
    if (LEDIndicator == 1)
      chargingState = "Absorption";
    if (verboseDebugOutput)
      Serial.println("Multiplus Absorption LED is on");
    lastMQTTUpdateReceived = millis();
    doc.clear(); });

  msTimer.begin(100);

  client.subscribe(ledsTopic + "/Float", [](const String &payload)
                   {
    awaitingDataToBeReceived[7] = false;
    String response = String(payload);
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, response);
    int LEDIndicator = doc["value"].as<int>();
    if (LEDIndicator == 1)
      chargingState = "Float";
    if (verboseDebugOutput)
      Serial.println("Multiplus Float LED is on");
    lastMQTTUpdateReceived = millis();
    doc.clear(); });

  msTimer.begin(100);

  KeepMQTTAlive(true);
}

void SubscribeToGetChargingStateFromSolarCharger()
{

  String commonTopic = "N/" + VictronInstallationID;
  String solarChargerStateTopic = commonTopic + "/solarcharger/" + SolarChargerThreeDigitID + "/State";

  client.subscribe(solarChargerStateTopic, [](const String &payload)
                   {
    String response = String(payload);
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, response);
    int stateCode = doc["value"].as<int>();

    switch (stateCode) {
      case 0:
        chargingState = "Off";
        break;
      case 2:
        chargingState = "Fault";
        break;
      case 3:
        chargingState = "Bulk";
        break;
      case 4:
        chargingState = "Absorption";
        break;
      case 5:
        chargingState = "Float";
        break;
      case 6:
        chargingState = "Storage";
        break;
      case 7:
        chargingState = "Equalize";
        break;
      case 252:
        // ESS is being used, so we will get the Charging State from the Multiplus instead
        // this will be done outside this switch statement to keep the compiler happy
        ESSIsBeingUsed = true;
        chargingState = "";
        break;
      default:
        chargingState = "Unknown";
        break;
    };

    if (ESSIsBeingUsed) {

      String commonTopic = "N/" + VictronInstallationID;
      String xsolarChargerStateTopic = commonTopic + "/solarcharger/" + SolarChargerThreeDigitID + "/State";

      client.unsubscribe(xsolarChargerStateTopic);
      SubscribeToGetChargingStateFromMultiplus();
    } else {
      awaitingDataToBeReceived[7] = false;
      if (verboseDebugOutput)
        Serial.println("Charging State from MPPT: " + chargingState);
    };

    lastMQTTUpdateReceived = millis();
    doc.clear(); });
}

void MassSubscribe()
{

  // at this point we have the VictronInstallationID, MultiplusThreeDigitID and SolarChargerThreeDigitID so let's get the rest of the data

  if (generalDebugOutput)
    Serial.println("Subscribing");

  String commonTopic = "N/" + VictronInstallationID;
  String system0Topic = commonTopic + "/system/0/";
  String multiplusModeTopic = commonTopic + "/vebus/" + MultiplusThreeDigitID + "/Mode";

  // reset global variables so we will not start displaying information until all the subscribed data has been received
  ResetGlobals();

  // get the data

  // Grid (L1, L2, L3)

  if (GENERAL_SETTINGS_GRID_IN_L1_IS_USED)
  {

    client.subscribe(system0Topic + "Ac/Grid/L1/Power", [](const String &payload)
                     {
      awaitingDataToBeReceived[0] = false;
      String response = String(payload);
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, response);
      gridInL1Watts = doc["value"].as<float>();
      if (verboseDebugOutput)
        Serial.println("gridInL1Watts: " + String(gridInL1Watts));
      lastMQTTUpdateReceived = millis();
      doc.clear(); });

    msTimer.begin(100);
  }
  else
  {
    awaitingDataToBeReceived[0] = false;
  };

  if (GENERAL_SETTINGS_GRID_IN_L2_IS_USED)
  {

    client.subscribe(system0Topic + "Ac/Grid/L2/Power", [](const String &payload)
                     {
      awaitingDataToBeReceived[1] = false;
      String response = String(payload);
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, response);
      gridInL2Watts = doc["value"].as<float>();
      if (verboseDebugOutput)
        Serial.println("gridInL2Watts: " + String(gridInL2Watts));
      lastMQTTUpdateReceived = millis();
      doc.clear(); });

    msTimer.begin(100);
  }
  else
  {
    awaitingDataToBeReceived[1] = false;
  };

  if (GENERAL_SETTINGS_GRID_IN_L3_IS_USED)
  {

    client.subscribe(system0Topic + "Ac/Grid/L3/Power", [](const String &payload)
                     {
      awaitingDataToBeReceived[2] = false;
      String response = String(payload);
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, response);
      gridInL3Watts = doc["value"].as<float>();
      if (verboseDebugOutput)
        Serial.println("gridInL3Watts: " + String(gridInL3Watts));
      lastMQTTUpdateReceived = millis();
      doc.clear(); });

    msTimer.begin(100);
  }
  else
  {
    awaitingDataToBeReceived[2] = false;
  };

  // Solar
  if (GENERAL_SETTINGS_PV_IS_USED)
  {
    client.subscribe(system0Topic + "Dc/Pv/Power", [](const String &payload)
                     {
      awaitingDataToBeReceived[3] = false;
      String response = String(payload);
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, response);
      solarWatts = doc["value"].as<float>();
      if (verboseDebugOutput)
        Serial.println("solarWatts: " + String(solarWatts));
      lastMQTTUpdateReceived = millis();
      doc.clear(); });

    msTimer.begin(100);
  }
  else
  {
    awaitingDataToBeReceived[3] = false;
  };

  // Battery

  client.subscribe(system0Topic + "Dc/Battery/Soc", [](const String &payload)
                   {
    awaitingDataToBeReceived[4] = false;
    String response = String(payload);
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, response);
    batterySOC = doc["value"].as<float>();
    if (verboseDebugOutput)
      Serial.println("batterySOC: " + String(batterySOC));
    lastMQTTUpdateReceived = millis();
    doc.clear(); });

  msTimer.begin(100);

  client.subscribe(system0Topic + "Dc/Battery/Power", [](const String &payload)
                   {
    awaitingDataToBeReceived[5] = false;
    String response = String(payload);
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, response);
    batteryPower = doc["value"].as<float>();
    if (verboseDebugOutput)
      Serial.println("batteryPower: " + String(batteryPower));
    lastMQTTUpdateReceived = millis();
    doc.clear(); });

  msTimer.begin(100);

  switch (GENERAL_SETTINGS_ADDITIONAL_INFO)
  {

  case 0:

    awaitingDataToBeReceived[6] = false;
    awaitingDataToBeReceived[7] = false;
    awaitingDataToBeReceived[8] = false;
    break;

  case 1:

    client.subscribe(system0Topic + "Dc/Battery/TimeToGo", [](const String &payload)
                     {
        awaitingDataToBeReceived[6] = false;
        String response = String(payload);
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, response);
        batteryTTG = doc["value"].as<float>();
        if (verboseDebugOutput)
          Serial.println("batteryTTG: " + String(batteryTTG));
        lastMQTTUpdateReceived = millis();
        doc.clear(); });

    awaitingDataToBeReceived[7] = false;
    awaitingDataToBeReceived[8] = false;
    break;

  case 2:

    awaitingDataToBeReceived[6] = false;

    if (ESSIsBeingUsed)
      SubscribeToGetChargingStateFromMultiplus();
    else
      SubscribeToGetChargingStateFromSolarCharger();

    awaitingDataToBeReceived[8] = false;
    break;

  case 3:

    awaitingDataToBeReceived[6] = false;
    awaitingDataToBeReceived[7] = false;

    client.subscribe(system0Topic + "Dc/Battery/Temperature", [](const String &payload)
                     {
        awaitingDataToBeReceived[8] = false;
        String response = String(payload);
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, response);
        batteryTemperature = doc["value"].as<float>();
        if (verboseDebugOutput)
          Serial.println("batteryTemperature: " + String(batteryTemperature));
        lastMQTTUpdateReceived = millis();
        doc.clear(); });

    msTimer.begin(100);
    break;

    case 4:

      awaitingDataToBeReceived[6] = false;
      awaitingDataToBeReceived[7] = false;
      awaitingDataToBeReceived[8] = false;
      break;

  default:
    break;
  };

  msTimer.begin(100);

  // AC Out (L1, L2, L3)

  if (GENERAL_SETTINGS_AC_OUT_L1_IS_USED)
  {
    client.subscribe(system0Topic + "Ac/Consumption/L1/Power", [](const String &payload)
                     {
      awaitingDataToBeReceived[9] = false;
      String response = String(payload);
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, response);
      ACOutL1Watts = doc["value"].as<float>();
      if (verboseDebugOutput)
        Serial.println("ACOutL1Watts: " + String(ACOutL1Watts));
      lastMQTTUpdateReceived = millis();
      doc.clear(); });

    msTimer.begin(100);
  }
  else
  {
    awaitingDataToBeReceived[9] = false;
  };

  if (GENERAL_SETTINGS_AC_OUT_L2_IS_USED)
  {
    client.subscribe(system0Topic + "Ac/Consumption/L2/Power", [](const String &payload)
                     {
      awaitingDataToBeReceived[10] = false;
      String response = String(payload);
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, response);
      ACOutL2Watts = doc["value"].as<float>();
      if (verboseDebugOutput)
        Serial.println("ACOutL2Watts: " + String(ACOutL2Watts));
      lastMQTTUpdateReceived = millis();
      doc.clear(); });

    msTimer.begin(100);
  }
  else
  {
    awaitingDataToBeReceived[10] = false;
  };

  if (GENERAL_SETTINGS_AC_OUT_L3_IS_USED)
  {
    client.subscribe(system0Topic + "Ac/Consumption/L3/Power", [](const String &payload)
                     {
      awaitingDataToBeReceived[11] = false;
      String response = String(payload);
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, response);
      ACOutL3Watts = doc["value"].as<float>();
      if (verboseDebugOutput)
        Serial.println("ACOutL3Watts: " + String(ACOutL3Watts));
      lastMQTTUpdateReceived = millis();
      doc.clear(); });

    msTimer.begin(100);
  }
  else
  {
    awaitingDataToBeReceived[11] = false;
  };

  // Multiplus mode

  currentMultiplusMode = Unknown;

  client.subscribe(multiplusModeTopic, [](const String &payload)
                   {
    awaitingDataToBeReceived[12] = false;
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
    doc.clear(); });

  msTimer.begin(100);

  KeepMQTTAlive(true);
}

void MassUnsubscribe()
{

  if (generalDebugOutput)
    Serial.println("Unsubscribing");

  String commonTopic = "N/" + VictronInstallationID;
  String system0Topic = commonTopic + "/system/0/";
  String multiplusModeTopic = commonTopic + "/vebus/" + MultiplusThreeDigitID + "/Mode";
  String solarChargerStateTopic = commonTopic + "/solarcharger/" + SolarChargerThreeDigitID + "/State";
  String ledsTopic = commonTopic + "/vebus/" + MultiplusThreeDigitID + "/Leds/";

  if (GENERAL_SETTINGS_GRID_IN_L1_IS_USED)
    client.unsubscribe(system0Topic + "Ac/Grid/L1/Power");

  if (GENERAL_SETTINGS_GRID_IN_L2_IS_USED)
    client.unsubscribe(system0Topic + "Ac/Grid/L2/Power");

  if (GENERAL_SETTINGS_GRID_IN_L3_IS_USED)
    client.unsubscribe(system0Topic + "Ac/Grid/L3/Power");

  if (GENERAL_SETTINGS_PV_IS_USED)
    client.unsubscribe(system0Topic + "Dc/Pv/Power");

  client.unsubscribe(system0Topic + "Dc/Battery/Soc");

  client.unsubscribe(system0Topic + "Dc/Battery/Power");

  switch (GENERAL_SETTINGS_ADDITIONAL_INFO)
  {
  case 1:
    client.unsubscribe(system0Topic + "Dc/Battery/TimeToGo");
    break;
  case 2:
    if (ESSIsBeingUsed)
    {
      client.unsubscribe(ledsTopic + "Bulk");
      client.unsubscribe(ledsTopic + "Absorption");
      client.unsubscribe(ledsTopic + "Float");
    }
    else
      client.unsubscribe(solarChargerStateTopic);
    break;
  case 3:
    client.unsubscribe(system0Topic + "Dc/Battery/Temperature");
    break;
  default:
    break;
  };

  if (GENERAL_SETTINGS_AC_OUT_L1_IS_USED)
    client.unsubscribe(system0Topic + "Ac/Consumption/L1/Power");

  if (GENERAL_SETTINGS_AC_OUT_L2_IS_USED)
    client.unsubscribe(system0Topic + "Ac/Consumption/L2/Power");

  if (GENERAL_SETTINGS_AC_OUT_L3_IS_USED)
    client.unsubscribe(system0Topic + "Ac/Consumption/L3/Power");

  client.unsubscribe(multiplusModeTopic);

  msTimer.begin(100);
}

void onConnectionEstablished()
{

  // note: this subroutine uses recursion to discover the VictronInstallationID, MultiplusThreeDigitID and (if needed) SolarChargerThreeDigitID

  if (VictronInstallationID == "+")
  {

    // Let's find the Victron Installation ID

    client.subscribe("N/+/system/0/Serial", [](const String &topic, const String &payload)
                     {
      client.unsubscribe("N/+/system/0/Serial");
      String mytopic = String(topic);
      VictronInstallationID = mytopic.substring(2, 14);
      VictronInstallationID.toCharArray(VictronInstallationIDArray, VictronInstallationID.length() + 1);
      if (generalDebugOutput)
        Serial.println("*** Discovered Installation ID: " + VictronInstallationID);

      onConnectionEstablished();
      return; });

    return;
  };

  if (MultiplusThreeDigitID == "+")
  {

    // Let's find the Multiplus three digit ID
    String commonTopic = "N/" + VictronInstallationID;
    client.subscribe(commonTopic + "/vebus/+/Mode", [](const String &topic, const String &payload)
                     {
      String commonTopic = "N/" + VictronInstallationID;
      client.unsubscribe(commonTopic + "/vebus/+/Mode");
      String mytopic = String(topic);
      MultiplusThreeDigitID = mytopic.substring(21, 24);
      MultiplusThreeDigitID.toCharArray(MultiplusThreeDigitIDArray, MultiplusThreeDigitID.length() + 1);
      if (generalDebugOutput)
        Serial.println("*** Discovered Multiplus three digit ID: " + MultiplusThreeDigitID);

      onConnectionEstablished();
      return; });

    // a keep alive request is required for Venus to publish the topic subscribed to above
    KeepMQTTAlive(true);

    return;
  };

  if ((SolarChargerThreeDigitID == "+") && (GENERAL_SETTINGS_ADDITIONAL_INFO == 2))
  {

    // Let's find the solarcharger three digit ID
    String commonTopic = "N/" + VictronInstallationID;
    client.subscribe(commonTopic + "/solarcharger/+/Mode", [](const String &topic, const String &payload)
                     {
      String commonTopic = "N/" + VictronInstallationID;
      client.unsubscribe(commonTopic + "/solarcharger/+/Mode");
      String mytopic = String(topic);
      SolarChargerThreeDigitID = mytopic.substring(28, 31);
      SolarChargerThreeDigitID.toCharArray(SolarChargerThreeDigitIDArray, SolarChargerThreeDigitID.length() + 1);
      if (generalDebugOutput)
        Serial.println("*** Discovered Solar Charger three digit ID: " + SolarChargerThreeDigitID);

      onConnectionEstablished();
      return; });

    // a keep alive request is required for Venus to publish the topic subscribed to above
    KeepMQTTAlive(true);

    return;
  };

  // at this point the recursive calling is over and we have the VictronInstallationID, MultiplusThreeDigitID and (if needed) SolarChargerThreeDigitID so let's get the rest of the data

  MassSubscribe();
}

void onWiFiConnectionEstablished(WiFiEvent_t event, WiFiEventInfo_t info)
{

  if (generalDebugOutput)
  {

    Serial.print("Wi-Fi connected to: ");
    Serial.println(String(SECRET_SETTINGS_WIFI_SSID));

    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  };

  RefreshTimeOnceADay(true);
}

void SetupWiFiAndMQTT()
{

  if (generalDebugOutput)
    Serial.println("Setting up Wi-Fi and MQTT");

  // if GENERAL_SETTINGS_TURN_ON_DISPLAY_AT_SPECIFIC_TIMES_ONLY is true
  // then enable the on-connection event in order that the time from an NTP server once the Wifi connection has been established
  // otherwise this is not needed
  if (GENERAL_SETTINGS_TURN_ON_DISPLAY_AT_SPECIFIC_TIMES_ONLY)
    WiFi.onEvent(onWiFiConnectionEstablished, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);

  if (GENERAL_SETTINGS_ENABLE_OVER_THE_AIR_UPDATES)
    client.enableOTA(SECRET_SETTINGS_OTA_PASSWORD, SECRET_SETTINGS_OTA_Port);

  if (verboseDebugOutput)
    client.enableDebuggingMessages();
}

bool isNumeric(String str)
{

  for (size_t i = 0; i < str.length(); i++)
  {
    if (!isdigit(str.charAt(i)))
    {
      return false;
    }
  }
  return true;
}

void convertSecondsToTime(int seconds, int &hours, int &minutes, int &remainingSeconds)
{
  hours = seconds / 3600; // 3600 seconds in an hour
  seconds %= 3600;
  minutes = seconds / 60; // 60 seconds in a minute
  remainingSeconds = seconds % 60;
}

void GotoDeepSleep()
{

  // this routine is only called when it is time to send the ESP32 to sleep
  // according the logic below counts on the fact that the current time is currently within the sleep period

  int toleranceSeconds = 15; // don't bother going to sleep if a wakeup would otherwise happen in this many seconds

  int secondsInDeepSleep = 0;

  String sleepTime = String(GENERAL_SETTINGS_SLEEP_TIME);

  String wakeTime = String(GENERAL_SETTINGS_WAKE_TIME);

  if (sleepTime == wakeTime)
  {

    // if sleep time = wake time go into deep sleep,
    // however do not set a timer to wake up, rather wake will be handled by a button press

    MassUnsubscribe();

    if (generalDebugOutput)
    {
      if (GENERAL_SETTINGS_USB_ON_THE_LEFT)
        Serial.print("Going to sleep; device may be awakened by pressing the top button");
      else
        Serial.print("Going to sleep; device may be awakened by pressing the bottom button");
      delay(250);
      Serial.flush();
    };

    // provide time for the mass subscribe to complete
    msTimer.begin(1000);

    esp_deep_sleep_start();
  }
  else
  {

    if (parseTimeString(sleepTime, sleepHour, sleepMinute))
    {

      if (parseTimeString(wakeTime, wakeHour, wakeMinute))
      {

        int wakeupTimeInSeconds = (wakeHour * 60 + wakeMinute) * 60;

        int sleepTimeInSeconds = (sleepHour * 60 + sleepMinute) * 60;

        int NowInSeconds;

        struct tm timeinfo;
        if (getLocalTime(&timeinfo))
        {
          NowInSeconds = (timeinfo.tm_hour * 60 + timeinfo.tm_min) * 60 + timeinfo.tm_sec;
        }
        else
        {

          int tryAgainInThisManyMinutes = 10;
          SetKeepDisplayOnTimeOut(tryAgainInThisManyMinutes);
          if (generalDebugOutput)
          {
            Serial.println("Could not set wakeup timer - network may be down - try again in another ");
            Serial.println(tryAgainInThisManyMinutes);
            Serial.println(" minutes");
          };
          return;
        };

        int secondsToDeepSleep;

        if (wakeupTimeInSeconds < sleepTimeInSeconds)
        {

          if (NowInSeconds == wakeupTimeInSeconds)
          {
            secondsInDeepSleep = 0;
          }
          else
          {

            // the current time must be either before wakeup time or after sleep time

            if (NowInSeconds < wakeupTimeInSeconds)
            {
              secondsInDeepSleep = wakeupTimeInSeconds - NowInSeconds;
            }
            else
            {
              int secondsInADay = 24 * 60 * 60;
              secondsInDeepSleep = secondsInADay - NowInSeconds + wakeupTimeInSeconds;
            };
          };
        }
        else
        {

          // wakeupTimeInSeconds > sleepTimeInSeconds

          if (NowInSeconds != wakeupTimeInSeconds)
          {

            // the current time must be between the sleep time and the wakeup time
            secondsInDeepSleep = wakeupTimeInSeconds - NowInSeconds;
          };
        };
      };
    };
  };

  if (secondsInDeepSleep > toleranceSeconds)
  {

    MassUnsubscribe();

    if (generalDebugOutput)
    {

      int hours, minutes, remainingSeconds;
      convertSecondsToTime(secondsInDeepSleep, hours, minutes, remainingSeconds);

      Serial.print("Going to sleep for ");

      Serial.print(hours);
      if (hours == 1)
        Serial.print(" hour ");
      else
        Serial.print(" hours ");

      Serial.print(minutes);
      if (minutes == 1)
        Serial.print(" minute ");
      else
        Serial.print(" minutes ");

      Serial.print(remainingSeconds);
      if (remainingSeconds == 1)
        Serial.println(" second");
      else
        Serial.println(" seconds");

      delay(250);
      Serial.flush();
    };

    // provide time for the mass subscribe to complete
    msTimer.begin(1000);

    const uint64_t convertSecondsToMicroSeconds = 1000000;
    unsigned long long DeepSleepMicroSeconds = secondsInDeepSleep * convertSecondsToMicroSeconds;
    esp_sleep_enable_timer_wakeup(DeepSleepMicroSeconds);
    esp_deep_sleep_start();
  }
  else
  {

    if (generalDebugOutput)
    {
      Serial.print("No need to sleep, as it'll be time to wakeup in the next ");
      Serial.print(toleranceSeconds);
      Serial.println(" seconds anyway!");
    };
  };
}

void SetTheDisplayOn(bool theDisplayShouldBeOn)
{

  static bool firstTimeSetup = true;

  if (theDisplayIsCurrentlyOn && !theDisplayShouldBeOn)
  {

    if (GENERAL_SETTINGS_USE_DEEP_SLEEP)
    {

      GotoDeepSleep();
    }
    else
    {

      // an AMOLED screen has no backlight as each pixel is an individual LED, so filling the display with black effectively turns it off causing it to use the least current possible
      sprite.fillSprite(TFT_BLACK);
      RefreshDisplay();

      // if the display is off there is no use receiving MQTT information, therefore unsubscribe to it so network traffic may be reduced
      MassUnsubscribe();

      theDisplayIsCurrentlyOn = false;

      if (generalDebugOutput)
        Serial.println("Display turned off");
    };
  }
  else
  {

    if (!theDisplayIsCurrentlyOn && theDisplayShouldBeOn)
    {

      // turn the display on
      // effectively just re-enable updates to it

      // if this is the first time the display is being turned on then
      // subscribe so that the data will be updated now that the display has been turned back on
      if (firstTimeSetup)
        firstTimeSetup = false;
      else
        MassSubscribe();

      theDisplayIsCurrentlyOn = true;

      if (generalDebugOutput)
        Serial.println("Display turned on");
    };
  };
}

void SetDisplayOrientation()
{

  if (GENERAL_SETTINGS_USB_ON_THE_LEFT)
    lcd_setRotation(3);
  else
    lcd_setRotation(1);
};

bool parseTimeString(String timeString, int &hours, int &minutes)
{

  if (timeString.length() != 5)
  { // Check if the length is not equal to "HH:MM"
    return false;
  }

  if (timeString.charAt(2) != ':')
  { // Check if the separator is at the correct position
    return false;
  }

  String hourStr = timeString.substring(0, 2); // Extract hours substring
  String minuteStr = timeString.substring(3);  // Extract minutes substring

  if (!isNumeric(hourStr) || !isNumeric(minuteStr))
  { // Check if both substrings are numeric
    return false;
  }

  hours = hourStr.toInt();     // Convert hour string to integer
  minutes = minuteStr.toInt(); // Convert minute string to integer

  if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59)
  { // Check if hours and minutes are within valid range
    return false;
  }

  return true; // Time string successfully parsed
}

void SetDisplayOnAndOffTimes()
{

  if (turnOnDisplayAtSpecificTimesOnly)
  {

    String sleepTime = String(GENERAL_SETTINGS_SLEEP_TIME);
    String wakeTime = String(GENERAL_SETTINGS_WAKE_TIME);

    if (parseTimeString(sleepTime, sleepHour, sleepMinute))
    {

      if (parseTimeString(wakeTime, wakeHour, wakeMinute))
      {

        int timeToWake = wakeHour * 60 + wakeMinute;
        int timeToSleep = sleepHour * 60 + sleepMinute;
        theDisplayIsCurrentlyOn = (timeToWake < timeToSleep);

        if (verboseDebugOutput)
        {
          if (timeToWake < timeToSleep)
          {
            Serial.print("Wake time set to:  ");
            Serial.println(wakeTime);
            Serial.print("Sleep time set to: ");
            Serial.println(sleepTime);
          }
          else
          {
            Serial.print("Sleep time set to: ");
            Serial.println(sleepTime);
            Serial.print("Wake time set to:  ");
            Serial.println(wakeTime);
          };
        };
      }
      else
      {
        turnOnDisplayAtSpecificTimesOnly = false;
        if (generalDebugOutput)
          Serial.println("Problem with GENERAL_SETTINGS_WAKE_TIME setting value, should be HH:MM");
      }
    }
    else
    {
      turnOnDisplayAtSpecificTimesOnly = false;
      if (generalDebugOutput)
        Serial.println("Problem with GENERAL_SETTINGS_SLEEP_TIME setting value, should be HH:MM");
    };
  }
  else
  {
    if (generalDebugOutput)
      Serial.println("Display set as always on");
  };
};

void SetupDisplay()
{

  sprite.createSprite(TFT_WIDTH, TFT_HEIGHT);
  sprite.setSwapBytes(1);

  rm67162_init();

  SetDisplayOrientation();

  SetDisplayOnAndOffTimes();

  SetKeepDisplayOnTimeOut(1);

  SetTheDisplayOn(true);
}

void ShowOpeningWindow()
{

  if ((GENERAL_SETTINGS_SHOW_SPLASH_SCREEN) && (initialStartupShowSplashScreen))
  {

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

    initialStartupShowSplashScreen = false;
  };
}

void SetDebugLevel()
{

  generalDebugOutput = (GENERAL_SETTINGS_DEBUG_OUTPUT_LEVEL > 0);
  verboseDebugOutput = (GENERAL_SETTINGS_DEBUG_OUTPUT_LEVEL > 1);

  if (generalDebugOutput)
  {
    Serial.begin(GENERAL_SETTINGS_SERIAL_MONITOR_SPEED);
  };
};

void SetWakeUpButton()
{

  if ((GENERAL_SETTINGS_TURN_ON_DISPLAY_AT_SPECIFIC_TIMES_ONLY) && (GENERAL_SETTINGS_USE_DEEP_SLEEP))
  {

    // set wakeup button; sadly while the button tied to GPIO 0 can be used for this, the button tied to GPIO 21 cannot
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0); // set wake-up on button controlled by GPIO 0

    if (generalDebugOutput)
    {
      if (GENERAL_SETTINGS_USB_ON_THE_LEFT)
        Serial.println("Wakeup button is on the top");
      else
        Serial.println("Wakeup button is on the bottom");
    };
  };
};

void setup()
{

  SetDebugLevel();

  if (generalDebugOutput)
  {

    Serial.println("");
    Serial.println(programName + " " + programVersion);
    Serial.println(programURL);
    Serial.println("");
  };

  if (GENERAL_SETTINGS_AMOLED_VERSION == 1)
  {
    SetGreenLEDOff();
  }
  else
  {
    SetDisplayOn();
  };

  SetupTopAndBottomButtons();

  SetWakeUpButton();

  SetupDisplay();

  ResetGlobals();

  SetupWiFiAndMQTT();

  ShowOpeningWindow();
}

void loop()
{

  client.loop();

  KeepMQTTAlive();

  CheckButtons();

  UpdateDisplay();

  RefreshTimeOnceADay();

  ArduinoOTA.handle();
}
