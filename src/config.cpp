#include "config.hpp"

#include <SimpleIni.h>
#include <cstring>

Config loadConfig()
{
  CSimpleIni ini;
  ini.LoadFile("config.ini");
  Config config;

  strncpy(config.wifi_ssid, ini.GetValue("Wifi", "ssid", ""), sizeof(config.wifi_ssid) - 1);
  strncpy(config.wifi_password, ini.GetValue("Wifi", "password", ""), sizeof(config.wifi_password) - 1);
  strncpy(config.server_host, ini.GetValue("Server", "host", ""), sizeof(config.server_host) - 1);

  config.server_port = ini.GetLongValue("Server", "port", 0);

  strncpy(config.server_path, ini.GetValue("Server", "path", ""), sizeof(config.server_path) - 1);

  return config;
}
