#include "utils.h"

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
         printf("\nDisconnected from %s\n", ACCESS_POINT_NAME);
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
}

bool is_connected_to_wifi()
{
   return (xEventGroupGetBits(wifi_event_group) & WIFI_CONNECTED_BIT) == WIFI_CONNECTED_BIT;
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
