#include <stdio.h>
#include "global_definitions.h"
#include "device_settings.h"
#include "esp_heap_caps.h"
#include "esp_libc.h"
#include "stdbool.h"

#ifdef USE_MALLOC_LOGGER
   #define FREE(allocated_address_element_to_free) free_logger(allocated_address_element_to_free, __LINE__)
   #define MALLOC(element_length, allocated_time)  malloc_logger(element_length, allocated_time, __ESP_FILE__, __LINE__, false)
   #define ZALLOC(element_length, allocated_time)  malloc_logger(element_length, allocated_time, __ESP_FILE__, __LINE__, true)
#else
   #define FREE(allocated_address_element_to_free) os_free(allocated_address_element_to_free)
   #define MALLOC(element_length, allocated_time)  os_malloc(element_length)
   #define ZALLOC(element_length, allocated_time)  os_zalloc(element_length)
#endif

#ifndef MALLOC_LOGGER
#define MALLOC_LOGGER

#define MALLOC_LOGGER_LIST_SIZE 20

struct malloc_logger_element {
   unsigned int variable_line;
   const char *file_name;
   void * allocated_element_address;
   unsigned int allocated_time;
};

char *malloc_logger(unsigned int element_size, unsigned int allocated_time, const char *file_name, unsigned int variable_line, bool is_zalloc);
unsigned char get_malloc_logger_list_elements_amount();
void free_logger(void *allocated_address_element_to_free, unsigned int line_no);
struct malloc_logger_element get_last_element_in_logger_list();
void print_not_empty_elements_lines();

#endif
