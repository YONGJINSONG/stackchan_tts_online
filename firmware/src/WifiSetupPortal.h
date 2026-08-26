#ifndef _WIFI_SETUP_PORTAL_H
#define _WIFI_SETUP_PORTAL_H

// Blocking SoftAP captive portal. Saves credentials to SPIFFS /wifi.json then reboots.
// Call when STA Wi-Fi connection has failed. Does not return on success (ESP.restart).
void wifi_setup_portal_run(void);

#endif  // _WIFI_SETUP_PORTAL_H
