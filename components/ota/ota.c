#include "include/ota.h"

// OTA data write buffer ready to write to the flash
static char ota_write_data[BUFFSIZE + 1] = { 0 };
// Packet receive buffer
static char text[BUFFSIZE + 1] = { 0 };
// Image total length
static int binary_file_length = 0;
// socket id
static int socket_id = -1;

static os_timer_t upgrade_timer;

static void __attribute__((noreturn)) task_fatal_error() {
   #ifdef ALLOW_USE_PRINTF
   printf("Exiting task due to fatal error...");
   #endif

   close(socket_id);
   (void) vTaskDelete(NULL);

   while (1) {}
}

static void esp_ota_firm_init(esp_ota_firm_t *ota_firm, const esp_partition_t *update_partition) {
   memset(ota_firm, 0, sizeof(esp_ota_firm_t));

   ota_firm->state = ESP_OTA_INIT;
   ota_firm->ota_num = get_ota_partition_count();
   ota_firm->update_ota_num = update_partition->subtype - ESP_PARTITION_SUBTYPE_APP_OTA_0;

   #ifdef ALLOW_USE_PRINTF
   printf("Total OTA number %d update to %d part", ota_firm->ota_num, ota_firm->update_ota_num);
   #endif
}

// Read buffer by byte still delim, return read bytes counts
static int read_until(const char *buffer, char delim, int len) {
   // TODO: delim check,buffer check,further: do an buffer length limited
   int i = 0;
   while (buffer[i] != delim && i < len) {
      ++i;
   }
   return i + 1;
}

static bool _esp_ota_firm_parse_http(esp_ota_firm_t *ota_firm, const char *text, size_t total_len, size_t *parse_len) {
   // i means current position
   int i = 0, i_read_len = 0;
   char *ptr = NULL, *ptr2 = NULL;
   char length_str[32];

   while (text[i] != 0 && i < total_len) {
      ptr = (char *) strstr(text, "Content-Length");

      if (ota_firm->content_len == 0 && ptr != NULL) {
         ptr += 16;
         ptr2 = (char *) strstr(ptr, "\r\n");

         memset(length_str, 0, sizeof(length_str));
         memcpy(length_str, ptr, ptr2 - ptr);

         ota_firm->content_len = atoi(length_str);

         ota_firm->ota_size = ota_firm->content_len;
         ota_firm->ota_offset = 0;

         #ifdef ALLOW_USE_PRINTF
         printf("Parse Content-Length: %d, ota_size %d", ota_firm->content_len, ota_firm->ota_size);
         #endif
      }

      i_read_len = read_until(&text[i], '\n', total_len - i);

      if (i_read_len > total_len - i) {
         #ifdef ALLOW_USE_PRINTF
         printf("recv. malformed HTTP header");
         #endif

         task_fatal_error();
      }

      // if resolve \r\n line, HTTP header is finished
      if (i_read_len == 2) {
         if (ota_firm->content_len == 0) {
            #ifdef ALLOW_USE_PRINTF
            printf("Did not parse Content-Length item");
            #endif

            task_fatal_error();
         }

         *parse_len = i + 2;

         return true;
      }

      i += i_read_len;
   }
   return false;
}

static size_t esp_ota_firm_do_parse_msg(esp_ota_firm_t *ota_firm, const char *in_buf, size_t in_len) {
   size_t tmp;
   size_t parsed_bytes = in_len;

   switch (ota_firm->state) {
      case ESP_OTA_INIT:
         if (_esp_ota_firm_parse_http(ota_firm, in_buf, in_len, &tmp)) {
            ota_firm->state = ESP_OTA_PREPARE;

            #ifdef ALLOW_USE_PRINTF
            printf("HTTP parsed %d bytes", tmp);
            #endif

            parsed_bytes = tmp;
         }

         break;
      case ESP_OTA_PREPARE:
         ota_firm->read_bytes += in_len;

         if (ota_firm->read_bytes >= ota_firm->ota_offset) {
            ota_firm->buf = &in_buf[in_len - (ota_firm->read_bytes - ota_firm->ota_offset)];
            ota_firm->bytes = ota_firm->read_bytes - ota_firm->ota_offset;
            ota_firm->write_bytes += ota_firm->read_bytes - ota_firm->ota_offset;
            ota_firm->state = ESP_OTA_START;

            #ifdef ALLOW_USE_PRINTF
            printf("Received %d bytes and start to update", ota_firm->read_bytes);
            printf("Write %d total %d", ota_firm->bytes, ota_firm->write_bytes);
            #endif
         }

         break;
      case ESP_OTA_START:
         if (ota_firm->write_bytes + in_len > ota_firm->ota_size) {
            ota_firm->bytes = ota_firm->ota_size - ota_firm->write_bytes;
            ota_firm->state = ESP_OTA_RECVED;
         } else {
            ota_firm->bytes = in_len;
         }

         ota_firm->buf = in_buf;
         ota_firm->write_bytes += ota_firm->bytes;

         #ifdef ALLOW_USE_PRINTF
         printf("Write %d total %d", ota_firm->bytes, ota_firm->write_bytes);
         #endif

         break;
      case ESP_OTA_RECVED:
         parsed_bytes = 0;
         ota_firm->state = ESP_OTA_FINISH;

         break;
      default:
         parsed_bytes = 0;

         #ifdef ALLOW_USE_PRINTF
         printf("State is %d", ota_firm->state);
         #endif

         break;
   }

   return parsed_bytes;
}

static void esp_ota_firm_parse_msg(esp_ota_firm_t *ota_firm, const char *in_buf, size_t in_len) {
   size_t parse_bytes = 0;

   #ifdef ALLOW_USE_PRINTF
   printf("Input %d bytes", in_len);
   #endif

   do {
      size_t bytes = esp_ota_firm_do_parse_msg(ota_firm, in_buf + parse_bytes, in_len - parse_bytes);

      #ifdef ALLOW_USE_PRINTF
      printf("Parsed %d bytes", bytes);
      #endif

      if (bytes) {
         parse_bytes += bytes;
      }
   } while (parse_bytes != in_len);
}

static inline int esp_ota_firm_can_write(esp_ota_firm_t *ota_firm) {
   return (ota_firm->state == ESP_OTA_START || ota_firm->state == ESP_OTA_RECVED);
}

static inline const char *esp_ota_firm_get_write_buf(esp_ota_firm_t *ota_firm) {
   return ota_firm->buf;
}

static inline size_t esp_ota_firm_get_write_bytes(esp_ota_firm_t *ota_firm) {
   return ota_firm->bytes;
}

static inline int esp_ota_firm_is_finished(esp_ota_firm_t *ota_firm) {
   return ota_firm->state == ESP_OTA_FINISH || ota_firm->state == ESP_OTA_RECVED;
}

static void update_firmware_task(void *pvParameter) {
   esp_err_t err;
   // update handle : set by esp_ota_begin(), must be freed via esp_ota_end()
   esp_ota_handle_t update_handle = 0;
   const esp_partition_t *update_partition = NULL;

   #ifdef ALLOW_USE_PRINTF
   printf("Starting OTA... Flash: %s", CONFIG_ESPTOOLPY_FLASHSIZE);
   #endif

   /*const esp_partition_t *configured = esp_ota_get_boot_partition();
   assert(configured != NULL);
   const esp_partition_t *running = esp_ota_get_running_partition();
   assert(running != NULL);

   if (configured != running) {
      ESP_LOGI(TAG, "Configured OTA boot partition at offset 0x%X, but running from offset 0x%X", configured->address, running->address);
      ESP_LOGI(TAG, "(This can happen if either the OTA boot data or preferred boot image become corrupted somehow.)");
   }
   ESP_LOGI(TAG, "Running partition type 0x%X subtype 0x%X (offset 0x%X)", running->type, running->subtype, running->address);*/

   const char *request_parameters[] = {"firmware.bin", SERVER_IP_ADDRESS, NULL};
   char *http_request = set_string_parameters(FIRMWARE_UPDATE_GET_REQUEST, request_parameters);

   #ifdef ALLOW_USE_PRINTF
   printf("GET HTTP request: %s", http_request);
   #endif

   socket_id = connect_to_http_server();

   if (socket_id == -1) {
      free(http_request);

      #ifdef ALLOW_USE_PRINTF
      printf("Error on server connection for updating");
      #endif

      task_fatal_error();
   }

   int res = send(socket_id, http_request, strlen(http_request), 0);

   free(http_request);

   if (res < 0) {
      #ifdef ALLOW_USE_PRINTF
      printf("Send GET request to server failed");
      #endif

      task_fatal_error();
   } else {
      #ifdef ALLOW_USE_PRINTF
      printf("Send GET request to server succeeded");
      #endif
   }

   update_partition = esp_ota_get_next_update_partition(NULL);
   assert(update_partition != NULL);

   #ifdef ALLOW_USE_PRINTF
   printf("Writing to partition subtype %d at offset 0x%X", update_partition->subtype, update_partition->address);
   #endif

   err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);

   if (err != ESP_OK) {
      #ifdef ALLOW_USE_PRINTF
      printf("esp_ota_begin failed, error=%d", err);
      #endif

      task_fatal_error();
   }
   #ifdef ALLOW_USE_PRINTF
   printf("esp_ota_begin succeeded");
   #endif

   bool flag = true;
   esp_ota_firm_t ota_firm;

   esp_ota_firm_init(&ota_firm, update_partition);

   // deal with all receive packet
   while (flag) {
      memset(text, 0, TEXT_BUFFSIZE);
      memset(ota_write_data, 0, BUFFSIZE);

      int buff_len = recv(socket_id, text, TEXT_BUFFSIZE, 0);

      if (buff_len < 0) { // receive error
         #ifdef ALLOW_USE_PRINTF
         printf("Error: receive data error! Error no.=%d", errno);
         #endif

         task_fatal_error();
      } else if (buff_len > 0) { // deal with response body
         esp_ota_firm_parse_msg(&ota_firm, text, buff_len);

         if (!esp_ota_firm_can_write(&ota_firm)) {
            continue;
         }

         memcpy(ota_write_data, esp_ota_firm_get_write_buf(&ota_firm), esp_ota_firm_get_write_bytes(&ota_firm));
         buff_len = esp_ota_firm_get_write_bytes(&ota_firm);

         err = esp_ota_write(update_handle, (const void *) ota_write_data, buff_len);

         if (err != ESP_OK) {
            #ifdef ALLOW_USE_PRINTF
            printf("Error: esp_ota_write failed! err=0x%X", err);
            #endif

            task_fatal_error();
         }

         binary_file_length += buff_len;
         //ESP_LOGI(TAG, "Have written image length %d", binary_file_length);
      } else if (buff_len == 0) { // packet over
         flag = false;

         #ifdef ALLOW_USE_PRINTF
         printf("Connection closed, all packets received");
         #endif

         close(socket_id);
      } else {
         #ifdef ALLOW_USE_PRINTF
         printf("Unexpected recv. result");
         #endif
      }

      if (esp_ota_firm_is_finished(&ota_firm)) {
         break;
      }
   }

   #ifdef ALLOW_USE_PRINTF
   printf("Total write binary data length : %d", binary_file_length);
   #endif

   if (esp_ota_end(update_handle) != ESP_OK) {
      #ifdef ALLOW_USE_PRINTF
      printf("esp_ota_end failed!");
      #endif

      task_fatal_error();
   }

   err = esp_ota_set_boot_partition(update_partition);

   if (err != ESP_OK) {
      #ifdef ALLOW_USE_PRINTF
      printf("esp_ota_set_boot_partition failed! err=0x%X", err);
      #endif

      task_fatal_error();
   }

   #ifdef ALLOW_USE_PRINTF
   printf("Prepare to restart system!");
   #endif

   esp_restart();
}

static void on_update_timeout() {
   #ifdef ALLOW_USE_PRINTF
   printf("Update timeout");
   #endif

   esp_restart();
}

void update_firmware() {
   os_timer_disarm(&upgrade_timer);
   os_timer_setfn(&upgrade_timer, (os_timer_func_t *) on_update_timeout, NULL);
   os_timer_arm(&upgrade_timer, 300000, false);

   xTaskCreate(update_firmware_task, "update_firmware_task", configMINIMAL_STACK_SIZE * 7, NULL, 5, NULL);
}
