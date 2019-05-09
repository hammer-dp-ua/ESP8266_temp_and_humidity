#include <stdio.h>
#include "global_definitions.h"
#include "device_settings.h"
#include "esp_heap_caps.h"
#include "esp_libc.h"
#include "stdbool.h"

#ifdef USE_MALLOC_LOGGER
   #define FREE(allocated_address_element_to_free, line_no) free_logger(allocated_address_element_to_free, line_no)
   #define MALLOC(element_length, line_no, allocated_time)  malloc_logger(element_length, line_no, allocated_time, false)
   #define ZALLOC(element_length, line_no, allocated_time)  malloc_logger(element_length, line_no, allocated_time, true)
#else
   #define FREE(allocated_address_element_to_free, line_no) os_free(allocated_address_element_to_free)
   #define MALLOC(element_length, line_no, allocated_time)  os_malloc(element_length)
   #define ZALLOC(element_length, line_no, allocated_time)  os_zalloc(element_length)
#endif

#ifndef MALLOC_LOGGER
#define MALLOC_LOGGER

#define MALLOC_LOGGER_LIST_SIZE 50

struct malloc_logger_element {
   unsigned int variable_line;
   void * allocated_element_address;
   unsigned int allocated_time;
};

char *malloc_logger(unsigned int string_size, unsigned int variable_line, unsigned int allocated_time, bool is_zalloc);
unsigned char get_malloc_logger_list_elements_amount();
void free_logger(void *allocated_address_element_to_free, unsigned int line_no);
struct malloc_logger_element get_last_element_in_logger_list();
void print_not_empty_elements_lines();

#endif
