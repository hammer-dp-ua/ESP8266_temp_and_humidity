#ifndef GENERAL_UTILS
#define GENERAL_UTILS

#include "device_settings.h"
#include "global_definitions.h"
#include "malloc_logger.h"
#include "esp_err.h"
#include "esp_event_loop.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "FreeRTOS.h"
#include "event_groups.h"
#include "string.h"
#include "lwip/ip4_addr.h"

#define HEXADECIMAL_ADDRESS_FORMAT "%08x"

void *set_string_parameters(const char string[], const char *parameters[]);
char *generate_post_request(char *request);
bool compare_strings(char *string1, char *string2);
char *put_flash_string_into_heap(const char *flash_string, unsigned int allocated_time);
char *generate_reset_reason();
void wifi_init_sta(void (*on_connected)(), void (*on_disconnected)(), void (*on_connection)());
bool is_connected_to_wifi();
#endif
