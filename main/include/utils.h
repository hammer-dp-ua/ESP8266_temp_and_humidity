#ifndef GENERAL_UTILS
#define GENERAL_UTILS

#include "device_settings.h"
#include "global_definitions.h"
#include "malloc_logger.h"
#include "esp_err.h"
#include "esp_event_loop.h"

#define HEXADECIMAL_ADDRESS_FORMAT "%08x"

static char RESET_REASON_TEMPLATE[] ICACHE_RODATA_ATTR = "<1>\\n"
      " Fatal exception (<2>):\\n"
      " epc1=0x<3>, epc2=0x<4>, epc3=0x<5>, excvaddr=0x<6>, depc=0x<7>, rtn_addr=0x<8>\\n"
      " used software: <9>";

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
