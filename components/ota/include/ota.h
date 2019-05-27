#include "esp_ota_ops.h"
#include "esp_log.h"
#include "global_definitions.h"
#include "sdkconfig.h"
#include "esp_system.h"

#include "freertos/FreeRTOS.h"
#include "event_groups.h"

#include "utils.h"

#include "sys/socket.h"

#define BUFFSIZE 1500
#define TEXT_BUFFSIZE 1024

typedef enum esp_ota_firm_state {
   ESP_OTA_INIT = 0,
   ESP_OTA_PREPARE,
   ESP_OTA_START,
   ESP_OTA_RECVED,
   ESP_OTA_FINISH,
} esp_ota_firm_state_t;

typedef struct esp_ota_firm {
   uint8_t ota_num;
   uint8_t update_ota_num;

   esp_ota_firm_state_t state;

   size_t content_len;

   size_t read_bytes;
   size_t write_bytes;

   size_t ota_size;
   size_t ota_offset;

   const char *buf;
   size_t bytes;
} esp_ota_firm_t;

// Send GET request to HTTP server
static const char FIRMWARE_UPDATE_GET_REQUEST[] =
      "GET /esp8266_fota/<1> HTTP/1.1\r\n"
      "Host: <2>\r\n"
      "User-Agent: ESP8266\r\n"
      "Connection: close\r\n\r\n";

void update_firmware();
