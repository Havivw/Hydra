#include <Arduino.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include <PCF8574.h>

#include "version.h"
#include "Touchscreen.h"
#include "wificonfig.h"
#include "bleconfig.h"
#include "subconfig.h"
#include "utils.h"
#include "shared.h"
#include "icon.h"
#include "flock_detect.h"
#include "pwn_detect.h"
#include "pineapple_detect.h"
#include "flipper_detect.h"
#include "airtag_detect.h"
#include "skimmer_detect.h"
#include "rayban_detect.h"
#include "hidden_reveal.h"
#include "probe_detect.h"
#include "esp_detect.h"
#include "evil_portal_detect.h"
#include "multissid_detect.h"
#include "ap_scan.h"
#include "sd_format.h"
#include "camera_scanner.h"
#include "deauth_attack.h"
#include "sae_attack.h"
#include "csa_attack.h"
#include "probe_flood.h"
#include "sweep_jammer.h"
#include "esppwnagotchi.h"
#include "evil_deauth.h"
#include "brucegotchi.h"
#include "nrf_intermittent.h"
#include "ir_remote.h"
#include "sd_browser.h"
#include "nrf_mode_jammer.h"
#include "gps.h"
#include "gps_status.h"
#include "subghz_wardrive.h"
#include "wardrive_config.h"
#include "wardrive_channels.h"
#include "spectrum_analyzer.h"
#include "freq_detector.h"
#include "sub_replay.h"
#include "sub_record.h"
#include "sub_sendcode.h"
#include "sub_keeloq.h"
#include "nrf_mousejack.h"
#include "nrf_sniffer.h"
#include "nrf_heatmap.h"
#include "nrf_carrier.h"
#include "nrf_wardrive.h"
#include "nrf_ble_adv.h"
#include "nrf_replay.h"
#include "nrf_triwatch.h"

TFT_eSPI tft = TFT_eSPI();

#define pcf_ADDR 0x20
PCF8574 pcf(pcf_ADDR);

#define BTN_UP     6
#define BTN_DOWN   3
#define BTN_LEFT   4
#define BTN_RIGHT  5
#define BTN_SELECT 7

bool feature_exit_requested = false;

const int NUM_MENU_ITEMS = 8;
const char *menu_items[NUM_MENU_ITEMS] = {
    "WiFi",
    "Bluetooth",
    "2.4GHz",
    "SubGHz",
    "Tools",
    "Setting",
    "About",
    "IR"};

const unsigned char *bitmap_icons[NUM_MENU_ITEMS] = {
    bitmap_icon_wifi,
    bitmap_icon_spoofer,
    bitmap_icon_jammer,
    bitmap_icon_analyzer,
    bitmap_icon_stat,
    bitmap_icon_setting,
    bitmap_icon_question,
    bitmap_icon_flash};

int current_menu_index = 0;
bool is_main_menu = false;


const int NUM_SUBMENU_ITEMS = 24;
const char *submenu_items[NUM_SUBMENU_ITEMS] = {
    "Packet Monitor",
    "Beacon Spammer",
    "WiFi Deauther",
    "Deauth Detector",
    "WiFi Scanner",
    "Captive Portal",
    "Flock Detector",
    "Pwnagotchi Det.",
    "Pineapple Det.",
    "Probe Sniffer",
    "Espressif Det.",
    "Evil Portal Det.",
    "Multi-SSID Det.",
    "AP Scan & Select",
    "Targeted Deauth",
    "WPA3 SAE Flood",
    "CSA Attack",
    "Camera Scanner",
    "Probe Req Flood",
    "ESPPwnagotchi",
    "EvilPortal+Deauth",
    "Brucegotchi",
    "Hidden SSID",
    "Back to Main Menu"};


const int bluetooth_NUM_SUBMENU_ITEMS = 10;
const char *bluetooth_submenu_items[bluetooth_NUM_SUBMENU_ITEMS] = {
    "BLE Jammer",
    "BLE Spoofer",
    "Sour Apple",
    "Sniffer",
    "BLE Scanner",
    "Flipper Det.",
    "AirTag Det.",
    "Skimmer Det.",
    "Ray-Ban Det.",
    "Back to Main Menu"};


const int nrf_NUM_SUBMENU_ITEMS = 13;
const char *nrf_submenu_items[nrf_NUM_SUBMENU_ITEMS] = {
    "Scanner",
    "Proto Kill",
    "Mousejack Scan",
    "Promisc Sniffer",
    "Channel Heatmap",
    "Const Carrier",
    "Wardrive (GPS)",
    "BLE Adv Mon.",
    "Capture+Replay",
    "Tri-Radio Watch",
    "Intermit. Jammer",
    "Mode Jammer",
    "Back to Main Menu"};


const int subghz_NUM_SUBMENU_ITEMS = 12;
const char *subghz_submenu_items[subghz_NUM_SUBMENU_ITEMS] = {
    "Replay Attack",
    "SubGHz Jammer",
    "Saved Profile",
    "Wardrive (GPS)",
    "Spectrum Anlz.",
    "Freq Detector",
    "Replay .sub",
    "Sweep Jammer",
    "Record .sub",
    "Send Code",
    "Send KeeLoq",
    "Back to Main Menu"};


const int tools_NUM_SUBMENU_ITEMS = 4;
const char *tools_submenu_items[tools_NUM_SUBMENU_ITEMS] = {
    "Format SD",
    "GPS Status",
    "SD Browser",
    "Back to Main Menu"};


const int settings_NUM_SUBMENU_ITEMS = 2;
const char *settings_submenu_items[settings_NUM_SUBMENU_ITEMS] = {
    "Wardrive Channels",
    "Back to Main Menu"};


const int about_NUM_SUBMENU_ITEMS = 1;
const char *about_submenu_items[about_NUM_SUBMENU_ITEMS] = {
    "Back to Main Menu"};


const int ir_NUM_SUBMENU_ITEMS = 4;
const char *ir_submenu_items[ir_NUM_SUBMENU_ITEMS] = {
    "TV-B-Gone",
    "IR Receiver",
    "IR Replay",
    "Back to Main Menu"};
    
int current_submenu_index = 0;
bool in_sub_menu = false;

const char **active_submenu_items = nullptr;
int active_submenu_size = 0;

// Scroll state — when submenu has more items than fit on screen.
// Screen height 320, status bar ~37 px, each row 30 px → 9 visible.
#define MAX_VISIBLE_SUBMENU_ITEMS 9
int submenu_view_offset = 0;


const unsigned char *wifi_submenu_icons[NUM_SUBMENU_ITEMS] = {
    bitmap_icon_wifi,         // Packet Monitor
    bitmap_icon_antenna,      // Beacon Spammer
    bitmap_icon_wifi_jammer,  // WiFi Deauther
    bitmap_icon_eye2,         // Deauth Detector
    bitmap_icon_jammer,       // WiFi Scanner
    bitmap_icon_bash,         // Captive Portal
    bitmap_icon_eye,          // Flock Detector
    bitmap_icon_graph,        // Pwnagotchi Detector
    bitmap_icon_no_signal,    // Pineapple Detector
    bitmap_icon_signals,      // Probe Sniffer
    bitmap_icon_list,         // Espressif Detector
    bitmap_icon_eye2,         // Evil Portal Detector
    bitmap_icon_random,       // Multi-SSID Detector
    bitmap_icon_scanner,      // AP Scan & Select
    bitmap_icon_nuke,         // Targeted Deauth
    bitmap_icon_flash,        // WPA3 SAE Flood
    bitmap_icon_wifi_jammer,  // CSA Attack
    bitmap_icon_eye,          // Camera Scanner
    bitmap_icon_signals,      // Probe Req Flood
    bitmap_icon_graph,        // ESPPwnagotchi
    bitmap_icon_bash,         // EvilPortal+Deauth
    bitmap_icon_random,       // Brucegotchi
    bitmap_icon_eye2,         // Hidden SSID
    bitmap_icon_go_back
};

const unsigned char *bluetooth_submenu_icons[bluetooth_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_ble_jammer,  // BLE Jammer
    bitmap_icon_spoofer,     // BLE Spoofer
    bitmap_icon_apple,       // Sour Apple
    bitmap_icon_analyzer,    // Analyzer
    bitmap_icon_graph,       // BLE Scanner
    bitmap_icon_eye,         // Flipper Detector
    bitmap_icon_apple,       // AirTag Detector
    bitmap_icon_nuke,        // Skimmer Detector
    bitmap_icon_eye2,        // Ray-Ban Detector
    bitmap_icon_go_back
};

const unsigned char *nrf_submenu_icons[nrf_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_scanner,     // Scanner
    bitmap_icon_kill,        // Proto Kill
    bitmap_icon_eye2,        // Mousejack Scan
    bitmap_icon_signals,     // Promisc Sniffer
    bitmap_icon_graph,       // Channel Heatmap
    bitmap_icon_no_signal,   // Const Carrier
    bitmap_icon_signals,     // Wardrive (GPS)
    bitmap_icon_apple,       // BLE Adv Monitor
    bitmap_icon_flash,       // Capture+Replay
    bitmap_icon_random,      // Tri-Radio Watch
    bitmap_icon_jammer,      // Intermittent Jammer
    bitmap_icon_no_signal,   // Mode Jammer
    bitmap_icon_go_back
};

const unsigned char *subghz_submenu_icons[subghz_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_antenna,   // Replay Attack
    bitmap_icon_no_signal, // SubGHz Jammer
    bitmap_icon_list,      // Saved Profile
    bitmap_icon_signals,   // Wardrive (GPS)
    bitmap_icon_graph,     // Spectrum Analyzer
    bitmap_icon_eye2,      // Freq Detector
    bitmap_icon_flash,     // Replay .sub
    bitmap_icon_jammer,    // Sweep Jammer
    bitmap_icon_eye,       // Record .sub
    bitmap_icon_key,       // Send Code
    bitmap_icon_kill,      // Send KeeLoq
    bitmap_icon_go_back
};

const unsigned char *tools_submenu_icons[tools_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_sdcard,   // Format SD
    bitmap_icon_signals,  // GPS Status
    bitmap_icon_list,     // SD Browser
    bitmap_icon_go_back
};

const unsigned char *settings_submenu_icons[settings_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_signals,    // Wardrive Channels
    bitmap_icon_go_back
};

const unsigned char *about_submenu_icons[about_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_go_back
};

const unsigned char *ir_submenu_icons[ir_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_flash,     // TV-B-Gone
    bitmap_icon_eye,       // IR Receiver
    bitmap_icon_signals,   // IR Replay
    bitmap_icon_go_back
};


const unsigned char **active_submenu_icons = nullptr;

void updateActiveSubmenu() {
    submenu_view_offset = 0;  // start a fresh submenu at the top
    switch (current_menu_index) {
        case 0: // WiFi
            active_submenu_items = submenu_items;
            active_submenu_size = NUM_SUBMENU_ITEMS;
            active_submenu_icons = wifi_submenu_icons;
            break;
        case 1: // Bluetooth
            active_submenu_items = bluetooth_submenu_items;
            active_submenu_size = bluetooth_NUM_SUBMENU_ITEMS;
            active_submenu_icons = bluetooth_submenu_icons;
            break;
        case 2: // 2.4GHz (NRF)
            active_submenu_items = nrf_submenu_items;
            active_submenu_size = nrf_NUM_SUBMENU_ITEMS;
            active_submenu_icons = nrf_submenu_icons;
            break;
        case 3: // SubGHz
            active_submenu_items = subghz_submenu_items;
            active_submenu_size = subghz_NUM_SUBMENU_ITEMS;
            active_submenu_icons = subghz_submenu_icons;
            break;
        case 4: // Tools
            active_submenu_items = tools_submenu_items;
            active_submenu_size = tools_NUM_SUBMENU_ITEMS;
            active_submenu_icons = tools_submenu_icons;
            break;
        case 5: // Settings — placeholder; real options land as we ship them
            active_submenu_items = settings_submenu_items;
            active_submenu_size = settings_NUM_SUBMENU_ITEMS;
            active_submenu_icons = settings_submenu_icons;
            break;
        case 6: // About
            active_submenu_items = about_submenu_items;
            active_submenu_size = about_NUM_SUBMENU_ITEMS;
            active_submenu_icons = about_submenu_icons;
            break;
        case 7: // IR
            active_submenu_items = ir_submenu_items;
            active_submenu_size = ir_NUM_SUBMENU_ITEMS;
            active_submenu_icons = ir_submenu_icons;
            break;

        default:
            active_submenu_items = nullptr;
            active_submenu_size = 0;
            active_submenu_icons = nullptr;
            break;
    }
}

bool isButtonPressed(int buttonPin) {
  return !pcf.digitalRead(buttonPin);
}

float currentBatteryVoltage = readBatteryVoltage();
unsigned long last_interaction_time = 0;


/*
#define BACKLIGHT_PIN 4

const unsigned long BACKLIGHT_TIMEOUT = 100000;

void manageBacklight() {
  if (millis() - last_interaction_time > BACKLIGHT_TIMEOUT) {
    digitalWrite(BACKLIGHT_PIN, LOW);
  } else {
    digitalWrite(BACKLIGHT_PIN, HIGH);
  }
}
*/


int last_submenu_index = -1;
bool submenu_initialized = false;
int last_menu_index = -1;
bool menu_initialized = false;


void displaySubmenu() {
    menu_initialized = false;
    last_menu_index = -1;

    tft.setTextFont(2);
    tft.setTextSize(1);

    // Auto-scroll the view to keep current item visible.
    int old_offset = submenu_view_offset;
    if (current_submenu_index < submenu_view_offset) {
        submenu_view_offset = current_submenu_index;
    } else if (current_submenu_index >= submenu_view_offset + MAX_VISIBLE_SUBMENU_ITEMS) {
        submenu_view_offset = current_submenu_index - MAX_VISIBLE_SUBMENU_ITEMS + 1;
    }
    int max_offset = active_submenu_size - MAX_VISIBLE_SUBMENU_ITEMS;
    if (max_offset < 0) max_offset = 0;
    if (submenu_view_offset > max_offset) submenu_view_offset = max_offset;
    if (submenu_view_offset < 0) submenu_view_offset = 0;
    if (old_offset != submenu_view_offset) submenu_initialized = false;

    int visible = active_submenu_size - submenu_view_offset;
    if (visible > MAX_VISIBLE_SUBMENU_ITEMS) visible = MAX_VISIBLE_SUBMENU_ITEMS;

    if (!submenu_initialized) {
        tft.fillScreen(TFT_BLACK);

        for (int slot = 0; slot < visible; slot++) {
            int i = submenu_view_offset + slot;
            int yPos = 30 + slot * 30;

            tft.setTextColor(TFT_WHITE, TFT_BLACK);
            tft.drawBitmap(10, yPos, active_submenu_icons[i], 16, 16, TFT_WHITE);
            tft.setCursor(30, yPos);
            tft.print("| ");
            tft.print(active_submenu_items[i]);
        }

        // Scroll-indicator chevrons on the right edge
        tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
        if (submenu_view_offset > 0) {
            tft.setCursor(220, 30);
            tft.print("^");
        }
        if (submenu_view_offset + visible < active_submenu_size) {
            tft.setCursor(220, 30 + (visible - 1) * 30);
            tft.print("v");
        }

        submenu_initialized = true;
        last_submenu_index = -1;
    }

    if (last_submenu_index != current_submenu_index) {
        // Repaint the previous (now unselected) item in white if it's visible
        if (last_submenu_index >= submenu_view_offset &&
            last_submenu_index < submenu_view_offset + visible) {
            int prev_slot = last_submenu_index - submenu_view_offset;
            int prev_yPos = 30 + prev_slot * 30;
            tft.setTextColor(TFT_WHITE, TFT_BLACK);
            tft.drawBitmap(10, prev_yPos, active_submenu_icons[last_submenu_index], 16, 16, TFT_WHITE);
            tft.setCursor(30, prev_yPos);
            tft.print("| ");
            tft.print(active_submenu_items[last_submenu_index]);
        }

        int new_slot = current_submenu_index - submenu_view_offset;
        int new_yPos = 30 + new_slot * 30;
        tft.setTextColor(ORANGE, TFT_BLACK);
        tft.drawBitmap(10, new_yPos, active_submenu_icons[current_submenu_index], 16, 16, ORANGE);
        tft.setCursor(30, new_yPos);
        tft.print("| ");
        tft.print(active_submenu_items[current_submenu_index]);

        last_submenu_index = current_submenu_index;
    }

    drawStatusBar(currentBatteryVoltage, true);
}

const int COLUMN_WIDTH = 120;  
const int X_OFFSET_LEFT = 10;  
const int X_OFFSET_RIGHT = X_OFFSET_LEFT + COLUMN_WIDTH;  
const int Y_START = 30;        
const int Y_SPACING = 75;   

void displayMenu() {
    // Stop any leftover radio activity from the previous feature so the next
    // feature's setup starts clean. Both cleanups are no-ops on cold boot
    // (BT controller idle / WiFi not initialized).
    bleFeatureCleanup();
    wifiFeatureCleanup();

const uint16_t icon_colors[NUM_MENU_ITEMS] = {
  0xFFFF, // WiFi
  0xFFFF, // Bluetooth
  0xFFFF, // 2.4GHz
  0xFFFF, // SubGHz
  0xFFFF, // Tools
  0x8410, // Setting
  0xFFFF, // About
  0xFFFF  // IR
};

    if (current_menu_index < 0 || current_menu_index >= NUM_MENU_ITEMS) current_menu_index = 0;
    if (last_menu_index >= NUM_MENU_ITEMS) last_menu_index = -1;

    submenu_initialized = false;
    last_submenu_index = -1;
    tft.setTextFont(2);

    if (!menu_initialized) {
        tft.fillScreen(0x20e4);

        for (int i = 0; i < NUM_MENU_ITEMS; i++) {
            int column = i / 4; 
            int row = i % 4; 
            int x_position = (column == 0) ? X_OFFSET_LEFT : X_OFFSET_RIGHT;
            int y_position = Y_START + row * Y_SPACING;

            tft.fillRoundRect(x_position, y_position, 100, 60, 5, TFT_DARKBLUE); 
            tft.drawRoundRect(x_position, y_position, 100, 60, 5, TFT_GRAY); 
            tft.drawBitmap(x_position + 42, y_position + 10, bitmap_icons[i], 16, 16, icon_colors[i]);

            tft.setTextColor(TFTWHITE, TFT_DARKBLUE);
            int textWidth = 6 * strlen(menu_items[i]); 
            int textX = x_position + (100 - textWidth) / 2; 
            int textY = y_position + 30; 
            tft.setCursor(textX, textY);
            tft.print(menu_items[i]);
        }
        menu_initialized = true;
        last_menu_index = -1;
    }

    if (last_menu_index != current_menu_index) {
        for (int i = 0; i < NUM_MENU_ITEMS; i++) {
            int column = i / 4;
            int row = i % 4;
            int x_position = (column == 0) ? X_OFFSET_LEFT : X_OFFSET_RIGHT;
            int y_position = Y_START + row * Y_SPACING;

            if (i == last_menu_index) { 
                tft.fillRoundRect(x_position, y_position, 100, 60, 5, TFT_DARKBLUE); 
                tft.drawRoundRect(x_position, y_position, 100, 60, 5, TFT_GRAY); 
                tft.setTextColor(TFTWHITE, TFT_DARKBLUE);
                tft.drawBitmap(x_position + 42, y_position + 10, bitmap_icons[last_menu_index], 16, 16, icon_colors[last_menu_index]); 
                int textWidth = 6 * strlen(menu_items[last_menu_index]); 
                int textX = x_position + (100 - textWidth) / 2;
                int textY = y_position + 30;
                tft.setCursor(textX, textY);
                tft.print(menu_items[last_menu_index]);
            }
        }

        int column = current_menu_index / 4;
        int row = current_menu_index % 4;
        int x_position = (column == 0) ? X_OFFSET_LEFT : X_OFFSET_RIGHT;
        int y_position = Y_START + row * Y_SPACING;

        tft.fillRoundRect(x_position, y_position, 100, 60, 5, TFT_DARKBLUE); 
        tft.drawRoundRect(x_position, y_position, 100, 60, 5, ORANGE); 

        tft.setTextColor(ORANGE, TFT_DARKBLUE);
        tft.drawBitmap(x_position + 42, y_position + 10, bitmap_icons[current_menu_index], 16, 16, SELECTED_ICON_COLOR); 
        int textWidth = 6 * strlen(menu_items[current_menu_index]); 
        int textX = x_position + (100 - textWidth) / 2;
        int textY = y_position + 30;
        tft.setCursor(textX, textY);
        tft.print(menu_items[current_menu_index]);

        last_menu_index = current_menu_index;
    }
    drawStatusBar(currentBatteryVoltage, true);
}


void handleWiFiSubmenuButtons() {
    if (isButtonPressed(BTN_UP)) {
        current_submenu_index = (current_submenu_index - 1 + active_submenu_size) % active_submenu_size;
        if (current_submenu_index < 0) {
            current_submenu_index = NUM_SUBMENU_ITEMS - 1;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_DOWN)) {
        current_submenu_index = (current_submenu_index + 1) % active_submenu_size;
        if (current_submenu_index >= NUM_SUBMENU_ITEMS) {
            current_submenu_index = 0;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_SELECT)) {
        last_interaction_time = millis();
        delay(200);

        if (current_submenu_index == 23) {  // Back to Main Menu
            { uint32_t _dStart = millis(); while (isButtonPressed(BTN_SELECT) && millis() - _dStart < 400) delay(5); }
            in_sub_menu = false;
            is_main_menu = false;
            feature_active = false;
            feature_exit_requested = false;
            displayMenu();
            return;
        }

        if (current_submenu_index == 22) {  // Hidden SSID Reveal
            in_sub_menu = true; feature_active = true; feature_exit_requested = false;
            HiddenReveal::hiddenRevealSetup();
            while (current_submenu_index == 22 && !feature_exit_requested) {
                in_sub_menu = true;
                HiddenReveal::hiddenRevealLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true; is_main_menu = false;
                    submenu_initialized = false; feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu(); delay(200);
                    while (isButtonPressed(BTN_SELECT)) {}
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true; is_main_menu = false;
                submenu_initialized = false; feature_active = false;
                feature_exit_requested = false;
                displaySubmenu(); delay(200);
            }
        }

        if (current_submenu_index == 19) {  // ESPPwnagotchi
            in_sub_menu = true; feature_active = true; feature_exit_requested = false;
            EspPwnagotchi::pwnSetup();
            while (current_submenu_index == 19 && !feature_exit_requested) {
                in_sub_menu = true;
                EspPwnagotchi::pwnLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true; is_main_menu = false;
                    submenu_initialized = false; feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu(); delay(200);
                    while (isButtonPressed(BTN_SELECT)) {}
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true; is_main_menu = false;
                submenu_initialized = false; feature_active = false;
                feature_exit_requested = false;
                displaySubmenu(); delay(200);
            }
        }

        if (current_submenu_index == 20) {  // EvilPortal+Deauth
            in_sub_menu = true; feature_active = true; feature_exit_requested = false;
            EvilDeauth::evilDeauthSetup();
            while (current_submenu_index == 20 && !feature_exit_requested) {
                in_sub_menu = true;
                EvilDeauth::evilDeauthLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true; is_main_menu = false;
                    submenu_initialized = false; feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu(); delay(200);
                    while (isButtonPressed(BTN_SELECT)) {}
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true; is_main_menu = false;
                submenu_initialized = false; feature_active = false;
                feature_exit_requested = false;
                displaySubmenu(); delay(200);
            }
        }

        if (current_submenu_index == 21) {  // Brucegotchi
            in_sub_menu = true; feature_active = true; feature_exit_requested = false;
            Brucegotchi::brucegotchiSetup();
            while (current_submenu_index == 21 && !feature_exit_requested) {
                in_sub_menu = true;
                Brucegotchi::brucegotchiLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true; is_main_menu = false;
                    submenu_initialized = false; feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu(); delay(200);
                    while (isButtonPressed(BTN_SELECT)) {}
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true; is_main_menu = false;
                submenu_initialized = false; feature_active = false;
                feature_exit_requested = false;
                displaySubmenu(); delay(200);
            }
        }

        if (current_submenu_index == 18) {  // Probe Req Flood
            in_sub_menu = true; feature_active = true; feature_exit_requested = false;
            ProbeFlood::probeFloodSetup();
            while (current_submenu_index == 18 && !feature_exit_requested) {
                in_sub_menu = true;
                ProbeFlood::probeFloodLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true; is_main_menu = false;
                    submenu_initialized = false; feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu(); delay(200);
                    while (isButtonPressed(BTN_SELECT)) {}
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true; is_main_menu = false;
                submenu_initialized = false; feature_active = false;
                feature_exit_requested = false;
                displaySubmenu(); delay(200);
            }
        }

        if (current_submenu_index == 17) {  // Camera Scanner
            in_sub_menu = true; feature_active = true; feature_exit_requested = false;
            CameraScanner::cameraScannerSetup();
            while (current_submenu_index == 17 && !feature_exit_requested) {
                in_sub_menu = true;
                CameraScanner::cameraScannerLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true; is_main_menu = false;
                    submenu_initialized = false; feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu(); delay(200);
                    while (isButtonPressed(BTN_SELECT)) {}
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true; is_main_menu = false;
                submenu_initialized = false; feature_active = false;
                feature_exit_requested = false;
                displaySubmenu(); delay(200);
            }
        }

        if (current_submenu_index == 0) {
            current_submenu_index = 0;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            PacketMonitor::ptmSetup();
            while (current_submenu_index == 0 && !feature_exit_requested) {  
                current_submenu_index = 0;
                in_sub_menu = true;
                PacketMonitor::ptmLoop();        
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false; 
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false; 
                    displaySubmenu();           
                    delay(200);            
                    while (isButtonPressed(BTN_SELECT)) {
                    }           
                    break;  
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false; 
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false; 
                displaySubmenu(); 
                delay(200);
            }
        }

        if (current_submenu_index == 1) {
            current_submenu_index = 1;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false; 
            BeaconSpammer::beaconSpamSetup();   
            while (current_submenu_index == 1 && !feature_exit_requested) {  
                current_submenu_index = 1;
                in_sub_menu = true;
                BeaconSpammer::beaconSpamLoop();        
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false; 
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false; 
                    displaySubmenu(); 
                    delay(200);            
                    while (isButtonPressed(BTN_SELECT)) {
                    }           
                    break;  
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false; 
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false; 
                displaySubmenu(); 
                delay(200);
            }
        }

        if (current_submenu_index == 2) {
            current_submenu_index = 2;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false; 
            Deauther::deautherSetup();  
            while (current_submenu_index == 2 && !feature_exit_requested) {  
                current_submenu_index = 2;
                in_sub_menu = true;
                Deauther::deautherLoop();       
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false; 
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false; 
                    displaySubmenu(); 
                    delay(200);            
                    while (isButtonPressed(BTN_SELECT)) {
                    }           
                    break;  
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false; 
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false; 
                displaySubmenu(); 
                delay(200);
            }
        }

        if (current_submenu_index == 3) {
            current_submenu_index = 3;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false; 
            DeauthDetect::deauthdetectSetup();   
            while (current_submenu_index == 3 && !feature_exit_requested) {  
                current_submenu_index = 3;
                in_sub_menu = true;
                DeauthDetect::deauthdetectLoop();        
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false; 
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false; 
                    displaySubmenu(); 
                    delay(200);            
                    while (isButtonPressed(BTN_SELECT)) {
                    }           
                    break;  
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false; 
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false; 
                displaySubmenu(); 
                delay(200);
            }
        }

        if (current_submenu_index == 4) {
            current_submenu_index = 4;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false; 
            WifiScan::wifiscanSetup();   
            while (current_submenu_index == 4 && !feature_exit_requested) {  
                current_submenu_index = 4;
                in_sub_menu = true;
                WifiScan::wifiscanLoop();       
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false; 
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false; 
                    displaySubmenu(); 
                    delay(200);            
                    while (isButtonPressed(BTN_SELECT)) {
                    }           
                    break;  
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false; 
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false; 
                displaySubmenu(); 
                delay(200);
            }
        }


        if (current_submenu_index == 5) {
            current_submenu_index = 5;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            CaptivePortal::cportalSetup();
            while (current_submenu_index == 5 && !feature_exit_requested) {
                current_submenu_index = 5;
                in_sub_menu = true;
                CaptivePortal::cportalLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 6) {
            current_submenu_index = 6;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            FlockDetect::flockDetectSetup();
            while (current_submenu_index == 6 && !feature_exit_requested) {
                current_submenu_index = 6;
                in_sub_menu = true;
                FlockDetect::flockDetectLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 7) {
            current_submenu_index = 7;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            PwnDetect::pwnDetectSetup();
            while (current_submenu_index == 7 && !feature_exit_requested) {
                current_submenu_index = 7;
                in_sub_menu = true;
                PwnDetect::pwnDetectLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 8) {
            current_submenu_index = 8;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            PineappleDetect::pineappleDetectSetup();
            while (current_submenu_index == 8 && !feature_exit_requested) {
                current_submenu_index = 8;
                in_sub_menu = true;
                PineappleDetect::pineappleDetectLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 9) {
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            ProbeDetect::probeDetectSetup();
            while (current_submenu_index == 9 && !feature_exit_requested) {
                in_sub_menu = true;
                ProbeDetect::probeDetectLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true; is_main_menu = false;
                    submenu_initialized = false; feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu(); delay(200);
                    while (isButtonPressed(BTN_SELECT)) {}
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true; is_main_menu = false;
                submenu_initialized = false; feature_active = false;
                feature_exit_requested = false;
                displaySubmenu(); delay(200);
            }
        }

        if (current_submenu_index == 10) {
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            EspDetect::espDetectSetup();
            while (current_submenu_index == 10 && !feature_exit_requested) {
                in_sub_menu = true;
                EspDetect::espDetectLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true; is_main_menu = false;
                    submenu_initialized = false; feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu(); delay(200);
                    while (isButtonPressed(BTN_SELECT)) {}
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true; is_main_menu = false;
                submenu_initialized = false; feature_active = false;
                feature_exit_requested = false;
                displaySubmenu(); delay(200);
            }
        }

        if (current_submenu_index == 11) {
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            EvilPortalDetect::evilPortalDetectSetup();
            while (current_submenu_index == 11 && !feature_exit_requested) {
                in_sub_menu = true;
                EvilPortalDetect::evilPortalDetectLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true; is_main_menu = false;
                    submenu_initialized = false; feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu(); delay(200);
                    while (isButtonPressed(BTN_SELECT)) {}
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true; is_main_menu = false;
                submenu_initialized = false; feature_active = false;
                feature_exit_requested = false;
                displaySubmenu(); delay(200);
            }
        }

        if (current_submenu_index == 12) {
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            MultiSsidDetect::multiSsidDetectSetup();
            while (current_submenu_index == 12 && !feature_exit_requested) {
                in_sub_menu = true;
                MultiSsidDetect::multiSsidDetectLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true; is_main_menu = false;
                    submenu_initialized = false; feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu(); delay(200);
                    while (isButtonPressed(BTN_SELECT)) {}
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true; is_main_menu = false;
                submenu_initialized = false; feature_active = false;
                feature_exit_requested = false;
                displaySubmenu(); delay(200);
            }
        }

        if (current_submenu_index == 13) {
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            ApScan::apScanSetup();
            while (current_submenu_index == 13 && !feature_exit_requested) {
                in_sub_menu = true;
                ApScan::apScanLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true; is_main_menu = false;
                    submenu_initialized = false; feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu(); delay(200);
                    while (isButtonPressed(BTN_SELECT)) {}
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true; is_main_menu = false;
                submenu_initialized = false; feature_active = false;
                feature_exit_requested = false;
                displaySubmenu(); delay(200);
            }
        }

        if (current_submenu_index == 14) {
            in_sub_menu = true; feature_active = true; feature_exit_requested = false;
            DeauthAttack::deauthAttackSetup();
            while (current_submenu_index == 14 && !feature_exit_requested) {
                in_sub_menu = true;
                DeauthAttack::deauthAttackLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true; is_main_menu = false;
                    submenu_initialized = false; feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu(); delay(200);
                    while (isButtonPressed(BTN_SELECT)) {}
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true; is_main_menu = false;
                submenu_initialized = false; feature_active = false;
                feature_exit_requested = false;
                displaySubmenu(); delay(200);
            }
        }

        if (current_submenu_index == 15) {
            in_sub_menu = true; feature_active = true; feature_exit_requested = false;
            SaeAttack::saeAttackSetup();
            while (current_submenu_index == 15 && !feature_exit_requested) {
                in_sub_menu = true;
                SaeAttack::saeAttackLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true; is_main_menu = false;
                    submenu_initialized = false; feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu(); delay(200);
                    while (isButtonPressed(BTN_SELECT)) {}
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true; is_main_menu = false;
                submenu_initialized = false; feature_active = false;
                feature_exit_requested = false;
                displaySubmenu(); delay(200);
            }
        }

        if (current_submenu_index == 16) {
            in_sub_menu = true; feature_active = true; feature_exit_requested = false;
            CsaAttack::csaAttackSetup();
            while (current_submenu_index == 16 && !feature_exit_requested) {
                in_sub_menu = true;
                CsaAttack::csaAttackLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true; is_main_menu = false;
                    submenu_initialized = false; feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu(); delay(200);
                    while (isButtonPressed(BTN_SELECT)) {}
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true; is_main_menu = false;
                submenu_initialized = false; feature_active = false;
                feature_exit_requested = false;
                displaySubmenu(); delay(200);
            }
        }
    }

    if (ts.touched() && !feature_active) {
        TS_Point p = ts.getPoint();
        delay(10);

        int x, y, z;
        x = ::map(p.x, TS_MINX, TS_MAXX, 0, 239);
        y = ::map(p.y, TS_MAXY, TS_MINY, 0, 319);

        int _touch_visible = active_submenu_size - submenu_view_offset;
        if (_touch_visible > MAX_VISIBLE_SUBMENU_ITEMS) _touch_visible = MAX_VISIBLE_SUBMENU_ITEMS;
        for (int _slot = 0; _slot < _touch_visible; _slot++) {
            int i = submenu_view_offset + _slot;
            int yPos = 30 + _slot * 30;

            int button_x1 = 10;
            int button_y1 = yPos;
            int button_x2 = 110;
            int button_y2 = yPos + 30;

            if (x >= button_x1 && x <= button_x2 && y >= button_y1 && y <= button_y2) {
                current_submenu_index = i;
                last_interaction_time = millis();
                displaySubmenu();
                delay(200);

                if (current_submenu_index == 23) {  // Back to Main Menu
                    in_sub_menu = false;
                    is_main_menu = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displayMenu();
                    return;
                } else if (current_submenu_index == 22) {  // Hidden SSID Reveal
                    in_sub_menu = true; feature_active = true; feature_exit_requested = false;
                    HiddenReveal::hiddenRevealSetup();
                    while (current_submenu_index == 22 && !feature_exit_requested) {
                        in_sub_menu = true;
                        HiddenReveal::hiddenRevealLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true; is_main_menu = false;
                            submenu_initialized = false; feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu(); delay(200);
                            while (isButtonPressed(BTN_SELECT)) {}
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true; is_main_menu = false;
                        submenu_initialized = false; feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu(); delay(200);
                    }
                } else if (current_submenu_index == 19) {  // ESPPwnagotchi
                    in_sub_menu = true; feature_active = true; feature_exit_requested = false;
                    EspPwnagotchi::pwnSetup();
                    while (current_submenu_index == 19 && !feature_exit_requested) {
                        in_sub_menu = true;
                        EspPwnagotchi::pwnLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true; is_main_menu = false;
                            submenu_initialized = false; feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu(); delay(200);
                            while (isButtonPressed(BTN_SELECT)) {}
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true; is_main_menu = false;
                        submenu_initialized = false; feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu(); delay(200);
                    }
                } else if (current_submenu_index == 20) {  // EvilPortal+Deauth
                    in_sub_menu = true; feature_active = true; feature_exit_requested = false;
                    EvilDeauth::evilDeauthSetup();
                    while (current_submenu_index == 20 && !feature_exit_requested) {
                        in_sub_menu = true;
                        EvilDeauth::evilDeauthLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true; is_main_menu = false;
                            submenu_initialized = false; feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu(); delay(200);
                            while (isButtonPressed(BTN_SELECT)) {}
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true; is_main_menu = false;
                        submenu_initialized = false; feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu(); delay(200);
                    }
                } else if (current_submenu_index == 21) {  // Brucegotchi
                    in_sub_menu = true; feature_active = true; feature_exit_requested = false;
                    Brucegotchi::brucegotchiSetup();
                    while (current_submenu_index == 21 && !feature_exit_requested) {
                        in_sub_menu = true;
                        Brucegotchi::brucegotchiLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true; is_main_menu = false;
                            submenu_initialized = false; feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu(); delay(200);
                            while (isButtonPressed(BTN_SELECT)) {}
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true; is_main_menu = false;
                        submenu_initialized = false; feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu(); delay(200);
                    }
                } else if (current_submenu_index == 18) {  // Probe Req Flood
                    in_sub_menu = true; feature_active = true; feature_exit_requested = false;
                    ProbeFlood::probeFloodSetup();
                    while (current_submenu_index == 18 && !feature_exit_requested) {
                        in_sub_menu = true;
                        ProbeFlood::probeFloodLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true; is_main_menu = false;
                            submenu_initialized = false; feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu(); delay(200);
                            while (isButtonPressed(BTN_SELECT)) {}
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true; is_main_menu = false;
                        submenu_initialized = false; feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu(); delay(200);
                    }
                } else if (current_submenu_index == 17) {  // Camera Scanner
                    in_sub_menu = true; feature_active = true; feature_exit_requested = false;
                    CameraScanner::cameraScannerSetup();
                    while (current_submenu_index == 17 && !feature_exit_requested) {
                        in_sub_menu = true;
                        CameraScanner::cameraScannerLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true; is_main_menu = false;
                            submenu_initialized = false; feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu(); delay(200);
                            while (isButtonPressed(BTN_SELECT)) {}
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true; is_main_menu = false;
                        submenu_initialized = false; feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu(); delay(200);
                    }
                } else if (current_submenu_index == 0) {
                    current_submenu_index = 0;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    PacketMonitor::ptmSetup();
                    while (current_submenu_index == 0 && !feature_exit_requested) {  
                        current_submenu_index = 0;
                        in_sub_menu = true;
                        PacketMonitor::ptmLoop();        
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false; 
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false; 
                            displaySubmenu();           
                            delay(200);            
                            while (isButtonPressed(BTN_SELECT)) {
                            }           
                            break;  
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false; 
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false; 
                        displaySubmenu(); 
                        delay(200);
                    }
                } else if (current_submenu_index == 1) {
                    current_submenu_index = 1;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false; 
                    BeaconSpammer::beaconSpamSetup();   
                    while (current_submenu_index == 1 && !feature_exit_requested) {  
                        current_submenu_index = 1;
                        in_sub_menu = true;
                        BeaconSpammer::beaconSpamLoop();        
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false; 
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false; 
                            displaySubmenu(); 
                            delay(200);            
                            while (isButtonPressed(BTN_SELECT)) {
                            }           
                            break;  
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false; 
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false; 
                        displaySubmenu(); 
                        delay(200);
                    }
                } else if (current_submenu_index == 2) {
                    current_submenu_index = 2;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false; 
                    Deauther::deautherSetup();   
                    while (current_submenu_index == 2 && !feature_exit_requested) {  
                        current_submenu_index = 2;
                        in_sub_menu = true;
                        Deauther::deautherLoop();       
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false; 
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false; 
                            displaySubmenu(); 
                            delay(200);            
                            while (isButtonPressed(BTN_SELECT)) {
                            }           
                            break;  
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false; 
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false; 
                        displaySubmenu(); 
                        delay(200);
                    }
                } else if (current_submenu_index == 3) {
                    current_submenu_index = 3;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false; 
                    DeauthDetect::deauthdetectSetup();   
                    while (current_submenu_index == 3 && !feature_exit_requested) {  
                        current_submenu_index = 3;
                        in_sub_menu = true;
                        DeauthDetect::deauthdetectLoop();        
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false; 
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false; 
                            displaySubmenu(); 
                            delay(200);            
                            while (isButtonPressed(BTN_SELECT)) {
                            }           
                            break;  
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false; 
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false; 
                        displaySubmenu(); 
                        delay(200);
                    }
                } else if (current_submenu_index == 4) {
                    current_submenu_index = 4;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false; 
                    WifiScan::wifiscanSetup();   
                    while (current_submenu_index == 4 && !feature_exit_requested) {  
                        current_submenu_index = 4;
                        in_sub_menu = true;
                        WifiScan::wifiscanLoop();       
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false; 
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false; 
                            displaySubmenu(); 
                            delay(200);            
                            while (isButtonPressed(BTN_SELECT)) {
                            }           
                            break;  
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false; 
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false; 
                        displaySubmenu(); 
                        delay(200);
                    }
                } else if (current_submenu_index == 5) {
                    current_submenu_index = 5;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    CaptivePortal::cportalSetup();
                    while (current_submenu_index == 5 && !feature_exit_requested) {
                        current_submenu_index = 5;
                        in_sub_menu = true;
                        CaptivePortal::cportalLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 6) {
                    current_submenu_index = 6;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    FlockDetect::flockDetectSetup();
                    while (current_submenu_index == 6 && !feature_exit_requested) {
                        current_submenu_index = 6;
                        in_sub_menu = true;
                        FlockDetect::flockDetectLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 7) {
                    current_submenu_index = 7;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    PwnDetect::pwnDetectSetup();
                    while (current_submenu_index == 7 && !feature_exit_requested) {
                        current_submenu_index = 7;
                        in_sub_menu = true;
                        PwnDetect::pwnDetectLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 8) {
                    current_submenu_index = 8;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    PineappleDetect::pineappleDetectSetup();
                    while (current_submenu_index == 8 && !feature_exit_requested) {
                        current_submenu_index = 8;
                        in_sub_menu = true;
                        PineappleDetect::pineappleDetectLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 9) {
                    in_sub_menu = true; feature_active = true; feature_exit_requested = false;
                    ProbeDetect::probeDetectSetup();
                    while (current_submenu_index == 9 && !feature_exit_requested) {
                        in_sub_menu = true;
                        ProbeDetect::probeDetectLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true; is_main_menu = false;
                            submenu_initialized = false; feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu(); delay(200);
                            while (isButtonPressed(BTN_SELECT)) {}
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true; is_main_menu = false;
                        submenu_initialized = false; feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu(); delay(200);
                    }
                } else if (current_submenu_index == 10) {
                    in_sub_menu = true; feature_active = true; feature_exit_requested = false;
                    EspDetect::espDetectSetup();
                    while (current_submenu_index == 10 && !feature_exit_requested) {
                        in_sub_menu = true;
                        EspDetect::espDetectLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true; is_main_menu = false;
                            submenu_initialized = false; feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu(); delay(200);
                            while (isButtonPressed(BTN_SELECT)) {}
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true; is_main_menu = false;
                        submenu_initialized = false; feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu(); delay(200);
                    }
                } else if (current_submenu_index == 11) {
                    in_sub_menu = true; feature_active = true; feature_exit_requested = false;
                    EvilPortalDetect::evilPortalDetectSetup();
                    while (current_submenu_index == 11 && !feature_exit_requested) {
                        in_sub_menu = true;
                        EvilPortalDetect::evilPortalDetectLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true; is_main_menu = false;
                            submenu_initialized = false; feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu(); delay(200);
                            while (isButtonPressed(BTN_SELECT)) {}
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true; is_main_menu = false;
                        submenu_initialized = false; feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu(); delay(200);
                    }
                } else if (current_submenu_index == 12) {
                    in_sub_menu = true; feature_active = true; feature_exit_requested = false;
                    MultiSsidDetect::multiSsidDetectSetup();
                    while (current_submenu_index == 12 && !feature_exit_requested) {
                        in_sub_menu = true;
                        MultiSsidDetect::multiSsidDetectLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true; is_main_menu = false;
                            submenu_initialized = false; feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu(); delay(200);
                            while (isButtonPressed(BTN_SELECT)) {}
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true; is_main_menu = false;
                        submenu_initialized = false; feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu(); delay(200);
                    }
                } else if (current_submenu_index == 13) {
                    in_sub_menu = true; feature_active = true; feature_exit_requested = false;
                    ApScan::apScanSetup();
                    while (current_submenu_index == 13 && !feature_exit_requested) {
                        in_sub_menu = true;
                        ApScan::apScanLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true; is_main_menu = false;
                            submenu_initialized = false; feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu(); delay(200);
                            while (isButtonPressed(BTN_SELECT)) {}
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true; is_main_menu = false;
                        submenu_initialized = false; feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu(); delay(200);
                    }
                } else if (current_submenu_index == 14) {
                    in_sub_menu = true; feature_active = true; feature_exit_requested = false;
                    DeauthAttack::deauthAttackSetup();
                    while (current_submenu_index == 14 && !feature_exit_requested) {
                        in_sub_menu = true;
                        DeauthAttack::deauthAttackLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true; is_main_menu = false;
                            submenu_initialized = false; feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu(); delay(200);
                            while (isButtonPressed(BTN_SELECT)) {}
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true; is_main_menu = false;
                        submenu_initialized = false; feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu(); delay(200);
                    }
                } else if (current_submenu_index == 15) {
                    in_sub_menu = true; feature_active = true; feature_exit_requested = false;
                    SaeAttack::saeAttackSetup();
                    while (current_submenu_index == 15 && !feature_exit_requested) {
                        in_sub_menu = true;
                        SaeAttack::saeAttackLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true; is_main_menu = false;
                            submenu_initialized = false; feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu(); delay(200);
                            while (isButtonPressed(BTN_SELECT)) {}
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true; is_main_menu = false;
                        submenu_initialized = false; feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu(); delay(200);
                    }
                } else if (current_submenu_index == 16) {
                    in_sub_menu = true; feature_active = true; feature_exit_requested = false;
                    CsaAttack::csaAttackSetup();
                    while (current_submenu_index == 16 && !feature_exit_requested) {
                        in_sub_menu = true;
                        CsaAttack::csaAttackLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true; is_main_menu = false;
                            submenu_initialized = false; feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu(); delay(200);
                            while (isButtonPressed(BTN_SELECT)) {}
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true; is_main_menu = false;
                        submenu_initialized = false; feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu(); delay(200);
                    }
                }
                break;
            }
        }
    }
}


void handleBluetoothSubmenuButtons() {
    if (isButtonPressed(BTN_UP)) {
        current_submenu_index = (current_submenu_index - 1 + active_submenu_size) % active_submenu_size;
        if (current_submenu_index < 0) {
            current_submenu_index = NUM_SUBMENU_ITEMS - 1;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_DOWN)) {
        current_submenu_index = (current_submenu_index + 1) % active_submenu_size;
        if (current_submenu_index >= NUM_SUBMENU_ITEMS) {
            current_submenu_index = 0;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_SELECT)) {
        last_interaction_time = millis();
        delay(200);

        if (current_submenu_index == 9) {  // Back to Main Menu
            { uint32_t _dStart = millis(); while (isButtonPressed(BTN_SELECT) && millis() - _dStart < 400) delay(5); }
            in_sub_menu = false;
            is_main_menu = false;
            feature_active = false;
            feature_exit_requested = false;
            displayMenu();
            return;
        }

        if (current_submenu_index == 0) {
            current_submenu_index = 0;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false; 
            BleJammer::blejamSetup();  
            while (current_submenu_index == 0 && !feature_exit_requested) {  
                current_submenu_index = 0;
                in_sub_menu = true;
                BleJammer::blejamLoop();        
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false; 
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false; 
                    displaySubmenu(); 
                    delay(200);            
                    while (isButtonPressed(BTN_SELECT)) {
                    }           
                    break;  
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false; 
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false; 
                displaySubmenu(); 
                delay(200);
            }
        }

        if (current_submenu_index == 1) {
            current_submenu_index = 1;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false; 
            BleSpoofer::spooferSetup();
            while (current_submenu_index == 1 && !feature_exit_requested) {  
                current_submenu_index = 1;
                in_sub_menu = true;
                BleSpoofer::spooferLoop();        
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false; 
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false; 
                    displaySubmenu(); 
                    delay(200);            
                    while (isButtonPressed(BTN_SELECT)) {
                    }           
                    break;  
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false; 
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false; 
                displaySubmenu(); 
                delay(200);
            }
        }

        if (current_submenu_index == 2) {
            current_submenu_index = 2;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false; 
            SourApple::sourappleSetup(); 
            while (current_submenu_index == 2 && !feature_exit_requested) {  
                current_submenu_index = 2;
                in_sub_menu = true;
                SourApple::sourappleLoop();        
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false; 
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false; 
                    displaySubmenu(); 
                    delay(200);            
                    while (isButtonPressed(BTN_SELECT)) {
                    }           
                    break;  
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false; 
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false; 
                displaySubmenu(); 
                delay(200);
            }
        }

        if (current_submenu_index == 3) {
            current_submenu_index = 3;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false; 
            BleSniffer::blesnifferSetup(); 
            while (current_submenu_index == 3 && !feature_exit_requested) {  
                current_submenu_index = 3;
                in_sub_menu = true;
                BleSniffer::blesnifferLoop();        
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false; 
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false; 
                    displaySubmenu(); 
                    delay(200);            
                    while (isButtonPressed(BTN_SELECT)) {
                    }           
                    break;  
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false; 
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false; 
                displaySubmenu(); 
                delay(200);
            }
        }

        if (current_submenu_index == 4) {
            current_submenu_index = 4;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            BleScan::bleScanSetup();
            while (current_submenu_index == 4 && !feature_exit_requested) {
                current_submenu_index = 4;
                in_sub_menu = true;
                BleScan::bleScanLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 5) {
            current_submenu_index = 5;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            FlipperDetect::flipperDetectSetup();
            while (current_submenu_index == 5 && !feature_exit_requested) {
                current_submenu_index = 5;
                in_sub_menu = true;
                FlipperDetect::flipperDetectLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 6) {
            in_sub_menu = true; feature_active = true; feature_exit_requested = false;
            AirTagDetect::airtagDetectSetup();
            while (current_submenu_index == 6 && !feature_exit_requested) {
                in_sub_menu = true;
                AirTagDetect::airtagDetectLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true; is_main_menu = false;
                    submenu_initialized = false; feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu(); delay(200);
                    while (isButtonPressed(BTN_SELECT)) {}
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true; is_main_menu = false;
                submenu_initialized = false; feature_active = false;
                feature_exit_requested = false;
                displaySubmenu(); delay(200);
            }
        }

        if (current_submenu_index == 7) {
            in_sub_menu = true; feature_active = true; feature_exit_requested = false;
            SkimmerDetect::skimmerDetectSetup();
            while (current_submenu_index == 7 && !feature_exit_requested) {
                in_sub_menu = true;
                SkimmerDetect::skimmerDetectLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true; is_main_menu = false;
                    submenu_initialized = false; feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu(); delay(200);
                    while (isButtonPressed(BTN_SELECT)) {}
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true; is_main_menu = false;
                submenu_initialized = false; feature_active = false;
                feature_exit_requested = false;
                displaySubmenu(); delay(200);
            }
        }

        if (current_submenu_index == 8) {
            in_sub_menu = true; feature_active = true; feature_exit_requested = false;
            RaybanDetect::raybanDetectSetup();
            while (current_submenu_index == 8 && !feature_exit_requested) {
                in_sub_menu = true;
                RaybanDetect::raybanDetectLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true; is_main_menu = false;
                    submenu_initialized = false; feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu(); delay(200);
                    while (isButtonPressed(BTN_SELECT)) {}
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true; is_main_menu = false;
                submenu_initialized = false; feature_active = false;
                feature_exit_requested = false;
                displaySubmenu(); delay(200);
            }
        }
    }

    if (ts.touched() && !feature_active) {
        TS_Point p = ts.getPoint();
        delay(10);

        int x, y, z;
        x = ::map(p.x, TS_MINX, TS_MAXX, 0, 239);
        y = ::map(p.y, TS_MAXY, TS_MINY, 0, 319);

        int _touch_visible = active_submenu_size - submenu_view_offset;
        if (_touch_visible > MAX_VISIBLE_SUBMENU_ITEMS) _touch_visible = MAX_VISIBLE_SUBMENU_ITEMS;
        for (int _slot = 0; _slot < _touch_visible; _slot++) {
            int i = submenu_view_offset + _slot;
            int yPos = 30 + _slot * 30;

            int button_x1 = 10;
            int button_y1 = yPos;
            int button_x2 = 110;
            int button_y2 = yPos + 30;

            if (x >= button_x1 && x <= button_x2 && y >= button_y1 && y <= button_y2) {
                current_submenu_index = i;
                last_interaction_time = millis();
                displaySubmenu();
                delay(200);

                if (current_submenu_index == 9) {  // Back to Main Menu
                    in_sub_menu = false;
                    is_main_menu = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displayMenu();
                    return;
                } else if (current_submenu_index == 0) {
                    current_submenu_index = 0;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    BleJammer::blejamSetup();  
                    while (current_submenu_index == 0 && !feature_exit_requested) {  
                        current_submenu_index = 0;
                        in_sub_menu = true;
                        BleJammer::blejamLoop();        
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false; 
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false; 
                            displaySubmenu(); 
                            delay(200);            
                            while (isButtonPressed(BTN_SELECT)) {
                            }           
                            break;  
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false; 
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false; 
                        displaySubmenu(); 
                        delay(200);
                    }
                } else if (current_submenu_index == 1) {
                    current_submenu_index = 1;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false; 
                    BleSpoofer::spooferSetup();
                    while (current_submenu_index == 1 && !feature_exit_requested) {  
                        current_submenu_index = 1;
                        in_sub_menu = true;
                        BleSpoofer::spooferLoop();        
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false; 
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false; 
                            displaySubmenu(); 
                            delay(200);            
                            while (isButtonPressed(BTN_SELECT)) {
                            }           
                            break;  
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false; 
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false; 
                        displaySubmenu(); 
                        delay(200);
                    }
                } else if (current_submenu_index == 2) {
                    current_submenu_index = 2;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false; 
                    SourApple::sourappleSetup(); 
                    while (current_submenu_index == 2 && !feature_exit_requested) {  
                        current_submenu_index = 2;
                        in_sub_menu = true;
                        SourApple::sourappleLoop();        
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false; 
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false; 
                            displaySubmenu(); 
                            delay(200);            
                            while (isButtonPressed(BTN_SELECT)) {
                            }           
                            break;  
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false; 
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false; 
                        displaySubmenu(); 
                        delay(200);
                    }
                } else if (current_submenu_index == 3) {
                    current_submenu_index = 3;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false; 
                    BleSniffer::blesnifferSetup(); 
                    while (current_submenu_index == 3 && !feature_exit_requested) {  
                        current_submenu_index = 3;
                        in_sub_menu = true;
                        BleSniffer::blesnifferLoop();         
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false; 
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false; 
                            displaySubmenu(); 
                            delay(200);            
                            while (isButtonPressed(BTN_SELECT)) {
                            }           
                            break;  
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false; 
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false; 
                        displaySubmenu(); 
                        delay(200);
                    }
                } else if (current_submenu_index == 4) {
                    current_submenu_index = 4;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    BleScan::bleScanSetup();
                    while (current_submenu_index == 4 && !feature_exit_requested) {
                        current_submenu_index = 4;
                        in_sub_menu = true;
                        BleScan::bleScanLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 5) {
                    current_submenu_index = 5;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    FlipperDetect::flipperDetectSetup();
                    while (current_submenu_index == 5 && !feature_exit_requested) {
                        current_submenu_index = 5;
                        in_sub_menu = true;
                        FlipperDetect::flipperDetectLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 6) {
                    in_sub_menu = true; feature_active = true; feature_exit_requested = false;
                    AirTagDetect::airtagDetectSetup();
                    while (current_submenu_index == 6 && !feature_exit_requested) {
                        in_sub_menu = true;
                        AirTagDetect::airtagDetectLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true; is_main_menu = false;
                            submenu_initialized = false; feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu(); delay(200);
                            while (isButtonPressed(BTN_SELECT)) {}
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true; is_main_menu = false;
                        submenu_initialized = false; feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu(); delay(200);
                    }
                } else if (current_submenu_index == 7) {
                    in_sub_menu = true; feature_active = true; feature_exit_requested = false;
                    SkimmerDetect::skimmerDetectSetup();
                    while (current_submenu_index == 7 && !feature_exit_requested) {
                        in_sub_menu = true;
                        SkimmerDetect::skimmerDetectLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true; is_main_menu = false;
                            submenu_initialized = false; feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu(); delay(200);
                            while (isButtonPressed(BTN_SELECT)) {}
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true; is_main_menu = false;
                        submenu_initialized = false; feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu(); delay(200);
                    }
                } else if (current_submenu_index == 8) {
                    in_sub_menu = true; feature_active = true; feature_exit_requested = false;
                    RaybanDetect::raybanDetectSetup();
                    while (current_submenu_index == 8 && !feature_exit_requested) {
                        in_sub_menu = true;
                        RaybanDetect::raybanDetectLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true; is_main_menu = false;
                            submenu_initialized = false; feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu(); delay(200);
                            while (isButtonPressed(BTN_SELECT)) {}
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true; is_main_menu = false;
                        submenu_initialized = false; feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu(); delay(200);
                    }
                }
                break;
            }
        }
    }
}


void handleNRFSubmenuButtons() {
    if (isButtonPressed(BTN_UP)) {
        current_submenu_index = (current_submenu_index - 1 + active_submenu_size) % active_submenu_size;
        if (current_submenu_index < 0) {
            current_submenu_index = NUM_SUBMENU_ITEMS - 1;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_DOWN)) {
        current_submenu_index = (current_submenu_index + 1) % active_submenu_size;
        if (current_submenu_index >= NUM_SUBMENU_ITEMS) {
            current_submenu_index = 0;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_SELECT)) {
        last_interaction_time = millis();
        delay(200);

        if (current_submenu_index == 12) {  // Back to Main Menu
            { uint32_t _dStart = millis(); while (isButtonPressed(BTN_SELECT) && millis() - _dStart < 400) delay(5); }
            in_sub_menu = false;
            is_main_menu = false;
            feature_active = false;
            feature_exit_requested = false;
            displayMenu();
            return;
        }

        if (current_submenu_index == 11) {  // Mode Jammer
            in_sub_menu = true; feature_active = true; feature_exit_requested = false;
            NrfModeJammer::modeJammerSetup();
            while (current_submenu_index == 11 && !feature_exit_requested) {
                in_sub_menu = true;
                NrfModeJammer::modeJammerLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true; is_main_menu = false;
                    submenu_initialized = false; feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu(); delay(200);
                    while (isButtonPressed(BTN_SELECT)) {}
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true; is_main_menu = false;
                submenu_initialized = false; feature_active = false;
                feature_exit_requested = false;
                displaySubmenu(); delay(200);
            }
        }

        if (current_submenu_index == 10) {  // Intermittent Jammer
            in_sub_menu = true; feature_active = true; feature_exit_requested = false;
            NrfIntermittent::intermittentSetup();
            while (current_submenu_index == 10 && !feature_exit_requested) {
                in_sub_menu = true;
                NrfIntermittent::intermittentLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true; is_main_menu = false;
                    submenu_initialized = false; feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu(); delay(200);
                    while (isButtonPressed(BTN_SELECT)) {}
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true; is_main_menu = false;
                submenu_initialized = false; feature_active = false;
                feature_exit_requested = false;
                displaySubmenu(); delay(200);
            }
        }

        if (current_submenu_index == 6) {  // Wardrive (GPS)
            in_sub_menu = true; feature_active = true; feature_exit_requested = false;
            NrfWardrive::nrfWardriveSetup();
            while (current_submenu_index == 6 && !feature_exit_requested) {
                in_sub_menu = true;
                NrfWardrive::nrfWardriveLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true; is_main_menu = false;
                    submenu_initialized = false; feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu(); delay(200);
                    while (isButtonPressed(BTN_SELECT)) {}
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true; is_main_menu = false;
                submenu_initialized = false; feature_active = false;
                feature_exit_requested = false;
                displaySubmenu(); delay(200);
            }
        }
        if (current_submenu_index == 7) {  // BLE Adv Monitor
            in_sub_menu = true; feature_active = true; feature_exit_requested = false;
            NrfBleAdv::bleAdvSetup();
            while (current_submenu_index == 7 && !feature_exit_requested) {
                in_sub_menu = true;
                NrfBleAdv::bleAdvLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true; is_main_menu = false;
                    submenu_initialized = false; feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu(); delay(200);
                    while (isButtonPressed(BTN_SELECT)) {}
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true; is_main_menu = false;
                submenu_initialized = false; feature_active = false;
                feature_exit_requested = false;
                displaySubmenu(); delay(200);
            }
        }
        if (current_submenu_index == 8) {  // Capture + Replay
            in_sub_menu = true; feature_active = true; feature_exit_requested = false;
            NrfReplay::replaySetup();
            while (current_submenu_index == 8 && !feature_exit_requested) {
                in_sub_menu = true;
                NrfReplay::replayLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true; is_main_menu = false;
                    submenu_initialized = false; feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu(); delay(200);
                    while (isButtonPressed(BTN_SELECT)) {}
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true; is_main_menu = false;
                submenu_initialized = false; feature_active = false;
                feature_exit_requested = false;
                displaySubmenu(); delay(200);
            }
        }
        if (current_submenu_index == 9) {  // Tri-Radio Watch
            in_sub_menu = true; feature_active = true; feature_exit_requested = false;
            NrfTriWatch::triWatchSetup();
            while (current_submenu_index == 9 && !feature_exit_requested) {
                in_sub_menu = true;
                NrfTriWatch::triWatchLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true; is_main_menu = false;
                    submenu_initialized = false; feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu(); delay(200);
                    while (isButtonPressed(BTN_SELECT)) {}
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true; is_main_menu = false;
                submenu_initialized = false; feature_active = false;
                feature_exit_requested = false;
                displaySubmenu(); delay(200);
            }
        }

        // ---- New NRF24 tools ---- (cases 2..5)
        if (current_submenu_index == 2) {  // Mousejack Scan
            in_sub_menu = true; feature_active = true; feature_exit_requested = false;
            NrfMousejack::mousejackSetup();
            while (current_submenu_index == 2 && !feature_exit_requested) {
                in_sub_menu = true;
                NrfMousejack::mousejackLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true; is_main_menu = false;
                    submenu_initialized = false; feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu(); delay(200);
                    while (isButtonPressed(BTN_SELECT)) {}
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true; is_main_menu = false;
                submenu_initialized = false; feature_active = false;
                feature_exit_requested = false;
                displaySubmenu(); delay(200);
            }
        }
        if (current_submenu_index == 3) {  // Promisc Sniffer
            in_sub_menu = true; feature_active = true; feature_exit_requested = false;
            NrfSniffer::sniffSetup();
            while (current_submenu_index == 3 && !feature_exit_requested) {
                in_sub_menu = true;
                NrfSniffer::sniffLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true; is_main_menu = false;
                    submenu_initialized = false; feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu(); delay(200);
                    while (isButtonPressed(BTN_SELECT)) {}
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true; is_main_menu = false;
                submenu_initialized = false; feature_active = false;
                feature_exit_requested = false;
                displaySubmenu(); delay(200);
            }
        }
        if (current_submenu_index == 4) {  // Channel Heatmap
            in_sub_menu = true; feature_active = true; feature_exit_requested = false;
            NrfHeatmap::heatmapSetup();
            while (current_submenu_index == 4 && !feature_exit_requested) {
                in_sub_menu = true;
                NrfHeatmap::heatmapLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true; is_main_menu = false;
                    submenu_initialized = false; feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu(); delay(200);
                    while (isButtonPressed(BTN_SELECT)) {}
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true; is_main_menu = false;
                submenu_initialized = false; feature_active = false;
                feature_exit_requested = false;
                displaySubmenu(); delay(200);
            }
        }
        if (current_submenu_index == 5) {  // Const Carrier
            in_sub_menu = true; feature_active = true; feature_exit_requested = false;
            NrfCarrier::carrierSetup();
            while (current_submenu_index == 5 && !feature_exit_requested) {
                in_sub_menu = true;
                NrfCarrier::carrierLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true; is_main_menu = false;
                    submenu_initialized = false; feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu(); delay(200);
                    while (isButtonPressed(BTN_SELECT)) {}
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true; is_main_menu = false;
                submenu_initialized = false; feature_active = false;
                feature_exit_requested = false;
                displaySubmenu(); delay(200);
            }
        }

        if (current_submenu_index == 0) {
            current_submenu_index = 0;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            Scanner::scannerSetup();
            while (current_submenu_index == 0 && !feature_exit_requested) {  
                current_submenu_index = 0;
                in_sub_menu = true;
                Scanner::scannerLoop();        
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false; 
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false; 
                    displaySubmenu(); 
                    delay(200);            
                    while (isButtonPressed(BTN_SELECT)) {
                    }           
                    break;  
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false; 
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false; 
                displaySubmenu(); 
                delay(200);
            }
        }

        if (current_submenu_index == 1) {
            current_submenu_index = 1;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false; 
            ProtoKill::prokillSetup(); 
            while (current_submenu_index == 1 && !feature_exit_requested) {  
                current_submenu_index = 1;
                in_sub_menu = true;
                ProtoKill::prokillLoop();        
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false; 
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false; 
                    displaySubmenu(); 
                    delay(200);            
                    while (isButtonPressed(BTN_SELECT)) {
                    }           
                    break;  
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false; 
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false; 
                displaySubmenu(); 
                delay(200);
            }
        }
    }

    if (ts.touched() && !feature_active) {
        TS_Point p = ts.getPoint();
        delay(10);

        int x, y, z;
        x = ::map(p.x, TS_MINX, TS_MAXX, 0, 239);
        y = ::map(p.y, TS_MAXY, TS_MINY, 0, 319);

        int _touch_visible = active_submenu_size - submenu_view_offset;
        if (_touch_visible > MAX_VISIBLE_SUBMENU_ITEMS) _touch_visible = MAX_VISIBLE_SUBMENU_ITEMS;
        for (int _slot = 0; _slot < _touch_visible; _slot++) {
            int i = submenu_view_offset + _slot;
            int yPos = 30 + _slot * 30;

            int button_x1 = 10;
            int button_y1 = yPos;
            int button_x2 = 110;
            int button_y2 = yPos + 30;

            if (x >= button_x1 && x <= button_x2 && y >= button_y1 && y <= button_y2) {
                current_submenu_index = i;
                last_interaction_time = millis();
                displaySubmenu();
                delay(200);

                if (current_submenu_index == 12) {  // Back to Main Menu
                    in_sub_menu = false;
                    is_main_menu = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displayMenu();
                    return;
                } else if (current_submenu_index == 11) {  // Mode Jammer
                    in_sub_menu = true; feature_active = true; feature_exit_requested = false;
                    NrfModeJammer::modeJammerSetup();
                    while (current_submenu_index == 11 && !feature_exit_requested) {
                        in_sub_menu = true;
                        NrfModeJammer::modeJammerLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true; is_main_menu = false;
                            submenu_initialized = false; feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu(); delay(200);
                            while (isButtonPressed(BTN_SELECT)) {}
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true; is_main_menu = false;
                        submenu_initialized = false; feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu(); delay(200);
                    }
                } else if (current_submenu_index == 10) {  // Intermittent Jammer
                    in_sub_menu = true; feature_active = true; feature_exit_requested = false;
                    NrfIntermittent::intermittentSetup();
                    while (current_submenu_index == 10 && !feature_exit_requested) {
                        in_sub_menu = true;
                        NrfIntermittent::intermittentLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true; is_main_menu = false;
                            submenu_initialized = false; feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu(); delay(200);
                            while (isButtonPressed(BTN_SELECT)) {}
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true; is_main_menu = false;
                        submenu_initialized = false; feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu(); delay(200);
                    }
                } else if (current_submenu_index == 6) {  // Wardrive (GPS)
                    in_sub_menu = true; feature_active = true; feature_exit_requested = false;
                    NrfWardrive::nrfWardriveSetup();
                    while (current_submenu_index == 6 && !feature_exit_requested) {
                        in_sub_menu = true;
                        NrfWardrive::nrfWardriveLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true; is_main_menu = false;
                            submenu_initialized = false; feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu(); delay(200);
                            while (isButtonPressed(BTN_SELECT)) {}
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true; is_main_menu = false;
                        submenu_initialized = false; feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu(); delay(200);
                    }
                } else if (current_submenu_index == 7) {  // BLE Adv Monitor
                    in_sub_menu = true; feature_active = true; feature_exit_requested = false;
                    NrfBleAdv::bleAdvSetup();
                    while (current_submenu_index == 7 && !feature_exit_requested) {
                        in_sub_menu = true;
                        NrfBleAdv::bleAdvLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true; is_main_menu = false;
                            submenu_initialized = false; feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu(); delay(200);
                            while (isButtonPressed(BTN_SELECT)) {}
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true; is_main_menu = false;
                        submenu_initialized = false; feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu(); delay(200);
                    }
                } else if (current_submenu_index == 8) {  // Capture+Replay
                    in_sub_menu = true; feature_active = true; feature_exit_requested = false;
                    NrfReplay::replaySetup();
                    while (current_submenu_index == 8 && !feature_exit_requested) {
                        in_sub_menu = true;
                        NrfReplay::replayLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true; is_main_menu = false;
                            submenu_initialized = false; feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu(); delay(200);
                            while (isButtonPressed(BTN_SELECT)) {}
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true; is_main_menu = false;
                        submenu_initialized = false; feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu(); delay(200);
                    }
                } else if (current_submenu_index == 9) {  // Tri-Radio Watch
                    in_sub_menu = true; feature_active = true; feature_exit_requested = false;
                    NrfTriWatch::triWatchSetup();
                    while (current_submenu_index == 9 && !feature_exit_requested) {
                        in_sub_menu = true;
                        NrfTriWatch::triWatchLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true; is_main_menu = false;
                            submenu_initialized = false; feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu(); delay(200);
                            while (isButtonPressed(BTN_SELECT)) {}
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true; is_main_menu = false;
                        submenu_initialized = false; feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu(); delay(200);
                    }
                } else if (current_submenu_index == 2) {  // Mousejack
                    in_sub_menu = true; feature_active = true; feature_exit_requested = false;
                    NrfMousejack::mousejackSetup();
                    while (current_submenu_index == 2 && !feature_exit_requested) {
                        in_sub_menu = true;
                        NrfMousejack::mousejackLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true; is_main_menu = false;
                            submenu_initialized = false; feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu(); delay(200);
                            while (isButtonPressed(BTN_SELECT)) {}
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true; is_main_menu = false;
                        submenu_initialized = false; feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu(); delay(200);
                    }
                } else if (current_submenu_index == 3) {  // Sniffer
                    in_sub_menu = true; feature_active = true; feature_exit_requested = false;
                    NrfSniffer::sniffSetup();
                    while (current_submenu_index == 3 && !feature_exit_requested) {
                        in_sub_menu = true;
                        NrfSniffer::sniffLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true; is_main_menu = false;
                            submenu_initialized = false; feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu(); delay(200);
                            while (isButtonPressed(BTN_SELECT)) {}
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true; is_main_menu = false;
                        submenu_initialized = false; feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu(); delay(200);
                    }
                } else if (current_submenu_index == 4) {  // Heatmap
                    in_sub_menu = true; feature_active = true; feature_exit_requested = false;
                    NrfHeatmap::heatmapSetup();
                    while (current_submenu_index == 4 && !feature_exit_requested) {
                        in_sub_menu = true;
                        NrfHeatmap::heatmapLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true; is_main_menu = false;
                            submenu_initialized = false; feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu(); delay(200);
                            while (isButtonPressed(BTN_SELECT)) {}
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true; is_main_menu = false;
                        submenu_initialized = false; feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu(); delay(200);
                    }
                } else if (current_submenu_index == 5) {  // Const Carrier
                    in_sub_menu = true; feature_active = true; feature_exit_requested = false;
                    NrfCarrier::carrierSetup();
                    while (current_submenu_index == 5 && !feature_exit_requested) {
                        in_sub_menu = true;
                        NrfCarrier::carrierLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true; is_main_menu = false;
                            submenu_initialized = false; feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu(); delay(200);
                            while (isButtonPressed(BTN_SELECT)) {}
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true; is_main_menu = false;
                        submenu_initialized = false; feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu(); delay(200);
                    }
                } else if (current_submenu_index == 0) {
                    current_submenu_index = 0;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false; 
                    Scanner::scannerSetup(); 
                    while (current_submenu_index == 0 && !feature_exit_requested) {  
                        current_submenu_index = 0;
                        in_sub_menu = true;
                        Scanner::scannerLoop();        
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false; 
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false; 
                            displaySubmenu(); 
                            delay(200);            
                            while (isButtonPressed(BTN_SELECT)) {
                            }           
                            break;  
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false; 
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false; 
                        displaySubmenu(); 
                        delay(200);
                    }
                } else if (current_submenu_index == 1) {
                    current_submenu_index = 1;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false; 
                    ProtoKill::prokillSetup(); 
                    while (current_submenu_index == 1 && !feature_exit_requested) {  
                        current_submenu_index = 1;
                        in_sub_menu = true;
                        ProtoKill::prokillLoop();        
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false; 
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false; 
                            displaySubmenu(); 
                            delay(200);            
                            while (isButtonPressed(BTN_SELECT)) {
                            }           
                            break;  
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false; 
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false; 
                        displaySubmenu(); 
                        delay(200);
                    }
                }
                break;
            }
        }
    }
}


// Run a SubGHz feature: setup, then loop until the user either presses
// SELECT (to back out via the boilerplate) or the feature sets
// feature_exit_requested itself. Centralises the 30-line state-machine
// boilerplate that was previously copy-pasted 8 times across the button
// path and 8 more times across the touch path of this handler.
// Function-pointer type used inline (not as a typedef) so arduino-cli's
// auto-generated forward declarations don't see an unknown identifier.
static void runSubghzFeature(int idx, void (*setupFn)(), void (*loopFn)()) {
    in_sub_menu = true;
    feature_active = true;
    feature_exit_requested = false;
    setupFn();
    while (current_submenu_index == idx && !feature_exit_requested) {
        in_sub_menu = true;
        loopFn();
        if (isButtonPressed(BTN_SELECT)) {
            in_sub_menu = true;
            is_main_menu = false;
            submenu_initialized = false;
            feature_active = false;
            feature_exit_requested = false;
            displaySubmenu();
            delay(200);
            while (isButtonPressed(BTN_SELECT)) {}
            return;
        }
    }
    if (feature_exit_requested) {
        in_sub_menu = true;
        is_main_menu = false;
        submenu_initialized = false;
        feature_active = false;
        feature_exit_requested = false;
        displaySubmenu();
        delay(200);
    }
}

void handleSubGHzSubmenuButtons() {
    if (isButtonPressed(BTN_UP)) {
        current_submenu_index = (current_submenu_index - 1 + active_submenu_size) % active_submenu_size;
        if (current_submenu_index < 0) {
            current_submenu_index = NUM_SUBMENU_ITEMS - 1;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_DOWN)) {
        current_submenu_index = (current_submenu_index + 1) % active_submenu_size;
        if (current_submenu_index >= NUM_SUBMENU_ITEMS) {
            current_submenu_index = 0;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_SELECT)) {
        last_interaction_time = millis();
        delay(200);

        if (current_submenu_index == 11) {  // Back to Main Menu
            { uint32_t _dStart = millis(); while (isButtonPressed(BTN_SELECT) && millis() - _dStart < 400) delay(5); }
            in_sub_menu = false;
            is_main_menu = false;
            feature_active = false;
            feature_exit_requested = false;
            displayMenu();
            return;
        }

        // Each feature's own setup() now calls subghzReleasePinsFromNrf(),
        // so the previous in-line pinMode(26/16, INPUT) calls are gone.
        switch (current_submenu_index) {
            case 0: runSubghzFeature(0, replayat::ReplayAttackSetup, replayat::ReplayAttackLoop); break;
            case 1: runSubghzFeature(1, subjammer::subjammerSetup,   subjammer::subjammerLoop);   break;
            case 2: runSubghzFeature(2, SavedProfile::saveSetup,     SavedProfile::saveLoop);     break;
            case 3: runSubghzFeature(3, SubGhzWardrive::wardriveSetup, SubGhzWardrive::wardriveLoop); break;
            case 4: runSubghzFeature(4, SpectrumAnalyzer::spectrumSetup, SpectrumAnalyzer::spectrumLoop); break;
            case 5: runSubghzFeature(5, FreqDetector::freqDetectorSetup, FreqDetector::freqDetectorLoop); break;
            case 6: runSubghzFeature(6, SubReplay::subReplaySetup,   SubReplay::subReplayLoop);   break;
            case 7: runSubghzFeature(7, SweepJammer::sweepJammerSetup, SweepJammer::sweepJammerLoop); break;
            case 8: runSubghzFeature(8, SubRecord::subRecordSetup,   SubRecord::subRecordLoop);   break;
            case 9: runSubghzFeature(9, SubSendCode::subSendCodeSetup, SubSendCode::subSendCodeLoop); break;
            case 10: runSubghzFeature(10, SubKeeloq::subKeeloqSetup, SubKeeloq::subKeeloqLoop); break;
        }
    }

    if (ts.touched() && !feature_active) {
        TS_Point p = ts.getPoint();
        delay(10);

        int x, y, z;
        x = ::map(p.x, TS_MINX, TS_MAXX, 0, 239);
        y = ::map(p.y, TS_MAXY, TS_MINY, 0, 319);

        int _touch_visible = active_submenu_size - submenu_view_offset;
        if (_touch_visible > MAX_VISIBLE_SUBMENU_ITEMS) _touch_visible = MAX_VISIBLE_SUBMENU_ITEMS;
        for (int _slot = 0; _slot < _touch_visible; _slot++) {
            int i = submenu_view_offset + _slot;
            int yPos = 30 + _slot * 30;

            int button_x1 = 10;
            int button_y1 = yPos;
            int button_x2 = 110;
            int button_y2 = yPos + 30;

            if (x >= button_x1 && x <= button_x2 && y >= button_y1 && y <= button_y2) {
                current_submenu_index = i;
                last_interaction_time = millis();
                displaySubmenu();
                delay(200);

                if (current_submenu_index == 11) {  // Back to Main Menu
                    in_sub_menu = false;
                    is_main_menu = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displayMenu();
                    return;
                }
                switch (current_submenu_index) {
                    case 0: runSubghzFeature(0, replayat::ReplayAttackSetup, replayat::ReplayAttackLoop); break;
                    case 1: runSubghzFeature(1, subjammer::subjammerSetup,   subjammer::subjammerLoop);   break;
                    case 2: runSubghzFeature(2, SavedProfile::saveSetup,     SavedProfile::saveLoop);     break;
                    case 3: runSubghzFeature(3, SubGhzWardrive::wardriveSetup, SubGhzWardrive::wardriveLoop); break;
                    case 4: runSubghzFeature(4, SpectrumAnalyzer::spectrumSetup, SpectrumAnalyzer::spectrumLoop); break;
                    case 5: runSubghzFeature(5, FreqDetector::freqDetectorSetup, FreqDetector::freqDetectorLoop); break;
                    case 6: runSubghzFeature(6, SubReplay::subReplaySetup,   SubReplay::subReplayLoop);   break;
                    case 7: runSubghzFeature(7, SweepJammer::sweepJammerSetup, SweepJammer::sweepJammerLoop); break;
                    case 8: runSubghzFeature(8, SubRecord::subRecordSetup,   SubRecord::subRecordLoop);   break;
                    case 9: runSubghzFeature(9, SubSendCode::subSendCodeSetup, SubSendCode::subSendCodeLoop); break;
                    case 10: runSubghzFeature(10, SubKeeloq::subKeeloqSetup, SubKeeloq::subKeeloqLoop); break;
                }
                break;
            }
        }
    }
}


void handleToolsSubmenuButtons() {
    if (isButtonPressed(BTN_UP)) {
        current_submenu_index = (current_submenu_index - 1 + active_submenu_size) % active_submenu_size;
        if (current_submenu_index < 0) {
            current_submenu_index = active_submenu_size - 1;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_DOWN)) {
        current_submenu_index = (current_submenu_index + 1) % active_submenu_size;
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_SELECT)) {
        last_interaction_time = millis();
        delay(200);

        if (current_submenu_index == 3) {  // Back to Main Menu
            { uint32_t _dStart = millis(); while (isButtonPressed(BTN_SELECT) && millis() - _dStart < 400) delay(5); }
            in_sub_menu = false;
            is_main_menu = false;
            feature_active = false;
            feature_exit_requested = false;
            displayMenu();
            return;
        }

        if (current_submenu_index == 2) {  // SD Browser
            in_sub_menu = true; feature_active = true; feature_exit_requested = false;
            SdBrowser::sdBrowserSetup();
            while (current_submenu_index == 2 && !feature_exit_requested) {
                in_sub_menu = true;
                SdBrowser::sdBrowserLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true; is_main_menu = false;
                    submenu_initialized = false; feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu(); delay(200);
                    while (isButtonPressed(BTN_SELECT)) {}
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true; is_main_menu = false;
                submenu_initialized = false; feature_active = false;
                feature_exit_requested = false;
                displaySubmenu(); delay(200);
            }
        }

        if (current_submenu_index == 0) {  // Format SD
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            SdFormat::sdFormatSetup();
            while (current_submenu_index == 0 && !feature_exit_requested) {
                in_sub_menu = true;
                SdFormat::sdFormatLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {}
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 1) {  // GPS Status
            in_sub_menu = true; feature_active = true; feature_exit_requested = false;
            GpsStatus::gpsStatusSetup();
            while (current_submenu_index == 1 && !feature_exit_requested) {
                in_sub_menu = true;
                GpsStatus::gpsStatusLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true; is_main_menu = false;
                    submenu_initialized = false; feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu(); delay(200);
                    while (isButtonPressed(BTN_SELECT)) {}
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true; is_main_menu = false;
                submenu_initialized = false; feature_active = false;
                feature_exit_requested = false;
                displaySubmenu(); delay(200);
            }
        }
    }

    if (ts.touched() && !feature_active) {
        TS_Point p = ts.getPoint();
        delay(10);

        int x, y, z;
        x = ::map(p.x, TS_MINX, TS_MAXX, 0, 239);
        y = ::map(p.y, TS_MAXY, TS_MINY, 0, 319);

        int _touch_visible = active_submenu_size - submenu_view_offset;
        if (_touch_visible > MAX_VISIBLE_SUBMENU_ITEMS) _touch_visible = MAX_VISIBLE_SUBMENU_ITEMS;
        for (int _slot = 0; _slot < _touch_visible; _slot++) {
            int i = submenu_view_offset + _slot;
            int yPos = 30 + _slot * 30;

            int button_x1 = 10;
            int button_y1 = yPos;
            int button_x2 = 110;
            int button_y2 = yPos + 30;

            if (x >= button_x1 && x <= button_x2 && y >= button_y1 && y <= button_y2) {
                current_submenu_index = i;
                last_interaction_time = millis();
                displaySubmenu();
                delay(200);

                if (current_submenu_index == 3) {  // Back to Main Menu
                    in_sub_menu = false;
                    is_main_menu = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displayMenu();
                    return;
                } else if (current_submenu_index == 2) {  // SD Browser
                    in_sub_menu = true; feature_active = true; feature_exit_requested = false;
                    SdBrowser::sdBrowserSetup();
                    while (current_submenu_index == 2 && !feature_exit_requested) {
                        in_sub_menu = true;
                        SdBrowser::sdBrowserLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true; is_main_menu = false;
                            submenu_initialized = false; feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu(); delay(200);
                            while (isButtonPressed(BTN_SELECT)) {}
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true; is_main_menu = false;
                        submenu_initialized = false; feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu(); delay(200);
                    }
                } else if (current_submenu_index == 0) {  // Format SD
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    SdFormat::sdFormatSetup();
                    while (current_submenu_index == 0 && !feature_exit_requested) {
                        in_sub_menu = true;
                        SdFormat::sdFormatLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {}
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 1) {  // GPS Status
                    in_sub_menu = true; feature_active = true; feature_exit_requested = false;
                    GpsStatus::gpsStatusSetup();
                    while (current_submenu_index == 1 && !feature_exit_requested) {
                        in_sub_menu = true;
                        GpsStatus::gpsStatusLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true; is_main_menu = false;
                            submenu_initialized = false; feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu(); delay(200);
                            while (isButtonPressed(BTN_SELECT)) {}
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true; is_main_menu = false;
                        submenu_initialized = false; feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu(); delay(200);
                    }
                }
                break;
            }
        }
    }
}


// ============================================================
// Settings submenu — currently exposes:
//   0: Wardrive Channels (sub-GHz freq selector)
//   1: Back to Main Menu
// More options land here as the firmware grows.
// ============================================================
void handleSettingsSubmenuButtons() {
    if (isButtonPressed(BTN_UP)) {
        current_submenu_index = (current_submenu_index - 1 + active_submenu_size) % active_submenu_size;
        if (current_submenu_index < 0) current_submenu_index = active_submenu_size - 1;
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }
    if (isButtonPressed(BTN_DOWN)) {
        current_submenu_index = (current_submenu_index + 1) % active_submenu_size;
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_SELECT)) {
        last_interaction_time = millis();
        delay(200);

        if (current_submenu_index == 1) {  // Back to Main Menu
            { uint32_t _dStart = millis(); while (isButtonPressed(BTN_SELECT) && millis() - _dStart < 400) delay(5); }
            in_sub_menu = false;
            is_main_menu = false;
            feature_active = false;
            feature_exit_requested = false;
            displayMenu();
            return;
        }

        if (current_submenu_index == 0) {  // Wardrive Channels
            in_sub_menu = true; feature_active = true; feature_exit_requested = false;
            WardriveChannels::wardriveChannelsSetup();
            while (current_submenu_index == 0 && !feature_exit_requested) {
                in_sub_menu = true;
                WardriveChannels::wardriveChannelsLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true; is_main_menu = false;
                    submenu_initialized = false; feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu(); delay(200);
                    while (isButtonPressed(BTN_SELECT)) {}
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true; is_main_menu = false;
                submenu_initialized = false; feature_active = false;
                feature_exit_requested = false;
                displaySubmenu(); delay(200);
            }
        }
    }

    if (ts.touched() && !feature_active) {
        TS_Point p = ts.getPoint();
        delay(10);
        int x, y;
        x = ::map(p.x, TS_MINX, TS_MAXX, 0, 239);
        y = ::map(p.y, TS_MAXY, TS_MINY, 0, 319);

        int _touch_visible = active_submenu_size - submenu_view_offset;
        if (_touch_visible > MAX_VISIBLE_SUBMENU_ITEMS) _touch_visible = MAX_VISIBLE_SUBMENU_ITEMS;
        for (int _slot = 0; _slot < _touch_visible; _slot++) {
            int i = submenu_view_offset + _slot;
            int yPos = 30 + _slot * 30;
            if (x >= 10 && x <= 110 && y >= yPos && y <= yPos + 30) {
                current_submenu_index = i;
                last_interaction_time = millis();
                displaySubmenu();
                delay(200);
                // Dispatch via the same button path
                if (current_submenu_index == 1) {  // Back to Main Menu
                    in_sub_menu = false; is_main_menu = false;
                    feature_active = false; feature_exit_requested = false;
                    displayMenu();
                    return;
                } else if (current_submenu_index == 0) {
                    in_sub_menu = true; feature_active = true; feature_exit_requested = false;
                    WardriveChannels::wardriveChannelsSetup();
                    while (current_submenu_index == 0 && !feature_exit_requested) {
                        in_sub_menu = true;
                        WardriveChannels::wardriveChannelsLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true; is_main_menu = false;
                            submenu_initialized = false; feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu(); delay(200);
                            while (isButtonPressed(BTN_SELECT)) {}
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true; is_main_menu = false;
                        submenu_initialized = false; feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu(); delay(200);
                    }
                }
                break;
            }
        }
    }
}


void handleAboutPage() {

  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setTextSize(1);
  tft.setTextFont(2);
  
  const char* title = "[About This Project]";
  tft.setCursor(10, 90);
  tft.println(title);
  
  int lineHeight = 30;
  int text_x = 10;
  int text_y = 130;
  tft.setCursor(text_x, text_y);
  tft.println("- Hydra");
  text_y += lineHeight;
  tft.setCursor(text_x, text_y);
  tft.println("- Developed by: Claude");
  text_y += lineHeight;
  tft.setCursor(text_x, text_y);
  tft.println("- Version: " HYDRA_VERSION);
  text_y += lineHeight;


    if (isButtonPressed(BTN_UP)) {
        current_submenu_index = (current_submenu_index - 1 + active_submenu_size) % active_submenu_size;
        if (current_submenu_index < 0) {
            current_submenu_index = NUM_SUBMENU_ITEMS - 1;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_DOWN)) {
        current_submenu_index = (current_submenu_index + 1) % active_submenu_size;
        if (current_submenu_index >= NUM_SUBMENU_ITEMS) {
            current_submenu_index = 0;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_SELECT)) {
        last_interaction_time = millis();
        delay(200);

        if (current_submenu_index == 0) {  // Back to Main Menu
            { uint32_t _dStart = millis(); while (isButtonPressed(BTN_SELECT) && millis() - _dStart < 400) delay(5); }
            in_sub_menu = false;
            is_main_menu = false;
            feature_active = false;
            feature_exit_requested = false;
            displayMenu();
            return;
        }
    }

    if (ts.touched() && !feature_active) {
        TS_Point p = ts.getPoint();
        delay(10);

        int x, y, z;
        x = ::map(p.x, TS_MINX, TS_MAXX, 0, 239);
        y = ::map(p.y, TS_MAXY, TS_MINY, 0, 319);

        int _touch_visible = active_submenu_size - submenu_view_offset;
        if (_touch_visible > MAX_VISIBLE_SUBMENU_ITEMS) _touch_visible = MAX_VISIBLE_SUBMENU_ITEMS;
        for (int _slot = 0; _slot < _touch_visible; _slot++) {
            int i = submenu_view_offset + _slot;
            int yPos = 30 + _slot * 30;

            int button_x1 = 10;
            int button_y1 = yPos;
            int button_x2 = 110;
            int button_y2 = yPos + 30;

            if (x >= button_x1 && x <= button_x2 && y >= button_y1 && y <= button_y2) {
                current_submenu_index = i;
                last_interaction_time = millis();
                displaySubmenu();
                delay(200);

                if (current_submenu_index == 0) {  // Back to Main Menu
                    in_sub_menu = false;
                    is_main_menu = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displayMenu();
                    return;
                }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false; 
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false; 
                        displaySubmenu(); 
                        delay(200);
                    }             
            }
        }
    }
}


void handleSettingsSubmenuButtons();  // forward decl — defined below
void handleIRSubmenuButtons();         // forward decl — defined below

// ============================================================
// IR submenu — top-level section. Three modal entries:
//   0: TV-B-Gone    — sweeps ~30 power-off codes
//   1: IR Receiver  — live capture/decode
//   2: IR Replay    — replay last captured frame
//   3: Back to Main Menu
// ============================================================
void handleIRSubmenuButtons() {
    if (isButtonPressed(BTN_UP)) {
        current_submenu_index = (current_submenu_index - 1 + active_submenu_size) % active_submenu_size;
        if (current_submenu_index < 0) current_submenu_index = active_submenu_size - 1;
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_DOWN)) {
        current_submenu_index = (current_submenu_index + 1) % active_submenu_size;
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_SELECT)) {
        last_interaction_time = millis();
        delay(200);

        if (current_submenu_index == 3) {  // Back to Main Menu
            // Drain SELECT so the held press doesn't re-trigger as a fresh
            // main-menu SELECT and bounce us back into the first IR item.
            { uint32_t _dStart = millis(); while (isButtonPressed(BTN_SELECT) && millis() - _dStart < 400) delay(5); }
            in_sub_menu = false;
            is_main_menu = false;
            feature_active = false;
            feature_exit_requested = false;
            displayMenu();
            return;  // prevent fall-through to TV-B-Gone/Receiver/Replay branches
        }

        if (current_submenu_index == 0) {  // TV-B-Gone
            in_sub_menu = true; feature_active = true; feature_exit_requested = false;
            IrRemote::irTvOffSetup();
            while (current_submenu_index == 0 && !feature_exit_requested) {
                in_sub_menu = true;
                IrRemote::irTvOffLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true; is_main_menu = false;
                    submenu_initialized = false; feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu(); delay(200);
                    while (isButtonPressed(BTN_SELECT)) {}
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true; is_main_menu = false;
                submenu_initialized = false; feature_active = false;
                feature_exit_requested = false;
                displaySubmenu(); delay(200);
            }
        }

        if (current_submenu_index == 1) {  // IR Receiver
            in_sub_menu = true; feature_active = true; feature_exit_requested = false;
            IrRemote::irRecvSetup();
            while (current_submenu_index == 1 && !feature_exit_requested) {
                in_sub_menu = true;
                IrRemote::irRecvLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true; is_main_menu = false;
                    submenu_initialized = false; feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu(); delay(200);
                    while (isButtonPressed(BTN_SELECT)) {}
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true; is_main_menu = false;
                submenu_initialized = false; feature_active = false;
                feature_exit_requested = false;
                displaySubmenu(); delay(200);
            }
        }

        if (current_submenu_index == 2) {  // IR Replay
            in_sub_menu = true; feature_active = true; feature_exit_requested = false;
            IrRemote::irReplaySetup();
            while (current_submenu_index == 2 && !feature_exit_requested) {
                in_sub_menu = true;
                IrRemote::irReplayLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true; is_main_menu = false;
                    submenu_initialized = false; feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu(); delay(200);
                    while (isButtonPressed(BTN_SELECT)) {}
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true; is_main_menu = false;
                submenu_initialized = false; feature_active = false;
                feature_exit_requested = false;
                displaySubmenu(); delay(200);
            }
        }
    }
}

void handleButtons() {
    if (in_sub_menu) {
        switch (current_menu_index) {
            case 0: handleWiFiSubmenuButtons(); break;
            case 1: handleBluetoothSubmenuButtons(); break;
            case 2: handleNRFSubmenuButtons(); break;
            case 3: handleSubGHzSubmenuButtons(); break;
            case 4: handleToolsSubmenuButtons(); break;
            case 5: handleSettingsSubmenuButtons(); break;
            case 6: handleAboutPage(); break;
            case 7: handleIRSubmenuButtons(); break;
            default: break;
        }
    } else {
      
        if (isButtonPressed(BTN_UP) && !is_main_menu) {
            current_menu_index--;
            if (current_menu_index < 0) {
                current_menu_index = NUM_MENU_ITEMS - 1; 
            }
            last_interaction_time = millis();
            displayMenu();
            delay(200);
        }

        if (isButtonPressed(BTN_DOWN) && !is_main_menu) {
            current_menu_index++;
            if (current_menu_index >= NUM_MENU_ITEMS) {
                current_menu_index = 0; 
            }
            last_interaction_time = millis();
            displayMenu();
            delay(200);
        }

        if (isButtonPressed(BTN_LEFT) && !is_main_menu) {
            int row = current_menu_index % 4;
            if (current_menu_index >= 4) {
                current_menu_index = row;
            } else {
                int candidate = row + 4;
                if (candidate < NUM_MENU_ITEMS) current_menu_index = candidate;
            }
            if (current_menu_index < 0 || current_menu_index >= NUM_MENU_ITEMS) current_menu_index = 0;
            last_interaction_time = millis();
            displayMenu();
            delay(200);
        }

        if (isButtonPressed(BTN_RIGHT) && !is_main_menu) {
            int row = current_menu_index % 4;
            if (current_menu_index < 4) {
                int candidate = row + 4;
                if (candidate < NUM_MENU_ITEMS) current_menu_index = candidate;
            } else {
                current_menu_index = row;
            }
            if (current_menu_index < 0 || current_menu_index >= NUM_MENU_ITEMS) current_menu_index = 0;
            last_interaction_time = millis();
            displayMenu();
            delay(200);
        }

        if (isButtonPressed(BTN_SELECT)) {
            last_interaction_time = millis();
            updateActiveSubmenu();
            delay(200);

            if (active_submenu_items && active_submenu_size > 0) {
                current_submenu_index = 0;
                in_sub_menu = true;
                submenu_initialized = false;
                displaySubmenu();
            }

            if (is_main_menu) {
                is_main_menu = false;
                displayMenu();
            } else {
                is_main_menu = true;
            }
        }

        static unsigned long lastTouchTime = 0;
        const unsigned long touchFeedbackDelay = 100;

        // Edge-triggered touch: only act on the transition not-touched -> touched.
        // Without this, a stuck/noisy touchscreen reading (common right after
        // boot or with a held finger) auto-fires every loop iteration. Combined
        // with the new IR cell at the bottom-right, that caused the firmware
        // to auto-enter IR and re-enter it after every Back.
        static bool _prevTouchedMain = true;  // start true so a stale boot read is ignored
        bool _curTouched = ts.touched();
        bool _touchEdge = (_curTouched && !_prevTouchedMain);
        _prevTouchedMain = _curTouched;

        if (_touchEdge && !feature_active && (millis() - lastTouchTime >= touchFeedbackDelay)) {
            TS_Point p = ts.getPoint();
            delay(10); 

            int x, y, z;
            x = ::map(p.x, TS_MINX, TS_MAXX, 0, 239);
            y = ::map(p.y, TS_MAXY, TS_MINY, 0, 319); 

            for (int i = 0; i < NUM_MENU_ITEMS; i++) {
                int column = i / 4; 
                int row = i % 4; 
                int x_position = (column == 0) ? X_OFFSET_LEFT : X_OFFSET_RIGHT;
                int y_position = Y_START + row * Y_SPACING;

                int button_x1 = x_position;
                int button_y1 = y_position;
                int button_x2 = x_position + 100;
                int button_y2 = y_position + 60;

                if (x >= button_x1 && x <= button_x2 && y >= button_y1 && y <= button_y2) {
                    current_menu_index = i; 
                    last_interaction_time = millis();
                    displayMenu(); 

                    unsigned long startTime = millis();
                    while (ts.touched() && (millis() - startTime < touchFeedbackDelay)) {
                        delay(10); 
                    }

                    if (ts.touched()) {
                        updateActiveSubmenu(); 

                        if (active_submenu_items && active_submenu_size > 0) {
                            current_submenu_index = 0;
                            in_sub_menu = true;
                            submenu_initialized = false;
                            displaySubmenu();
                        } else {
                            
                            if (is_main_menu) {
                                is_main_menu = false;
                                displayMenu();
                            } else {
                                is_main_menu = true;
                            }
                        }
                    }
                    delay(200);
                    break; 
                }
            }
        }
    }
}


void setup() {
  Serial.begin(115200);
  
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);

  setupTouchscreen();

  // Boot splash: Hydra logo bitmap (220x146, 1-bit packed, ~4 KB in flash)
  // + version line. Held on screen for 2.5 sec.
  tft.fillScreen(TFT_BLACK);
  displayLogo(TFT_WHITE, 2500);
  
  //pinMode(36, INPUT);
  //pinMode(BACKLIGHT_PIN, OUTPUT);
  //digitalWrite(BACKLIGHT_PIN, HIGH);
  
  // Order matters: pinMode populates readMode/readModePullUp, then begin()
  // writes the correct latch state (0xF8) to the chip AND initializes the
  // library's byteBuffered cache to match. If begin() is called first with
  // no modes set, byteBuffered defaults to 0, and the first digitalRead on
  // every pullup pin returns LOW — making all 5 buttons read as "pressed"
  // until the library's stateful XOR/latency logic catches up.
  pcf.pinMode(BTN_UP, INPUT_PULLUP);
  pcf.pinMode(BTN_DOWN, INPUT_PULLUP);
  pcf.pinMode(BTN_LEFT, INPUT_PULLUP);
  pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);
  pcf.pinMode(BTN_SELECT, INPUT_PULLUP);
  pcf.begin();

  // Gps::begin() is no longer called at boot, but the touch-stealing
  // hazard that used to live here is gone too: as of v0.0.3 the GPS UART
  // is mapped to header pins IO17 (RX) and IO4 (TX) instead of IO32/IO25,
  // so wiring up a GPS module no longer kills the XPT2046 touchscreen.
  // Features that need GPS (wardrives, GPS Status) still call begin() on
  // entry — that initialises the UART lazily, which keeps the cost of a
  // GPS-less build at zero and avoids fighting NRF24 #1/#3 on boards
  // where no module is wired.

  for (int pin = 0; pin < 8; pin++) {
    Serial.print("Button ");
    Serial.print(pin);
    Serial.print(": ");
    Serial.println(pcf.digitalRead(pin) ? "Released" : "Pressed");
  }

  displayMenu();
  drawStatusBar(currentBatteryVoltage, false);
  last_interaction_time = millis();

  // Wait for any startup touch noise to settle. If the touchscreen reports
  // a stuck/sustained reading at boot, draining it here keeps the new
  // edge-triggered touch handler from immediately auto-selecting a cell.
  {
    uint32_t _bootTouchStart = millis();
    while (ts.touched() && millis() - _bootTouchStart < 1500) delay(20);
  }
}

void loop() {
  handleButtons();      
  //manageBacklight();     
  updateStatusBar(); 
}
