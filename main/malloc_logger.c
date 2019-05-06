#include "malloc_logger.h"

#ifdef MALLOC_LOGGER

static struct malloc_logger_element malloc_logger_list[MALLOC_LOGGER_LIST_SIZE];

static void check_is_full(unsigned char current_amount) {
   #ifdef ALLOW_USE_PRINTF
   if (current_amount >= MALLOC_LOGGER_LIST_SIZE) {
      ESP_LOGE(TAG, "\n Malloc_logger_list is full!\n");
   }
   #endif
}

void *zalloc_logger(unsigned int element_size, unsigned int variable_line, unsigned int allocated_time) {
   ESP_LOGI(TAG, "%u length heap element is to be allocated. Code line: %u, time: %u", element_size, variable_line, allocated_time);

   void *allocated_address = os_zalloc(element_size);
   unsigned char i;

   for (i = 0; i < MALLOC_LOGGER_LIST_SIZE; i++) {
      if (malloc_logger_list[i].allocated_element_address == NULL) {
         malloc_logger_list[i].allocated_element_address = allocated_address;
         malloc_logger_list[i].variable_line = variable_line;
         malloc_logger_list[i].allocated_time = allocated_time;

         ESP_LOGI(TAG, "%u length heap has been allocated. Address: %u, code line: %u, time: %u, index in logger list: %u",
                        (unsigned int) allocated_address, element_size, variable_line, allocated_time, i);
         break;
      }
   }

   check_is_full(i);
   return allocated_address;
}

char *malloc_logger(unsigned int element_size, unsigned int variable_line, unsigned int allocated_time) {
   ESP_LOGI(TAG, "%u length heap element is to be allocated. Code line: %u, time: %u", element_size, variable_line, allocated_time);

   char *allocated_element = os_malloc(element_size);
   unsigned char i;

   for (i = 0; i < MALLOC_LOGGER_LIST_SIZE; i++) {
      if (malloc_logger_list[i].allocated_element_address == NULL) {
         malloc_logger_list[i].allocated_element_address = allocated_element;
         malloc_logger_list[i].variable_line = variable_line;
         malloc_logger_list[i].allocated_time = allocated_time;

         ESP_LOGI(TAG, "%u length heap has been allocated. Address: %u, code line: %u, time: %u, index in logger list: %u",
               (unsigned int) allocated_element, element_size, variable_line, allocated_time, i);
         break;
      }
   }

   check_is_full(i);
   return allocated_element;
}

void free_logger(void *allocated_address_element_to_free) {
   ESP_LOGI(TAG, "Element is to be removed from heap. Address: %u", (unsigned int) allocated_address_element_to_free);

   unsigned char i;

   for (i = 0; i < MALLOC_LOGGER_LIST_SIZE; i++) {
      if (malloc_logger_list[i].allocated_element_address == allocated_address_element_to_free) {
         ESP_LOGI(TAG, "%u element has been found in logger list", (unsigned int) allocated_address_element_to_free);

         os_free(allocated_address_element_to_free);
         malloc_logger_list[i].allocated_element_address = NULL;
         malloc_logger_list[i].variable_line = 0;
         malloc_logger_list[i].allocated_time = 0;

         ESP_LOGI(TAG, "%u element has been removed from heap. Index in logger list: %u",
               (unsigned int) allocated_address_element_to_free, i);
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
         ESP_LOGI(TAG, " element's variable line: %u, allocated time: %u\n",
               malloc_logger_list[i].variable_line, malloc_logger_list[i].allocated_time);
      }
   }
}

#endif
