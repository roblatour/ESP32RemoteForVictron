#define GENERAL_SETTINGS_USB_ON_THE_LEFT                            true          // set to true to plug in the USB power cable from the left, set to false to plug in the USB power cable from the right

#define GENERAL_SETTINGS_SHOW_BATTERY_AS_YELLOW                       40          // show battery as yellow if its percentage charged is at or below this number

#define GENERAL_SETTINGS_SHOW_BATTERY_AS_RED                          20          // show battery as red if its percentage charged is at or below this number                                            

#define GENERAL_SETTINGS_SECONDS_BETWEEN_SCREEN_UPDATES                1          // seconds between screen updates

#define GENERAL_SETTINGS_SECONDS_BETWEEN_KEEP_ALIVE_REQUESTS          30          // seconds between keep alive requests

#define GENERAL_SETTINGS_IF_OVER_1000_WATTS_REPORT_KW                true         // if value being reported is over 1000 Watts then if true report in Kilo Watts otherwise report in Watts

#define GENERAL_SETTINGS_NUMBER_DECIMAL_PLACES_FOR_KW_REPORTING        1          // if GENERAL_SETTINGS_IF_OVER_1000_WATTS_REPORT_KW is true, then show KiloWatts with this many decimal places

                                                                                  // if any of the following are not used in your installation then you can set the assocated value(s) below to false to reduce unneeded MQTT traffic:
#define GENERAL_SETTINGS_GRID_IN_L1_IS_USED                          true         // set to true if Grid IN L1 is used in your installation, otherwise set to false
#define GENERAL_SETTINGS_PV_IS_USED                                  true         // set to true if PV (Solar) is used in your installation, otherwise set to false
#define GENERAL_SETTINGS_AC_OUT_L1_IS_USED                           true         // set to true if AC OUT L1 is used in your installation, otherwise set to false
#define GENERAL_SETTINGS_AC_OUT_L2_IS_USED                           true         // set to true if AC OUT L2 is used in your installation, otherwise set to false

#define GENERAL_SETTINGS_SHOW_SPLASH_SCREEN                          true         // set to true show the splash screen, set to false to not show the splash screen

#define GENERAL_SETTINGS_DEBUG_OUTPUT                                true         // set to true to have debug output shown in the serial monitor, set to false to not have debug output shown in the serial monitor
#define GENERAL_SETTINGS_SERIAL_MONITOR_SPEED                      115200         // serial monitor speed
