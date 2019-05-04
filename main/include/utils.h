#ifndef GENERAL_UTILS
#define GENERAL_UTILS

#include "device_settings.h"
#include "global_definitions.h"
#include "malloc_logger.h"
#include "esp_err.h"
#include "esp_event_loop.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "FreeRTOS.h"
#include "event_groups.h"
#include "string.h"
#include "lwip/ip4_addr.h"

#define HEXADECIMAL_ADDRESS_FORMAT "%08x"

void *set_string_parameters(char string[], char *parameters[]);
char *generate_post_request(char *request);
bool compare_strings(char *string1, char *string2);
void pin_output_set(unsigned int pin);
void pin_output_reset(unsigned int pin);
bool read_output_pin_state(unsigned int pin);
bool read_input_pin_state(unsigned int pin);
char *generate_reset_reason();
void wifi_init_sta(void (*on_connected)(), void (*on_disconnected)(), void (*on_connection)());
bool is_connected_to_wifi();

#endif
