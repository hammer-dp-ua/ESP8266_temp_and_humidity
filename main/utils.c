#include "utils.h"

static const char FAILED_TO_ALLOCATE_SOCKET_MSG[] = "\nFailed to allocate socket\n";
static const char ALLOCATED_SOCKET_MSG[] = "\nSocket %d has been allocated\n";
static const char SOCKET_CONNECTION_ERROR_MSG[] = "\nSocket connection failed. Error: %d\n";
static const char SOCKET_CONNECTED_MSG[] = "\nSocket %d has been connected\n";
static const char NOT_CONNECTED_TO_WI_FI_MSG[] = "\nNot connected to Wi-Fi. To be deleted task\n";
static const char ERROR_ON_SENT_REQUEST_MSG[] = "\nError occurred during sending. Error no.: %d\n";
static const char SUCCESSFULLY_SENT_REQUEST_MSG[] = "\nRequest has been sent. Socket %d\n";
static const char ERROR_ON_RECEIVE_RESPONSE_MSG[] = "\nReceive failed. Error no.: %d\n";
static const char SHUTTING_DOWN_SOCKET_MSG[] = "Shutting down socket and restarting...";

// FreeRTOS event group to signal when we are connected
// Max 24 bits when configUSE_16_BIT_TICKS is 0
static EventGroupHandle_t wifi_event_group;
/*
 * The event group allows multiple bits for each event,
 * but we only care about one event - are we connected
 * to the AP with an IP?
 */
static const int WIFI_CONNECTED_BIT = (1 << 0);

static void (*on_wifi_connected)();
static void (*on_wifi_disconnected)();
static void (*on_wifi_connection)();

/**
 * Do not forget to call free() function on returned pointer when it's no longer needed.
 *
 * *parameters - array of pointers to strings. The last parameter has to be NULL
 */
void *set_string_parameters(const char string[], const char *parameters[]) {
   unsigned char open_brace_found = 0;
   unsigned char parameters_amount = 0;
   unsigned short result_string_length = 0;

   for (; parameters[parameters_amount] != NULL; parameters_amount++) {
   }

   // Calculate the length without symbols to be replaced ('<x>')
   const char *string_pointer;
   for (string_pointer = string; *string_pointer != '\0'; string_pointer++) {
      if (*string_pointer == '<') {
         if (open_brace_found) {
            return NULL;
         }
         open_brace_found = 1;
         continue;
      }
      if (*string_pointer == '>') {
         if (!open_brace_found) {
            return NULL;
         }
         open_brace_found = 0;
         continue;
      }
      if (open_brace_found) {
         continue;
      }

      result_string_length++;
   }

   if (open_brace_found) {
      return NULL;
   }

   unsigned char i;
   for (i = 0; parameters[i] != NULL; i++) {
      result_string_length += strnlen(parameters[i], 0xFFFF);
   }
   // 1 is for the last \0 character
   result_string_length++;

   char *allocated_result = MALLOC(result_string_length, __LINE__, 0xFFFF); // (string_length + 1) * sizeof(char)

   if (allocated_result == NULL) {
      return NULL;
   }

   unsigned short result_string_index = 0, input_string_index = 0;
   for (; result_string_index < result_string_length - 1; result_string_index++) {
      char input_string_char = string[input_string_index];

      if (input_string_char == '<') {
         input_string_index++;
         input_string_char = string[input_string_index];

         if (input_string_char < '1' || input_string_char > '9') {
            return NULL;
         }

         unsigned short parameter_numeric_value = input_string_char - '0';
         if (parameter_numeric_value > parameters_amount) {
            return NULL;
         }

         input_string_index++;
         input_string_char = string[input_string_index];

         if (input_string_char >= '0' && input_string_char <= '9') {
            parameter_numeric_value = parameter_numeric_value * 10 + input_string_char - '0';
            input_string_index++;
         }
         input_string_index++;

         // Parameters are starting with 1
         const char *parameter = parameters[parameter_numeric_value - 1];

         for (; *parameter != '\0'; parameter++, result_string_index++) {
            *(allocated_result + result_string_index) = *parameter;
         }
         result_string_index--;
      } else {
         *(allocated_result + result_string_index) = string[input_string_index];
         input_string_index++;
      }
   }
   *(allocated_result + result_string_length - 1) = '\0';
   return allocated_result;
}

bool compare_strings(char *string1, char *string2) {
   if (string1 == NULL || string2 == NULL) {
      return false;
   }

   bool result = true;
   unsigned short i = 0;

   while (result) {
      char string1_character = *(string1 + i);
      char string2_character = *(string2 + i);

      if (string1_character == '\0' && string2_character == '\0') {
         break;
      } else if (string1_character == '\0' || string2_character == '\0' || string1_character != string2_character) {
         result = false;
      }
      i++;
   }
   return result;
}

char *put_flash_string_into_heap(const char *flash_string, unsigned int allocated_time) {
   if (flash_string == NULL) {
      return NULL;
   }

   unsigned short string_length = strlen(flash_string);

   char *heap_string = MALLOC(string_length + 1, __LINE__, allocated_time);

   for (unsigned short i = 0; i < string_length; i++) {
      *(heap_string + i) = flash_string[i];
   }
   *(heap_string + string_length) = '\0';
   return heap_string;
}

static esp_err_t esp_event_handler(void *ctx, system_event_t *event) {
   switch(event->event_id) {
      case SYSTEM_EVENT_STA_START:
         esp_wifi_connect();
         on_wifi_connection();

         #ifdef ALLOW_USE_PRINTF
         printf("\nSYSTEM_EVENT_STA_START event\n");
         /*wifi_scan_config_t scan_config;
         scan_config.scan_type = WIFI_SCAN_TYPE_PASSIVE;
         scan_config.scan_time.passive = 1500;
         scan_config.scan_time.active.min = 500;
         scan_config.scan_time.active.max = 1500;
         esp_wifi_scan_start(&scan_config, false);*/
         #endif
         break;
      case SYSTEM_EVENT_SCAN_DONE:
         #ifdef ALLOW_USE_PRINTF
         printf("\nScan status: %u, number: %u, scan id: %u",
               event->event_info.scan_done.status, event->event_info.scan_done.number, event->event_info.scan_done.scan_id);

         unsigned short number = 10;
         wifi_ap_record_t ap_records[10];

         ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&number, ap_records));
         printf("\nScanned %u access points", number);
         for (unsigned char i = 0; i < number; i++) {
            printf("\nScan index: %u, ssid: %s, rssi: %d", i, ap_records[i].ssid, ap_records[i].rssi);
         }
         printf("\n");
         #endif

         break;
      case SYSTEM_EVENT_STA_GOT_IP:
         #ifdef ALLOW_USE_PRINTF
         printf("Got IP: %s", ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
         #endif

         xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
         on_wifi_connected();
         break;
      case SYSTEM_EVENT_AP_STACONNECTED:
         #ifdef ALLOW_USE_PRINTF
         printf("\nStation: "MACSTR" join, AID=%d\n", MAC2STR(event->event_info.sta_connected.mac), event->event_info.sta_connected.aid);
         #endif

         break;
      case SYSTEM_EVENT_AP_STADISCONNECTED:
         #ifdef ALLOW_USE_PRINTF
         printf("\nStation: "MACSTR" leave, AID=%d\n", MAC2STR(event->event_info.sta_disconnected.mac), event->event_info.sta_disconnected.aid);
         #endif

         break;
      case SYSTEM_EVENT_STA_DISCONNECTED:
         #ifdef ALLOW_USE_PRINTF
         // See reason info in wifi_err_reason_t of esp_wifi_types.h
         printf("\nDisconnected from %s, reason: %u\n", event->event_info.disconnected.ssid, event->event_info.disconnected.reason);
         #endif

         on_wifi_disconnected();
         //esp_wifi_connect();
         on_wifi_connection();
         xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
         break;
      default:
         break;
   }
   return ESP_OK;
}

void wifi_init_sta(void (*on_connected)(), void (*on_disconnected)(), void (*on_connection)()) {
   on_wifi_connected = on_connected;
   on_wifi_disconnected = on_disconnected;
   on_wifi_connection = on_connection;

   wifi_event_group = xEventGroupCreate();

   ESP_ERROR_CHECK(esp_event_loop_init(esp_event_handler, NULL));

   wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
   ESP_ERROR_CHECK(esp_wifi_init(&cfg));

   wifi_config_t wifi_config;
   memcpy(&wifi_config.sta.ssid, ACCESS_POINT_NAME, 32);
   memcpy(&wifi_config.sta.password, ACCESS_POINT_PASSWORD, 64);

   ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
   ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
   ESP_ERROR_CHECK(esp_wifi_start());

   #ifdef ALLOW_USE_PRINTF
   printf("\nwifi_init_sta finished\n");
   #endif

   /*#ifdef ALLOW_USE_PRINTF
   wifi_scan_config_t scan_config;

   esp_wifi_scan_start(&scan_config, true);
   esp_wifi_scan_get_ap_records
   #endif*/
}

bool is_connected_to_wifi() {
   return (xEventGroupGetBits(wifi_event_group) & WIFI_CONNECTED_BIT) == WIFI_CONNECTED_BIT;
}

/**
  * @brief     Read user data from the RTC memory.
  *
  *            The user data segment (512 bytes, as shown below) is used to store user data.
  *
  *             |<---- system data(256 bytes) ---->|<----------- user data(512 bytes) --------->|
  *
  * @attention Read and write unit for data stored in the RTC memory is 4 bytes.
  * @attention src_addr is the block number (4 bytes per block). So when reading data
  *            at the beginning of the user data segment, src_addr will be 256/4 = 64,
  *            n will be data length.
  *
  * @param     unsigned char addr :    source address of rtc memory, addr >= 64
  * @param     void *dst :             data pointer
  * @param     unsigned short length : data length, unit: byte
  *
  * @return    true  : succeed
  * @return    false : fail
  */
bool rtc_mem_read(unsigned short addr, void *dst, unsigned short length) {
   short blocks;

   // validate reading a user block
   if (addr < 64) return false;
   if (dst == 0) return false;
   // validate 4 byte aligned
   if (((unsigned int)dst & 0x3) != 0) return false;
   // validate length is multiple of 4
   if ((length & 0x3) != 0) return false;

   // check valid length from specified starting point
   if (length > ((256 + 512) - (addr * 4))) return false;

   // copy the data
   for (blocks = (length >> 2) - 1; blocks >= 0; blocks--) {
      volatile unsigned int *ram = ((unsigned int*)dst) + blocks;
      volatile unsigned int *rtc = ((unsigned int*)PERIPHS_RTC_BASEADDR) + addr + blocks;
      *rtc = *ram;
   }

   return true;
}

/**
  * @brief     Write user data to  the RTC memory.
  *
  *            During deep-sleep, only RTC is working. So users can store their data
  *            in RTC memory if it is needed. The user data segment below (512 bytes)
  *            is used to store the user data.
  *
  *            |<---- system data(256 bytes) ---->|<----------- user data(512 bytes) --------->|
  *
  * @attention Read and write unit for data stored in the RTC memory is 4 bytes.
  * @attention src_addr is the block number (4 bytes per block). So when storing data
  *            at the beginning of the user data segment, src_addr will be 256/4 = 64,
  *            n will be data length.
  *
  * @param     unsigned short dst :    destination address of rtc memory, dst >= 64
  * @param     const void *src :       data pointer
  * @param     unsigned short length : data length, unit: byte
  *
  * @return    true  : succeed
  * @return    false : fail
  */
bool rtc_mem_write(unsigned short dst, const void *src, unsigned short length) {
   short blocks;

   // validate reading a user block
   if (dst < 64) return false;
   if (src == 0) return false;
   // validate 4 byte aligned
   if (((unsigned int)src & 0x3) != 0) return false;
   // validate length is multiple of 4
   if ((length & 0x3) != 0) return false;

   // check valid length from specified starting point
   if (length > ((256 + 512) - (dst))) return false;

   // copy the data
   for (blocks = (length >> 2) - 1; blocks >= 0; blocks--) {
      volatile unsigned int *ram = ((unsigned int*)src) + blocks;
      volatile unsigned int *rtc = ((unsigned int*)PERIPHS_RTC_BASEADDR) + dst + blocks;
      *rtc = *ram;
   }

   return true;
}

int connect_to_http_server() {
   int socket_id = socket(AF_INET, SOCK_STREAM, IPPROTO_IP); // SOCK_STREAM - TCP

   if (socket_id < 0) {
      #ifdef ALLOW_USE_PRINTF
      printf(FAILED_TO_ALLOCATE_SOCKET_MSG);
      #endif

      return -1;
   }
   #ifdef ALLOW_USE_PRINTF
   printf(ALLOCATED_SOCKET_MSG, socket_id);
   #endif

   struct sockaddr_in destination_address;
   destination_address.sin_addr.s_addr = inet_addr(SERVER_IP_ADDRESS);
   destination_address.sin_family = AF_INET;
   destination_address.sin_port = htons(SERVER_PORT);

   int connection_result = connect(socket_id, (struct sockaddr *) &destination_address, sizeof(destination_address));

   if (connection_result != 0) {
      #ifdef ALLOW_USE_PRINTF
      printf(SOCKET_CONNECTION_ERROR_MSG, connection_result);
      #endif

      close(socket_id);
      return -1;
   }

   #ifdef ALLOW_USE_PRINTF
   printf(SOCKET_CONNECTED_MSG, socket_id);
   #endif

   if (!is_connected_to_wifi()) {
      #ifdef ALLOW_USE_PRINTF
      printf(NOT_CONNECTED_TO_WI_FI_MSG);
      #endif

      close(socket_id);
      return -1;
   }
   return socket_id;
}

char *send_request(char *request, unsigned short response_buffer_size, unsigned int invocation_time) {
   int socket_id = connect_to_http_server();

   if (socket_id < 0) {
      return NULL;
   }

   unsigned short received_bytes_amount = 0;
   char *final_response_result = MALLOC(response_buffer_size, __LINE__, invocation_time);

   for (;;) {
      int send_result = send(socket_id, request, strlen(request), 0);

      if (send_result < 0) {
         #ifdef ALLOW_USE_PRINTF
         printf(ERROR_ON_SENT_REQUEST_MSG, send_result);
         #endif

         break;
      }
      #ifdef ALLOW_USE_PRINTF
      printf(SUCCESSFULLY_SENT_REQUEST_MSG, socket_id);
      #endif

      unsigned char tmp_buffer_size = response_buffer_size <= 255 ? response_buffer_size : 255;
      char tmp_buffer[tmp_buffer_size];
      int len = recv(socket_id, tmp_buffer, tmp_buffer_size - 1, 0);
      tmp_buffer[len] = '\0';

      if (len < 0) {
         #ifdef ALLOW_USE_PRINTF
         printf(ERROR_ON_RECEIVE_RESPONSE_MSG, len);
         #endif

         break;
      } else if (len == 0) {
         final_response_result[received_bytes_amount] = '\0';

         #ifdef ALLOW_USE_PRINTF
         printf("\nFinal response: %s\n", final_response_result);
         #endif

         break;
      } else {
         bool max_length_exceed = false;

         for (unsigned short i = 0; i < len; i++) {
            unsigned short addend = received_bytes_amount + i;

            if (addend >= response_buffer_size) {
               max_length_exceed = true;
               received_bytes_amount = response_buffer_size;
               break;
            }

            *(final_response_result + addend) = tmp_buffer[i];
         }
         received_bytes_amount += max_length_exceed ? 0 : len;

         #ifdef ALLOW_USE_PRINTF
         printf("\nReceived %d bytes\n", len);
         printf("\nResponse: %s\n", tmp_buffer);
         #endif
      }
   }

   #ifdef ALLOW_USE_PRINTF
   printf(SHUTTING_DOWN_SOCKET_MSG);
   #endif

   shutdown(socket_id, 0);
   close(socket_id);

   return final_response_result;
}

/*
 * Added into SDK's esp_system_internal.h:
 * uint32_t get_reset_reason_custom();
 *
 * Added into SDK's reset_reason.c:
   uint32_t get_reset_reason_custom()
   {
      const uint32_t hw_reset = esp_rtc_get_reset_reason();
      const uint32_t hint = esp_reset_reason_get_hint(hw_reset);
      return get_reset_reason(hw_reset, hint);
   }
 */
