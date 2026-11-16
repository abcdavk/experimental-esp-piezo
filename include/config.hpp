#pragma once

#include <stdint.h>

struct Config
{
  char wifi_ssid[33];
  char wifi_password[64];
  char server_host[12];
  uint16_t server_port;
  char server_path[64];
};

Config loadConfig();
