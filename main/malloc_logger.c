#include "malloc_logger.h"

#ifdef MALLOC_LOGGER

static struct malloc_logger_element malloc_logger_list[MALLOC_LOGGER_LIST_SIZE];

static const char TO_BE_ALLOCATED_ELEMENT_MSG[] = "\n%u length heap element is to be allocated. Code line: %s(%u), time: %u\n";
static const char ALLOCATED_ELEMENT_MSG[] = "\n%u length element has been allocated in heap. Address: 0x%X, code line: %s(%u), time: %u, index in logger list: %u\n";
static const char TO_BE_REMOVED_ELEMENT_MSG[] = "\nElement is to be removed from heap. Address: 0x%X\n";
static const char FOUND_ELEMENT_MSG[] = "\n0x%X element has been found in logger list\n";
static const char REMOVED_ELEMENT_MSG[] = "\n0x%X element has been removed from heap. Index in logger list: %u\n";
static const char ALLOCATED_ELEMENT_INFO_MSG[] = "\nElement's variable line: %s(%u), allocated time: %u\n";

char *malloc_logger(unsigned int element_size, unsigned int allocated_time, const char *file_name, unsigned int variable_line, bool is_zalloc) {
   printf(TO_BE_ALLOCATED_ELEMENT_MSG, element_size, file_name, variable_line, allocated_time);

   char *allocated_element;

   if (is_zalloc) {
      allocated_element = os_zalloc(element_size);
   } else {
      allocated_element = os_malloc(element_size);
   }

   unsigned char i;
   for (i = 0; i < MALLOC_LOGGER_LIST_SIZE; i++) {
      if (malloc_logger_list[i].allocated_element_address == NULL) {
         malloc_logger_list[i].allocated_element_address = allocated_element;
         malloc_logger_list[i].variable_line = variable_line;
         malloc_logger_list[i].file_name = file_name;
         malloc_logger_list[i].allocated_time = allocated_time;

         printf(ALLOCATED_ELEMENT_MSG, element_size, (unsigned int) allocated_element, file_name, variable_line, allocated_time, i);
         break;
      }
   }

   assert(i < MALLOC_LOGGER_LIST_SIZE);
   return allocated_element;
}

void free_logger(void *allocated_address_element_to_free, unsigned int variable_line) {
   printf(TO_BE_REMOVED_ELEMENT_MSG, (unsigned int) allocated_address_element_to_free);

   unsigned char i;

   for (i = 0; i < MALLOC_LOGGER_LIST_SIZE; i++) {
      if (malloc_logger_list[i].allocated_element_address == allocated_address_element_to_free) {

         printf(FOUND_ELEMENT_MSG, (unsigned int) allocated_address_element_to_free);

         os_free(allocated_address_element_to_free);
         malloc_logger_list[i].allocated_element_address = NULL;
         malloc_logger_list[i].variable_line = 0;
         malloc_logger_list[i].file_name = NULL;
         malloc_logger_list[i].allocated_time = 0;

         printf(REMOVED_ELEMENT_MSG, (unsigned int) allocated_address_element_to_free, i);
         break;
      }
   }
}

unsigned char get_malloc_logger_list_elements_amount() {
   unsigned char i = 0;
   unsigned char amount = 0;

   while (i < MALLOC_LOGGER_LIST_SIZE) {
      if (malloc_logger_list[i].allocated_element_address != NULL) {
         amount++;
      }
      i++;
   }
   return amount;
}

struct malloc_logger_element get_last_element_in_logger_list() {
   unsigned char i;

   for(i = MALLOC_LOGGER_LIST_SIZE - 1; i != 0xFF; i--) {
      if (malloc_logger_list[i].allocated_element_address != NULL) {
         return malloc_logger_list[i];
         break;
      }
   }
   return malloc_logger_list[MALLOC_LOGGER_LIST_SIZE - 1];
}

void print_not_empty_elements_lines() {
   unsigned int i;

   for (i = 0; i < MALLOC_LOGGER_LIST_SIZE; i++) {
      if (malloc_logger_list[i].allocated_element_address != NULL) {
         printf(ALLOCATED_ELEMENT_INFO_MSG, malloc_logger_list[i].file_name, malloc_logger_list[i].variable_line, malloc_logger_list[i].allocated_time);
      }
   }
}

#endif
