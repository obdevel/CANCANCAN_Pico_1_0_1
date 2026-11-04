
/*
  Copyright (C) Duncan Greenwood 2026 (duncan_greenwood@hotmail.com)

  This work is licensed under the:
      Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
   To view a copy of this license, visit:
      http://creativecommons.org/licenses/by-nc-sa/4.0/
   or send a letter to Creative Commons, PO Box 1866, Mountain View, CA 94042, USA.

   License summary:
    You are free to:
      Share, copy and redistribute the material in any medium or format
      Adapt, remix, transform, and build upon the material

    The licensor cannot revoke these freedoms as long as you follow the license terms.

    Attribution : You must give appropriate credit, provide a link to the license,
                  and indicate if changes were made. You may do so in any reasonable manner,
                  but not in any way that suggests the licensor endorses you or your use.

    NonCommercial : You may not use the material for commercial purposes. **(see note below)

    ShareAlike : If you remix, transform, or build upon the material, you must distribute
                 your contributions under the same license as the original.

    No additional restrictions : You may not apply legal terms or technological measures that
                                 legally restrict others from doing anything the license permits.

   ** For commercial use, please contact the original copyright holder(s) to agree licensing terms

    This software is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE
*/

#include <CBUSLED.h>     // CBUS LEDs
#include <CBUSconfig.h>  // CBUS module configuration

#include <cerrno>

#include "defs.h"
#include "enums.h"
#include "types.h"
#include "funcs.h"

#if defined(IS_NETWORK_DEVICE)

/// external globals

#include <WebServer.h>

extern WebServer *webserver;
extern network_config_t netcfg;
extern CBUSLED error_led;

/// webserver pages

// typedef struct {
//   byte wifi_mode;        // STA or AP
//   char hostname[32];     // DNS hostname
//   char ssid[32];         // wfi SSID
//   char password[32];     // wifi password
//   char ap_ssid[32];      // access point SSID
//   char ap_password[32];  // access point password
//   bool hide_ap;          // hide AP
//   byte ap_channel;       // channel in AP mode
//   char servicename[32];  // DNS-SD service name
//   char server[32];       // server (if net client)
//   uint16_t port;         // network port number
//   char servername[32];   // server name to connect to if client
//   byte check_value;      // set to 99 if config is valid
// } network_config_t;

String html_form_wifi = "<html>"
                        "<head>"
                        "<title>CANCANCAN Network Router Configuration</title>"
                        "</head>"
                        "<body>"
                        "<h2>CANCANCAN Network Router Configuration</h2><br>"
                        "<form method='post' enctype='application/x-www-form-urlencoded' action='/postform'>"
                        "<div>"
                        "Code version: {{VER}}<br>"
                        "Network type: {{NET_TYPE}}<br>"
                        "</div>"
                        "<div>"
                        "<br>"
                        "WiFi mode: "
                        "<input type='radio' name='mode' value='STA' {{MODE_STA}}> Station "
                        "<input type='radio' name='mode' value='AP' {{MODE_AP}}> Access point"
                        "</div>"
                        "<br>"
                        "STA SSID: <input type='text' name='ssid' size='32' value='{{SSID}}'><br>"
                        "STA password: <input type='password' name='password' size='32' value='{{PASSWORD}}'><br>"
                        "<br>"
                        "AP SSID: <input type='text' name='ap_ssid' size='32' value='{{AP_SSID}}'><br>"
                        "AP password: <input type='password' name='ap_password' size='32' value='{{AP_PASSWORD}}'><br>"
                        "<br>"
                        "Hostname: <input type='text' name='hostname' size='32' value='{{HOSTNAME}}'> (.local)<br>"
                        "Server: <input type='text' name='server' size='32' value='{{SERVER}}'> (.local)<br>"
                        "Port: <input type='number' name='port' value='{{PORT}}'><br>"
                        "DNS-SD service: <input type='text' name='service' size='32' value='{{SERVICE}}'><br>"
                        "</div>"
                        "<br>"
                        "<input type='submit' value='Save and restart'>"
                        "</form>"
                        "</body>"
                        "</html>";

/// webserver page handlers

/// send the configuration form

void handle_root(void) {

  char vbuff[16];
  String &html_form = html_form_wifi;

  VLOG("webserver: handling root");

  sprintf(vbuff, "%u%c%u", VER_MAJ, VER_MIN, VER_BETA);
  html_form.replace("{{VER}}", vbuff);

  if (netcfg.wifi_mode == WIFI_MODE_STA) {
    html_form.replace("{{MODE_STA}}", "checked");
    html_form.replace("{{MODE_AP}}", "");
  } else {
    html_form.replace("{{MODE_STA}}", "");
    html_form.replace("{{MODE_AP}}", "checked");
  }

  html_form.replace("{{SSID}}", String(netcfg.ssid));
  html_form.replace("{{PASSWORD}}", String(netcfg.password));
  html_form.replace("{{AP_SSID}}", String(netcfg.ap_ssid));
  html_form.replace("{{AP_PASSWORD}}", String(netcfg.ap_password));
  html_form.replace("{{SERVICE}}", String(netcfg.servicename));
  html_form.replace("{{HOSTNAME}}", String(netcfg.hostname));
  html_form.replace("{{SERVER}}", String(netcfg.server));
  html_form.replace("{{PORT}}", String(netcfg.port));

#if defined(IS_WIFI_DEVICE)
  html_form.replace("{{NET_TYPE}}", "WiFi");
#endif

#if defined(IS_ETHERET_DEVICE)
  html_form.replace("{{NET_TYPE}}", "Ethernet");
#endif

  webserver->send(200, "text/html", html_form);
  return;
}

/// process the submitted form data

void handle_form(void) {

  VLOG("webserver handling form POST");

  if (webserver->method() != HTTP_POST) {
    webserver->send(405, "text/plain", "method not allowed");
  } else {
    String message = "POST form data:\n";

    for (uint8_t i = 0; i < webserver->args(); i++) {
      message += (" " + webserver->argName(i) + ": " + webserver->arg(i) + "\n");
    }

    message += "config saved, rebooting now";
    webserver->send(200, "text/plain", message);
    VLOG(message.c_str());

    netcfg.wifi_mode = (webserver->arg("mode") == "STA") ? WIFI_MODE_STA : WIFI_MODE_AP;
    strcpy(netcfg.ssid, webserver->arg("ssid").c_str());
    strcpy(netcfg.password, webserver->arg("password").c_str());
    strcpy(netcfg.ap_ssid, webserver->arg("ap_ssid").c_str());
    strcpy(netcfg.ap_password, webserver->arg("ap_password").c_str());
    strcpy(netcfg.servicename, webserver->arg("service").c_str());
    strcpy(netcfg.hostname, webserver->arg("hostname").c_str());
    strcpy(netcfg.server, webserver->arg("server").c_str());
    netcfg.port = atoi(webserver->arg("port").c_str());

    save_network_config(&netcfg);
    delay(500);
    rp2040.reboot();
  }

  return;
}

/// handle requested resource not found

void handle_not_found(void) {

  VLOG("webserver: *** handling URI not found - %s", webserver->uri().c_str());
  webserver->send(404, "text/plain", "resource not found");
  error_led.pulse();
  return;
}

///
#endif
