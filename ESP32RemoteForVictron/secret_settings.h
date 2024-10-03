
#define SECRET_SETTINGS_WIFI_SSID               "xxxxx"                    // your Wi-Fi Station ID which is on the same network as your Victron Venus device
#define SECRET_SETTINGS_WIFI_PASSWORD           "yyyyy"                    // your Wi-Fi Password

#define SECRET_SETTING_VICTRON_INSTALLATION_ID  "+"                        // your unique Victron Installation/Portal ID. If you don't know it you can use a "+" in this field - but it is more efficient to use the actual ID
#define SECRET_SETTING_VICTRON_MULTIPLUS_ID     "+"                        // your unique Victron Multiplus (vebus) three digit ID (often 288); if you don't know it you can use a "+" in this field but it is more efficient to use the actual ID
#define SECRET_SETTING_VICTRON_SOLAR_CHARGER_ID "+"                        // your unique Victron Solar Charger three digit ID (often 289); if you don't know it you can use a "+" in this field but it is more efficient to use the actual ID
                                                                           // note: in any or all of the three cases above if you use a '+' you can look in the Serial Monitor output to see what the discovered IDs are and then come back and change the setting(s) above

#define SECRET_SETTINGS_MQTT_Broker             "venus.local"              // generally you can use "venus.local" but this may also be an IPv4 address such as 192.168.1.195
                                                                           // however, if when you "ping venus.local" you get an IPv6 address, then do not use venus.local but rather the device's IPv4 address
                                                                           // you can get the device's IPv4 address with the command "ping -4 venus.local"
#define SECRET_SETTINGS_MQTT_UserID             "Anonymous"                // if you have MQTT security setup, you can enter your MQTT User ID here, otherwise leave the set to  "Anonymous"
#define SECRET_SETTINGS_MQTT_Password           "Anonymous"                // if you have MQTT security setup, you can enter your MQTT Password here, otherwise leave the set to  "Anonymous"
#define SECRET_SETTINGS_MQTT_ClientName         "ESP32RemoteForVictron"
#define SECRET_SETTINGS_MQTT_Port               1883

#define SECRET_SETTINGS_OTA_DEVICE_NAME         "ESP32RemoteForVictron"    // used when GENERAL_SETTINGS_ENABLE_OVER_THE_AIR_UPDATES is set to true
#define SECRET_SETTINGS_OTA_PASSWORD            "ESP32RemoteForVictron"    // used when GENERAL_SETTINGS_ENABLE_OVER_THE_AIR_UPDATES is set to true
#define SECRET_SETTINGS_OTA_Port                3332                       // used when GENERAL_SETTINGS_ENABLE_OVER_THE_AIR_UPDATES is set to true
