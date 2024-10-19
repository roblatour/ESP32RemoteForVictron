#define GENERAL_SETTINGS_AMOLED_VERSION                                   1    // set to 1 if you are using the Lilygo T-Display S3 AMOLED v1
                                                                               // set to 2 if you are using the Lilygo T-Display S3 AMOLED v2 
                                                                               // for more information please see: https://www.lilygo.cc/en-ca/products/t-display-s3-amoled
																			   
#define GENERAL_SETTINGS_USB_ON_THE_LEFT                               true    // set to true to plug in the USB power cable from the left, set to false to plug in the USB power cable from the right

#define GENERAL_SETTINGS_ALLOW_CHANGING_INVERTER_AND_CHARGER_MODES     true    // set to true to allow the user to change the Multiplus charger and inverter modes with the buttons on this device, otherwise to prevent that set to false
#define GENERAL_SETTINGS_SHOW_CHARGER_MODE                             true    // set to true to show the charger mode, otherwise set to false to hide the charger mode
#define GENERAL_SETTINGS_SHOW_INVERTER_MODE                            true    // set to true to show the inverter mode, otherwise set to false to hide the inverter mode
                                      
#define GENERAL_SETTINGS_SECONDS_BETWEEN_DISPLAY_UPDATES                  1    // seconds between display updates

#define GENERAL_SETTINGS_IF_OVER_1000_WATTS_REPORT_KW                  true    // if value being reported is over 1000 Watts then if true report in Kilo Watts otherwise report in Watts

#define GENERAL_SETTINGS_NUMBER_DECIMAL_PLACES_FOR_KW_REPORTING           1    // if GENERAL_SETTINGS_IF_OVER_1000_WATTS_REPORT_KW is true, then show KiloWatts with this many decimal places

#define GENERAL_SETTINGS_ROUND_NUMBERS                                 true    // if true numbers will be rounded, otherwise they will be truncated (i.e. 9.55 rounded = 9.6; 9.55 truncated = 9.5)

#define GENERAL_SETTINGS_SHOW_BATTERY_AS_YELLOW                          40    // show battery as yellow if its percentage charged is at or below this number

#define GENERAL_SETTINGS_SHOW_BATTERY_AS_RED                             20    // show battery as red if its percentage charged is at or below this number   

#define GENERAL_SETTINGS_ADDITIONAL_INFO                                  2    // show additional information under battery percent:
                                                                               // 0 = do not show any additional info  
                                                                               // 1 = show Time To Go (remaining battery time); note this value is only available when the battery is discharging
                                                                               // 2 = show Solar Charger (mppt) / Multiplus charging state: Off/Fault/Bulk/Absorption/Float/Storage/Equalize/ESS
                                                                               // 3 = show battery temperature

                                                                               // if any of the following are not used in your installation then you can set the associated value(s) below to false to reduce unneeded MQTT traffic:
#define GENERAL_SETTINGS_GRID_IN_L1_IS_USED                            true    // set to true if Grid IN L1 is used in your installation, otherwise set to false
#define GENERAL_SETTINGS_GRID_IN_L2_IS_USED                            true    // set to true if Grid IN L2 is used in your installation, otherwise set to false
#define GENERAL_SETTINGS_GRID_IN_L3_IS_USED                            true    // set to true if Grid IN L3 is used in your installation, otherwise set to false
#define GENERAL_SETTINGS_PV_IS_USED                                    true    // set to true if PV (Solar) is used in your installation, otherwise set to false
#define GENERAL_SETTINGS_AC_OUT_L1_IS_USED                             true    // set to true if AC OUT L1 is used in your installation, otherwise set to false
#define GENERAL_SETTINGS_AC_OUT_L2_IS_USED                             true    // set to true if AC OUT L2 is used in your installation, otherwise set to false
#define GENERAL_SETTINGS_AC_OUT_L3_IS_USED                             true    // set to true if AC OUT L3 is used in your installation, otherwise set to false

#define GENERAL_SETTINGS_SHOW_SPLASH_SCREEN                            true    // set to true show the splash screen on initial startup, set to false to not show the splash screen

#define GENERAL_SETTINGS_TURN_ON_DISPAY_AT_SPECIFIC_TIMES_ONLY        false    // set to true to turn on the display between the times indentified below, set to false to leave the display always on
                                                                               //
                                                                               // if GENERAL_SETTINGS_TURN_ON_DISPAY_AT_SPECIFIC_TIMES_ONLY is true, then the following will also be needed:
                                                                               //      
#define GENERAL_SETTINGS_USE_DEEP_SLEEP                                true    //     set to true to use deep sleep (see notes below)
                                                                               //                                                                                    
#define GENERAL_SETTINGS_WAKE_TIME                                  "06:15"    //     the time at which the display will automatically be turned on - in 24 hour format between 00:00 and 23:59
#define GENERAL_SETTINGS_SLEEP_TIME                                 "23:45"    //     the time at which the display will automatically be turned off - in 24 hour format between 00:00 and 23:59
                                                                               //
                                                                               //     NOTES: 
                                                                               //
                                                                               //     1. if GENERAL_SETTINGS_WAKE_TIME and GENERAL_SETTINGS_SLEEP_TIME are the same then the display will
                                                                               //        default to being off except for a minute following startup or when a button is pressed
                                                                               //
                                                                               //     2. GENERAL_SETTINGS_WAKE_TIME does not need to be less than the GENERAL_SETTINGS_SLEEP_TIME
                                                                               //
                                                                               //     3. to turn on the display outside the times prescribed above, press 
                                                                               //        the top button if GENERAL_SETTINGS_USB_ON_THE_LEFT = true 
                                                                               //        or
                                                                               //        the bottom button if GENERAL_SETTINGS_USB_ON_THE_LEFT = false            
                                                                               //
                                                                               //     4. If GENERAL_SETTINGS_USE_DEEP_SLEEP = true                                                                  
                                                                               //        The esp32 will use far less power when in sleep mode
                                                                               //        However, the wake up time will not be exact as there are inherent inaccuracies with the precision of the ESP32's 
                                                                               //        internal real time clock. For example, in testing with my device a 23 hour and 50 minute sleep cycle resulted 
                                                                               //        in my device waking up 15 minutes and 9 seconds earlier than it should have.  However, shorter sleep periods should 
                                                                               //        have smaller discrepancies.
                                                                               //        If deep sleep is not used, the esp32 will use more power when the display is off, but automatic wakeup times
                                                                               //        will be precise.
                                                                               //
#define GENERAL_SETTINGS_MY_TIME_ZONE              "EST5EDT,M3.2.0,M11.1.0"    //     Time Zone; supported Timezones are listed here: https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv
                                                                               //
#define GENERAL_SETTINGS_PRIMARY_TIME_SERVER                  "time.nrc.ca"    //     primary ntp time server   
#define GENERAL_SETTINGS_SECONDARY_TIME_SERVER            "ca.pool.ntp.org"    //     secondary ntp time server
#define GENERAL_SETTINGS_TERTIARY_TIME_SERVER  "north-america.pool.ntp.org"    //     tertiary ntp time server 
                                                                               //     alternative ntp time servers / server pools that may be used above:                                                                          
                                                                               //        "time.nrc.ca"                        // Ottawa, Ontario, Canada
                                                                               //        "ca.pool.ntp.org"                    // Canada
                                                                               //        "asia.pool.ntp.org"
                                                                               //        "europe.pool.ntp.org"
                                                                               //        "north-america.pool.ntp.org" 
                                                                               //        "oceania.pool.ntp.org"
                                                                               //        "south-america.pool.ntp.org" 
                                                                               //        "pool.ntp.org"                       // World wide      

#define GENERAL_SETTINGS_SEND_PERIODICAL_KEEP_ALIVE_REQUESTS           true    // Keep Alive requests are needed periodically by Venus to continue to publish MQTT data 
                                                                               //
                                                                               // Generally, this option should be set to true to have this program periodically send the needed Keep Alive requests.
                                                                               // However, if another system (for example Home Assistant) is already sending periodic Keep Alive requests then
                                                                               // this option may be set to false to cut down on a very minor amount of network traffic.
                                                                               //
                                                                               // Notes: 
                                                                               // 1. even if this option is set to false, Keep Alive requests will still be sent by this program
                                                                               //    in specific cases to get data as needed
                                                                               //  
                                                                               // 2. it is recommended to set this option to true even if another system is sending Keep Alive requests,
                                                                               //    however, this is not absolutely necessary
                                                                               //
                                                                               // 3. if this option is set to false and you see the message "MQTT data updates have stopped" appear for several seconds 
                                                                               //    on the screen and then be replaced by a normal readout, it likely means that the other system that is supposed to be  
                                                                               //    sending the keep alive requests has stopped doing so
                                                                               //
                                                                               // 4. regardless of if this option is set to true or false, if you see the message "Awaiting Wi-Fi connection" appear 
                                                                               //    on your screen and stay there it means this device cannot access your Wi-Fi network
                                                                               //
                                                                               // 5. regardless of if this option is set to true or false, if you see the message "Awaiting MQTT connection" appear 
                                                                               //    on your screen and stay there it likely means Venus itself is no longer transmitting MQTT data 
#define GENERAL_SETTINGS_SEND_PERIODICAL_KEEP_ALIVE_REQUESTS_INTERVAL 30000    // Time between keep alive requests in ms

#define GENERAL_SETTINGS_ENABLE_OVER_THE_AIR_UPDATES                   true    // set to true to enable OTA updates, set to false to disable OTA updates

#define GENERAL_SETTINGS_DEBUG_OUTPUT_LEVEL                               1    // set to: 0 for no debug output
                                                                               //         1 for general debug output
                                                                               //         2 for verbose debug output

#define GENERAL_SETTINGS_SERIAL_MONITOR_SPEED                        115200    // serial monitor speed
