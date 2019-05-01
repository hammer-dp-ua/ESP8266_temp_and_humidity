#include "utils.h"

// FreeRTOS event group to signal when we are connected
static EventGroupHandle_t wifi_event_group;

/*
 * The event group allows multiple bits for each event,
 * but we only care about one event - are we connected
 * to the AP with an IP?
 */
const int WIFI_CONNECTED_BIT = BIT0;

void set_flag(unsigned int *flags, unsigned int flag) {
   *flags |= flag;
}

void reset_flag(unsigned int *flags, unsigned int flag) {
   *flags &= ~(*flags & flag);
}

bool read_flag(unsigned int flags, unsigned int flag) {
   return (flags & flag) ? true : false;
}

/**
 * Do not forget to call free() function on returned pointer when it's no longer needed
 *
 * *parameters - array of pointers to strings. The last parameter has to be NULL
 */
void *set_string_parameters(char string[], char *parameters[]) {
   unsigned char open_brace_found = 0;
   unsigned char parameters_amount = 0;
   unsigned short result_string_length = 0;

   for (; parameters[parameters_amount] != NULL; parameters_amount++) {
   }

   // Calculate the length without symbols to be replaced ('<x>')
   char *string_pointer;
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

   char *allocated_result = MALLOC(result_string_length, __LINE__, 0xFFFFFFFF); // (string_length + 1) * sizeof(char)

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
         char *parameter = parameters[parameter_numeric_value - 1];

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

LOCAL void calculate_rom_string_length_or_fill_malloc(unsigned short *string_length, char *result, const char *rom_string) {
   unsigned char calculate_string_length = *string_length ? false : true;
   unsigned short calculated_string_length = 0;
   unsigned int *rom_string_aligned = (unsigned int*) (((unsigned int) (rom_string)) & ~3); // Could be saved in not 4 bytes aligned address
   unsigned int rom_string_aligned_value = *rom_string_aligned;
   unsigned char shifted_bytes = (unsigned char) ((unsigned int) (rom_string) - (unsigned int) (rom_string_aligned)); // 0 - 3
   bool prematurely_stopped = false;

   unsigned char shifted_bytes_tmp = shifted_bytes;
   while (shifted_bytes_tmp < 4) {
      unsigned int comparable = 0xFF;
      unsigned char bytes_to_shift = shifted_bytes_tmp * 8;
      comparable <<= bytes_to_shift;
      unsigned int current_character_shifted = rom_string_aligned_value & comparable;

      if (current_character_shifted == 0) {
         prematurely_stopped = true;
         break;
      }
      shifted_bytes_tmp++;

      if (!calculate_string_length) {
         char current_character = (char) (current_character_shifted >> bytes_to_shift);
         *(result + calculated_string_length) = current_character;
      }

      calculated_string_length++;
   }

   if (!calculated_string_length) {
      return;
   }

   unsigned int *rom_string_aligned_next = rom_string_aligned + 1;
   while (prematurely_stopped == false && 1) {
      unsigned int shifted_tmp = 0xFF;
      unsigned int rom_string_aligned_tmp_value = *rom_string_aligned_next;
      unsigned char stop = 0;

      while (shifted_tmp) {
         unsigned int current_character_shifted = rom_string_aligned_tmp_value & shifted_tmp;

         if (current_character_shifted == 0) {
            stop = 1;
            break;
         }

         if (!calculate_string_length) {
            unsigned char bytes_to_shift;

            if (shifted_tmp == 0xFF) {
               bytes_to_shift = 0;
            } else if (shifted_tmp == 0xFF00) {
               bytes_to_shift = 8;
            } else if (shifted_tmp == 0xFF0000) {
               bytes_to_shift = 16;
            } else {
               bytes_to_shift = 24;
            }

            char current_character = (char) (current_character_shifted >> bytes_to_shift);
            *(result + calculated_string_length) = current_character;
         }

         calculated_string_length++;
         shifted_tmp <<= 8;
      }

      if (stop) {
         break;
      }
      rom_string_aligned_next++;
   }

   if (calculate_string_length) {
      *string_length = calculated_string_length;
   } else {
      *(result + *string_length) = '\0';
   }
}

/**
 * Do not forget to call free when a string is not required anymore
 */
char *get_string_from_rom(const char *rom_string) {
   unsigned short string_length = 0;

   calculate_rom_string_length_or_fill_malloc(&string_length, NULL, rom_string);

   if (!string_length) {
      return NULL;
   }

   char *result = MALLOC(string_length + 1, __LINE__, 0xFFFFFFFF); // 1 for the last empty character

   calculate_rom_string_length_or_fill_malloc(&string_length, result, rom_string);
   return result;
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

char *generate_reset_reason() {
   struct rst_info *rst_info = system_get_rst_info();
   char reason[2];
   snprintf(reason, 2, "%u", rst_info->reason);
   char cause[3];
   snprintf(cause, 3, "%u", rst_info->exccause);
   char epc_1[11];
   snprintf(epc_1, 11, HEXADECIMAL_ADDRESS_FORMAT, rst_info->epc1);
   char epc_2[11];
   snprintf(epc_2, 11, HEXADECIMAL_ADDRESS_FORMAT, rst_info->epc2);
   char epc_3[11];
   snprintf(epc_3, 11, HEXADECIMAL_ADDRESS_FORMAT, rst_info->epc3);
   char excvaddr[11];
   snprintf(excvaddr, 11, HEXADECIMAL_ADDRESS_FORMAT, rst_info->excvaddr);
   char depc[11];
   snprintf(depc, 11, HEXADECIMAL_ADDRESS_FORMAT, rst_info->depc);
   char rtn_addr[11];
   snprintf(rtn_addr, 11, HEXADECIMAL_ADDRESS_FORMAT, rst_info->rtn_addr);
   char *used_software = system_upgrade_userbin_check() ? "user2.bin" : "user1.bin";

   char *reset_reason_template = get_string_from_rom(RESET_REASON_TEMPLATE);
   char *reset_reason_template_parameters[] = {reason, cause, epc_1, epc_2, epc_3, excvaddr, depc, rtn_addr, used_software, NULL};
   char *reset_reason = set_string_parameters(reset_reason_template, reset_reason_template_parameters);
   FREE(reset_reason_template);
   return reset_reason;
}

static esp_err_t esp_event_handler(void *ctx, system_event_t *event) {
    switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect();
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        ESP_LOGI(TAG, "got ip: %s", ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        break;
    case SYSTEM_EVENT_AP_STACONNECTED:
        ESP_LOGI(TAG, "station: "MACSTR" join, AID=%d", MAC2STR(event->event_info.sta_connected.mac), event->event_info.sta_connected.aid);
        break;
    case SYSTEM_EVENT_AP_STADISCONNECTED:
        ESP_LOGI(TAG, "station: "MACSTR" leave, AID=%d", MAC2STR(event->event_info.sta_disconnected.mac), event->event_info.sta_disconnected.aid);
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
        break;
    default:
        break;
    }
    return ESP_OK;
}

// Do not call this function in user_init because of wifi_station_disconnect and wifi_station_connect
void set_default_wi_fi_settings() {
   wifi_event_group = xEventGroupCreate();

   tcpip_adapter_init();
   ESP_ERROR_CHECK(esp_event_loop_init(esp_event_handler, NULL));

   wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
   ESP_ERROR_CHECK(esp_wifi_init(&cfg));

   wifi_config_t wifi_config;
   memcpy(&wifi_config.sta.ssid, ACCESS_POINT_NAME, 32);
   char *default_access_point_password = get_string_from_rom(ACCESS_POINT_PASSWORD);
   memcpy(&wifi_config.sta.password, default_access_point_password, 64);
   FREE(default_access_point_password);

   ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
   ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
   ESP_ERROR_CHECK(esp_wifi_start());

   ESP_LOGI(TAG, "set_default_wi_fi_settings finished.");

   /*wifi_set_opmode(STATION_MODE);
   wifi_station_set_auto_connect(false);
   wifi_station_set_reconnect_policy(false);
   wifi_station_dhcpc_stop();

   if (wifi_station_disconnect()) {
      #ifdef ALLOW_USE_PRINTF
      printf("Disconnected from AP\n");
      #endif
   }

   struct station_config station_config_settings;

   wifi_station_get_config_default(&station_config_settings);

   char *default_access_point_password = get_string_from_rom(ACCESS_POINT_PASSWORD);

   if (strncmp(ACCESS_POINT_NAME, station_config_settings.ssid, 32) != 0
         || strncmp(default_access_point_password, station_config_settings.password, 64) != 0) {
      struct station_config station_config_settings_to_save;

      memcpy(&station_config_settings_to_save.ssid, ACCESS_POINT_NAME, 32);
      memcpy(&station_config_settings_to_save.password, default_access_point_password, 64);
      wifi_station_set_config(&station_config_settings_to_save);
   }
   FREE(default_access_point_password);

   struct ip_info current_ip_info;
   wifi_get_ip_info(STATION_IF, &current_ip_info);
   char *current_ip = ipaddr_ntoa(&current_ip_info.ip);
   char *own_ip_address = get_string_from_rom(OWN_IP_ADDRESS);

   if (strncmp(current_ip, own_ip_address, 15) != 0) {
      #ifdef ALLOW_USE_PRINTF
      printf("Current IP address: %s\n", current_ip);
      #endif

      char *own_netmask = get_string_from_rom(OWN_NETMASK);
      char *own_getaway_address = get_string_from_rom(OWN_GETAWAY_ADDRESS);
      struct ip_info ip_info_to_set;

      ip_info_to_set.ip.addr = ipaddr_addr(own_ip_address);
      ip_info_to_set.netmask.addr = ipaddr_addr(own_netmask);
      ip_info_to_set.gw.addr = ipaddr_addr(own_getaway_address);
      wifi_set_ip_info(STATION_IF, &ip_info_to_set);
      FREE(own_netmask);
      FREE(own_getaway_address);
   }
   FREE(current_ip);
   FREE(own_ip_address);

   if (wifi_station_connect()) {
      #ifdef ALLOW_USE_PRINTF
      printf("Connected to AP\n");
      #endif
   }*/
}

/**
 * @param pin : GPIO pin GPIO_Pin_x
 */
void pin_output_set(unsigned int pin) {
   GPIO_REG_WRITE(GPIO_OUT_W1TS_ADDRESS, pin);
}

/**
 * @param pin : GPIO pin GPIO_Pin_x
 */
void pin_output_reset(unsigned int pin) {
   GPIO_REG_WRITE(GPIO_OUT_W1TC_ADDRESS, pin);
}

/**
 * @param pin : GPIO pin GPIO_Pin_x
 */
bool read_output_pin_state(unsigned int pin) {
   return (GPIO_REG_READ(GPIO_OUT_ADDRESS) & pin) ? true : false;
}

/**
 * @param pin : GPIO pin GPIO_Pin_x
 */
bool read_input_pin_state(unsigned int pin) {
   return (GPIO_REG_READ(GPIO_IN_ADDRESS) & pin) ? true : false;
}

LOCAL unsigned int replace_zeroes(unsigned int to_be_replaced_value) {
   unsigned char bits;
   unsigned int to_be_replaced_value_tmp = to_be_replaced_value;

   for (bits = 0; bits <= 32 && to_be_replaced_value_tmp > 0; bits++) {
      to_be_replaced_value_tmp >>= 1;
   }

   #ifdef ALLOW_USE_PRINTF
   printf("bits: %u\n", bits);
   #endif

   unsigned int returning_value = 0;
   while (bits > 0) {
      returning_value <<= 1;
      returning_value |= 1;
      bits--;
   }
   return returning_value;
}

unsigned int generate_rand(unsigned int min_value, unsigned int max_value) {
   unsigned int generated_random = rand();
   unsigned int max_value_with_replaced_zeroes = replace_zeroes(max_value);
   unsigned int final_random = generated_random & max_value_with_replaced_zeroes;

   if (final_random > max_value) {
      return max_value;
   } else if (final_random < min_value) {
      return min_value;
   } else {
      return final_random;
   }
}
