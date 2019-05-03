#include <stdio.h>
#include "esp_system.h"

/**
 * Pins 4 and 5 on some ESP8266-07 are exchanged on silk screen!!!
 *
 * SPI SPI_CPOL & SPI_CPHA:
 *    SPI_CPOL - (0) Clock is low when inactive
 *               (1) Clock is high when inactive
 *    SPI_CPHA - (0) Data is valid on clock leading edge
 *               (1) Data is valid on clock trailing edge
 */

//#include "esp_common.h"

// driver libs
#include "driver/uart.h"
#include "driver/gpio.h"

//#include "esp_sta.h"
#include "esp_wifi.h"
#include "global_definitions.h"
#include "malloc_logger.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"
#include "device_settings.h"
//#include "espconn.h"
#include "utils.h"
#include "lwip/sys.h"
#include "lwip/inet.h"
#include "sys/socket.h"
#include "event_groups.h"
#include "user_main.h"

unsigned int milliseconds_counter_g;
int signal_strength_g;
unsigned short errors_counter_g;
unsigned short repetitive_request_errors_counter_g = 0;
unsigned char pending_connection_errors_counter_g;
unsigned int repetitive_ap_connecting_errors_counter_g;
int connection_error_code_g;

static os_timer_t millisecons_time_serv_g;
static os_timer_t status_sender_timer_g;
static os_timer_t errors_checker_timer_g;
static os_timer_t ap_autoconnect_timer_g;

struct _esp_tcp user_tcp;

unsigned char responses_index;
char *responses[10];
static EventGroupHandle_t general_event_group_g;

SemaphoreHandle_t requests_binary_semaphore_g;

static QueueHandle_t uart0_queue;

/******************************************************************************
 * FunctionName : user_rf_cal_sector_set
 * Description  : SDK just reversed 4 sectors, used for rf init data and paramters.
 *                We add this function to force users to set rf cal sector, since
 *                we don't know which sector is free in user's application.
 *                sector map for last several sectors : ABCCC
 *                A : rf cal
 *                B : rf init data
 *                C : sdk parameters
 * Parameters   : none
 * Returns      : rf cal sector
 *******************************************************************************/
unsigned int user_rf_cal_sector_set(void) {
   flash_size_map size_map = system_get_flash_size_map();
   unsigned int rf_cal_sec = 0;

   switch (size_map) {
      case FLASH_SIZE_4M_MAP_256_256:
         rf_cal_sec = 128 - 5;
         break;

      case FLASH_SIZE_8M_MAP_512_512:
         rf_cal_sec = 256 - 5;
         break;

      case FLASH_SIZE_16M_MAP_512_512:
      case FLASH_SIZE_16M_MAP_1024_1024:
         rf_cal_sec = 512 - 5;
         break;

      case FLASH_SIZE_32M_MAP_512_512:
      case FLASH_SIZE_32M_MAP_1024_1024:
         rf_cal_sec = 1024 - 5;
         break;

      default:
         rf_cal_sec = 0;
         break;
   }
   return rf_cal_sec;
}

static void milliseconds_counter() {
   milliseconds_counter_g++;
}

void start_100millisecons_counter() {
   os_timer_disarm(&millisecons_time_serv_g);
   os_timer_setfn(&millisecons_time_serv_g, (os_timer_func_t *) milliseconds_counter, NULL);
   os_timer_arm(&millisecons_time_serv_g, 1000 / MILLISECONDS_COUNTER_DIVIDER, 1); // 100 ms
}

void stop_milliseconds_counter() {
   os_timer_disarm(&millisecons_time_serv_g);
}

// Callback function when AP scanning is completed
void get_ap_signal_strength(void *arg, STATUS status) {
   if (status == OK && arg != NULL) {
      struct bss_info *got_bss_info = (struct bss_info *) arg;

      signal_strength_g = got_bss_info->rssi;
      //got_bss_info = got_bss_info->next.stqe_next;
   }
}

void scan_access_point_task(void *pvParameters) {
   long rescan_when_connected_task_delay = 10 * 60 * 1000 / portTICK_RATE_MS; // 10 mins
   long rescan_when_not_connected_task_delay = 10 * 1000 / portTICK_RATE_MS; // 10 secs
   wifi_scan_config_t scan_config;
   wifi_ap_record_t scanned_access_points[1];

   scan_config.ssid = ACCESS_POINT_NAME;
   scan_config.scan_type = WIFI_SCAN_TYPE_PASSIVE;

   for (;;) {
      if (esp_wifi_scan_start(&scan_config, true) == ESP_OK && esp_wifi_scan_get_ap_records(1, scanned_access_points) == ESP_OK) {
         signal_strength_g = scanned_access_points[0].rssi;

         ESP_LOGI(TAG, "Signal strength of AP: %d", signal_strength_g);

         vTaskDelay(rescan_when_connected_task_delay);
      } else {
         vTaskDelay(rescan_when_not_connected_task_delay);
      }
   }
}

void successfull_connected_tcp_handler_callback(void *arg) {
   struct espconn *connection = arg;
   struct connection_user_data *user_data = connection->reserve;
   char *request = user_data->request;
   unsigned short request_length = strnlen(request, 0xFFFF);

   espconn_set_opt(connection, ESPCONN_REUSEADDR);
   // Keep-Alive timeout doesn't work yet
   //espconn_set_opt(connection, ESPCONN_KEEPALIVE); // ESPCONN_REUSEADDR |
   //uint32 espconn_keepidle_value = 5; // seconds
   //unsigned char keepalive_error_code = espconn_set_keepalive(connection, ESPCONN_KEEPIDLE, &espconn_keepidle_value);
   //uint32 espconn_keepintvl_value = 2; // seconds
   // If there is no response, retry ESPCONN_KEEPCNT times every ESPCONN_KEEPINTVL
   //keepalive_error_code |= espconn_set_keepalive(connection, ESPCONN_KEEPINTVL, &espconn_keepintvl_value);
   //uint32 espconn_keepcnt_value = 2; // count
   //keepalive_error_code |= espconn_set_keepalive(connection, ESPCONN_KEEPCNT, &espconn_keepcnt_value);

   int sent_status = espconn_send(connection, request, request_length);
   FREE(request);
   user_data->request = NULL;

   if (sent_status != 0) {
      void (*execute_on_error)(struct espconn *connection) = user_data->execute_on_error;

      if (execute_on_error != NULL) {
         execute_on_error(connection);
      }
   }
}

void successfull_disconnected_tcp_handler_callback(void *arg) {
   struct espconn *connection = arg;
   struct connection_user_data *user_data = connection->reserve;
   bool response_received = user_data->response_received;

   #ifdef ALLOW_USE_PRINTF
   printf("Disconnected callback beginning. Response %s received. Time: %u\n", response_received ? "has been" : "has not been", milliseconds_counter_g);
   #endif

   void (*execute_on_disconnect)(struct espconn *connection) = user_data->execute_on_disconnect;

   if (execute_on_disconnect) {
      execute_on_disconnect(connection);
   }
}

void tcp_connection_error_handler_callback(void *arg, sint8 err) {
   #ifdef ALLOW_USE_PRINTF
   printf("Connection error callback. Error code: %d. Time: %u\n", err, milliseconds_counter_g);
   #endif

   connection_error_code_g = err;

   struct espconn *connection = arg;
   struct connection_user_data *user_data = NULL;
   void (*execute_on_error)(struct espconn *connection) = NULL;

   if (connection != NULL) {
      user_data = connection->reserve;
   }
   if (user_data != NULL) {
      execute_on_error = user_data->execute_on_error;
   }
   if (execute_on_error != NULL) {
      execute_on_error(connection);
   }
}

void tcp_response_received_handler_callback(void *arg, char *pdata, unsigned short len) {
   struct espconn *connection = arg;
   struct connection_user_data *user_data = connection->reserve;
   bool response_received = user_data->response_received;

   if (!response_received) {
      char *server_sent = get_string_from_rom(RESPONSE_SERVER_SENT_OK);

      if (strstr(pdata, server_sent)) {
         user_data->response_received = true;

         char *response = MALLOC(len, __LINE__, milliseconds_counter_g);

         memcpy(response, pdata, len);
         user_data->response = response;

         #ifdef ALLOW_USE_PRINTF
         printf("Response received. Time: %u\n", milliseconds_counter_g);
         #endif
      }
      FREE(server_sent);
   }
}

void tcp_request_successfully_sent_handler_callback() {
   //printf("Request sent callback\n");
}

void tcp_request_successfully_written_into_buffer_handler_callback() {
   //printf("Request written into buffer callback\n");
}

void status_request_on_disconnect_callback(struct espconn *connection) {
   #ifdef ALLOW_USE_PRINTF
   printf("status_request_on_disconnect_callback, Time: %u\n", milliseconds_counter_g);
   #endif

   struct connection_user_data *user_data = connection->reserve;

   if (!user_data->response_received && user_data->execute_on_error != NULL) {
      user_data->execute_on_error(connection);
      return;
   }

   pin_output_set(SERVER_AVAILABILITY_STATUS_LED_PIN);
   if (!read_flag(general_flags, FIRST_STATUS_INFO_SENT_FLAG)) {
      set_flag(&general_flags, FIRST_STATUS_INFO_SENT_FLAG);
   }
   reset_flag(&general_flags, REQUEST_ERROR_OCCURRED_FLAG);

   connection_error_code_g = 0;
   repetitive_request_errors_counter_g = 0;

   check_for_update_firmware(user_data->response);
   request_finish_action(connection);

   if (read_flag(general_flags, UPDATE_FIRMWARE_FLAG)) {
      upgrade_firmware();
      return;
   }

   schedule_sending_status_info(STATUS_REQUESTS_SEND_INTERVAL_MS);
}

void status_request_on_error_callback(struct espconn *connection) {
   #ifdef ALLOW_USE_PRINTF
   printf("status_request_on_error_callback. Time: %u\n", milliseconds_counter_g);
   #endif

   errors_counter_g++;
   repetitive_request_errors_counter_g++;

   pin_output_reset(SERVER_AVAILABILITY_STATUS_LED_PIN);
   set_flag(&general_flags, REQUEST_ERROR_OCCURRED_FLAG);
   request_finish_action(connection);
   schedule_sending_status_info(10000);
}

void general_request_on_disconnect_callback(struct espconn *connection) {
   #ifdef ALLOW_USE_PRINTF
   printf("general_request_on_disconnect_callback. Time: %u\n", milliseconds_counter_g);
   #endif

   struct connection_user_data *user_data = connection->reserve;
   xTaskHandle parent_task = user_data->parent_task;

   if (!user_data->response_received && user_data->execute_on_error) {
      user_data->execute_on_error(connection);
      return;
   }
   if (parent_task) {
      #ifdef ALLOW_USE_PRINTF
      printf("parent task of general request is to be deleted...\n");
      #endif

      vTaskDelete(parent_task);
   }

   connection_error_code_g = 0;
   repetitive_request_errors_counter_g = 0;

   check_for_update_firmware(user_data->response);
   pin_output_set(SERVER_AVAILABILITY_STATUS_LED_PIN);

   if (buzzer_semaphore_g != NULL) {
      signed portBASE_TYPE xHigherPriorityTaskWoken = pdFALSE;
      xSemaphoreGiveFromISR(buzzer_semaphore_g, &xHigherPriorityTaskWoken);
   }

   request_finish_action(connection);
}

void general_request_on_error_callback(struct espconn *connection) {
   #ifdef ALLOW_USE_PRINTF
   printf("general_request_on_error_callback. Time: %u\n", milliseconds_counter_g);
   #endif

   errors_counter_g++;
   repetitive_request_errors_counter_g++;

   pin_output_reset(SERVER_AVAILABILITY_STATUS_LED_PIN);
   set_flag(&general_flags, REQUEST_ERROR_OCCURRED_FLAG);
   request_finish_action(connection);
}

void request_finish_action(struct espconn *connection) {
   struct connection_user_data *user_data = connection->reserve;

   if (user_data->request != NULL) {
      FREE(user_data->request);
   }
   if (user_data->response != NULL) {
      FREE(user_data->response);
   }

   if (user_data->timeout_request_supervisor_task != NULL) {
      #ifdef ALLOW_USE_PRINTF
      printf("\n timeout_request_supervisor_task still exists\n");
      #endif

      vTaskDelete(user_data->timeout_request_supervisor_task);
   }
   FREE(user_data);
   connection->reserve = NULL;

   xTaskCreate(disconnect_connection_task, "disconnect_connection_task", 180, connection, 1, NULL);
}

void disconnect_connection_task(void *pvParameters) {
   struct espconn *connection = pvParameters;

   espconn_regist_disconcb(connection, NULL);
   espconn_disconnect(connection); // Don't call this API in any espconn callback
   FREE(connection);

   #if defined(ALLOW_USE_PRINTF) && defined(USE_MALLOC_LOGGER)
   printf("\n Elements amount in malloc logger list: %u\n", get_malloc_logger_list_elements_amount());
   print_not_empty_elements_lines();
   printf("\n Free Heap size: %u\n", xPortGetFreeHeapSize());
   #endif

   portBASE_TYPE xHigherPriorityTaskWoken = pdFALSE;
   xSemaphoreGiveFromISR(requests_binary_semaphore_g, &xHigherPriorityTaskWoken);
   vTaskDelete(NULL);
}

bool compare_pins_names(char *activated_pin, char *rom_pin_name) {
   char *comparable_pin = get_string_from_rom(rom_pin_name);
   bool names_matched = compare_strings(activated_pin, comparable_pin);

   FREE(comparable_pin);
   return names_matched;
}

void fill_motion_sensor_request_data(struct request_data *request_data, GeneralRequestType request_type, MotionSensorUnit msu, char *motion_sensor_pin) {
   struct motion_sensor *ms = (struct motion_sensor *) ZALLOC(sizeof(struct motion_sensor), __LINE__, milliseconds_counter_g);

   ms->unit = msu;
   ms->alarm_source = motion_sensor_pin;

   request_data->ms = ms;
   request_data->request_type = request_type;
}

void input_pins_analyzer_task(void *pvParameters) {
   char *activated_pin_with_prefix = pvParameters;
   char *prefix = "pin:";
   unsigned char prefix_length = strnlen(prefix, 0xFF);
   char *found_pin_prefix = strstr(activated_pin_with_prefix, prefix);

   if (found_pin_prefix == NULL) {
      #ifdef ALLOW_USE_PRINTF
      printf("\n pin prefix is not found: %s\n", activated_pin_with_prefix);
      #endif

      FREE(activated_pin_with_prefix);
      vTaskDelete(NULL);
   }

   unsigned char pin_name_length = 0;
   while (*(activated_pin_with_prefix + prefix_length + pin_name_length) != '\0') {
      pin_name_length++;
   }

   char *activated_pin = MALLOC(pin_name_length + 1, __LINE__, milliseconds_counter_g);

   unsigned char i = 0;

   for (i = 0; i < pin_name_length; i++) {
      *(activated_pin + i) = *(activated_pin_with_prefix + prefix_length + i);
   }
   *(activated_pin + pin_name_length) = '\0';
   FREE(activated_pin_with_prefix);

   #ifdef ALLOW_USE_PRINTF
   printf("\n pin without prefix: %s\n", activated_pin);
   #endif

   struct request_data *request_data_param = (struct request_data *) ZALLOC(sizeof(struct request_data), __LINE__, milliseconds_counter_g);

   if (compare_pins_names(activated_pin, MOTION_SENSOR_1_PIN)) {
      fill_motion_sensor_request_data(request_data_param, ALARM, MOTION_SENSOR_1, activated_pin);
      send_general_request(request_data_param, 2);
   } else if (compare_pins_names(activated_pin, MOTION_SENSOR_2_PIN)) {
      fill_motion_sensor_request_data(request_data_param, ALARM, MOTION_SENSOR_2, activated_pin);
      send_general_request(request_data_param, 2);
   } else if (compare_pins_names(activated_pin, MOTION_SENSOR_3_PIN)) {
      fill_motion_sensor_request_data(request_data_param, ALARM, MOTION_SENSOR_3, activated_pin);
      send_general_request(request_data_param, 2);
   /*} else if (compare_pins_names(activated_pin, PIR_LED_1_PIN) || compare_pins_names(activated_pin, MW_LED_1_PIN)) {
      fill_motion_sensor_request_data(request_data_param, FALSE_ALARM, MOTION_SENSOR_1, activated_pin);
      send_general_request(request_data_param, 1);*/
   } else if (compare_pins_names(activated_pin, PIR_LED_3_PIN) || compare_pins_names(activated_pin, MW_LED_3_PIN)) {
      fill_motion_sensor_request_data(request_data_param, FALSE_ALARM, MOTION_SENSOR_3, activated_pin);
      send_general_request(request_data_param, 1);
   } else if (compare_pins_names(activated_pin, IMMOBILIZER_LED_PIN)) {
      FREE(activated_pin);
      request_data_param->request_type = IMMOBILIZER_ACTIVATION;
      send_general_request(request_data_param, 1);
   } else {
      FREE(activated_pin);
      FREE(request_data_param);
   }

   vTaskDelete(NULL);
}

void timeout_request_supervisor_task(void *pvParameters) {
   struct espconn *connection = pvParameters;
   struct connection_user_data *user_data = connection->reserve;

   vTaskDelay(user_data->request_max_duration_time);

   #ifdef ALLOW_USE_PRINTF
   printf("\n Request timeout. Time: %u\n", milliseconds_counter_g);
   #endif

   // To not delete this task in other functions
   user_data->timeout_request_supervisor_task = NULL;

   void (*execute_on_error)(struct espconn *connection) = user_data->execute_on_error;
   if (execute_on_error != NULL) {
      execute_on_error(connection);
   }

   vTaskDelete(NULL);
}

void blink_leds_while_updating_task(void *pvParameters) {
   for (;;) {
      if (read_output_pin_state(AP_CONNECTION_STATUS_LED_PIN)) {
         pin_output_reset(AP_CONNECTION_STATUS_LED_PIN);
         pin_output_set(SERVER_AVAILABILITY_STATUS_LED_PIN);
      } else {
         pin_output_set(AP_CONNECTION_STATUS_LED_PIN);
         pin_output_reset(SERVER_AVAILABILITY_STATUS_LED_PIN);
      }

      vTaskDelay(100 / portTICK_RATE_MS);
   }
}

void check_for_update_firmware(char *response) {
   char *update_firmware_json_element = get_string_from_rom(UPDATE_FIRMWARE);

   if (strstr(response, update_firmware_json_element) != NULL) {
      set_flag(&general_flags, UPDATE_FIRMWARE_FLAG);
   }
   FREE(update_firmware_json_element);
}

void upgrade_firmware() {
   #ifdef ALLOW_USE_PRINTF
   printf("\nUpdating firmware... Time: %u\n", milliseconds_counter_g);
   #endif

   turn_motion_sensors_off();
   ETS_UART_INTR_DISABLE(); // To not receive data from UART RX

   xTaskCreate(blink_leds_while_updating_task, "blink_leds_while_updating_task", 256, NULL, 1, NULL);

   struct upgrade_server_info *upgrade_server =
         (struct upgrade_server_info *) ZALLOC(sizeof(struct upgrade_server_info), __LINE__, milliseconds_counter_g);
   struct sockaddr_in *sockaddrin = (struct sockaddr_in *) ZALLOC(sizeof(struct sockaddr_in), __LINE__, milliseconds_counter_g);

   upgrade_server->sockaddrin = *sockaddrin;
   upgrade_server->sockaddrin.sin_family = AF_INET;
   struct in_addr sin_addr;
   char *server_ip = get_string_from_rom(SERVER_IP_ADDRESS);
   sin_addr.s_addr = inet_addr(server_ip);
   upgrade_server->sockaddrin.sin_addr = sin_addr;
   upgrade_server->sockaddrin.sin_port = htons(SERVER_PORT);
   upgrade_server->sockaddrin.sin_len = sizeof(upgrade_server->sockaddrin);
   upgrade_server->check_cb = ota_finished_callback;
   upgrade_server->check_times = 10;

   char *url_pattern = get_string_from_rom(FIRMWARE_UPDATE_GET_REQUEST);
   unsigned char user_bin = system_upgrade_userbin_check();
   char *file_to_download = user_bin == UPGRADE_FW_BIN1 ? "user2.bin" : "user1.bin";
   char *url_parameters[] = {file_to_download, server_ip, NULL};
   char *url = set_string_parameters(url_pattern, url_parameters);

   FREE(url_pattern);
   FREE(server_ip);
   upgrade_server->url = url;
   system_upgrade_start(upgrade_server);
}

void ota_finished_callback(void *arg) {
   struct upgrade_server_info *update = arg;

   if (update->upgrade_flag == true) {
      #ifdef ALLOW_USE_PRINTF
      printf("[OTA] success; rebooting! Time: %u\n", milliseconds_counter_g);
      #endif

      SYSTEM_RESTART_REASON_TYPE reason = SOFTWARE_UPGRADE;
      system_rtc_mem_write(SYSTEM_RESTART_REASON_TYPE_RTC_ADDRESS, &reason, 4);

      system_upgrade_flag_set(UPGRADE_FLAG_FINISH);
      system_upgrade_reboot();
   } else {
      #ifdef ALLOW_USE_PRINTF
      printf("[OTA] failed! Time: %u\n", milliseconds_counter_g);
      #endif

      system_restart();
   }

   FREE(&update->sockaddrin);
   FREE(update->url);
   FREE(update);
}

void establish_connection(struct espconn *connection) {
   if (connection == NULL) {
      #ifdef ALLOW_USE_PRINTF
      printf("\n Create connection first\n");
      #endif

      return;
   }
   xTaskCreate(blink_on_send_task, "blink_on_send_task", 180, (void *) SERVER_AVAILABILITY_STATUS_LED_PIN_TYPE, 1, NULL);

   int connection_status = espconn_connect(connection);

   #ifdef ALLOW_USE_PRINTF
   printf("\n Connection status: ");
   #endif

   switch (connection_status) {
      case ESPCONN_OK:
      {
         struct connection_user_data *user_data = connection->reserve;

         if (user_data != NULL) {
            xTaskHandle created_supervisor_task;
            xTaskCreate(timeout_request_supervisor_task, "timeout_request_supervisor_task", 256, connection, 1, &created_supervisor_task);
            user_data->timeout_request_supervisor_task = created_supervisor_task;
         }

         #ifdef ALLOW_USE_PRINTF
         printf("Connected");
         #endif

         break;
      }
      case ESPCONN_RTE:
         #ifdef ALLOW_USE_PRINTF
         printf("Routing problem");
         #endif

         break;
      case ESPCONN_MEM:
         #ifdef ALLOW_USE_PRINTF
         printf("Out of memory");
         #endif

         break;
      case ESPCONN_ISCONN:
         #ifdef ALLOW_USE_PRINTF
         printf("Already connected");
         #endif

         break;
      case ESPCONN_ARG:
         #ifdef ALLOW_USE_PRINTF
         printf("Illegal argument");
         #endif

         break;
   }
   #ifdef ALLOW_USE_PRINTF
   printf(". Time: %u\n", milliseconds_counter_g);
   #endif

   if (connection_status != ESPCONN_OK) {
      struct connection_user_data *user_data = connection->reserve;

      connection_error_code_g = connection_status;

      if (user_data != NULL && user_data->execute_on_error != NULL) {
         user_data->execute_on_error(connection);
      }
   }
}

void send_status_info_task(void *pvParameters) {
   int socket_result = socket(AF_INET, SOCK_STREAM, IPPROTO_IP); // SOCK_STREAM - TCP

   if(socket_result < 0) {
      ESP_LOGE(TAG, "Failed to allocate socket");
   }
   ESP_LOGI(TAG, "Socket has been allocated");

   struct sockaddr_in destination_address;
   destination_address.sin_addr.s_addr = inet_addr(SERVER_IP_ADDRESS);
   destination_address.sin_family = AF_INET;
   destination_address.sin_port = htons(SERVER_PORT);

   int connection_result = connect(socket_result, (struct sockaddr *) &destination_address, sizeof(destination_address));

   if(connection_result != 0) {
      ESP_LOGE(TAG, "Socket connection failed. Error: %d", connection_result);
      close(socket_result);
   }

   #ifdef ALLOW_USE_PRINTF
   printf("send_status_requests_task has been created\n");
   #endif

   xSemaphoreTake(requests_binary_semaphore_g, portMAX_DELAY);

   if ((xEventGroupGetBits(general_event_group_g) & UPDATE_FIRMWARE_FLAG) == UPDATE_FIRMWARE_FLAG) {
      vTaskDelete(NULL);
   }

   if (!is_connected_to_wifi()) {
      #ifdef ALLOW_USE_PRINTF
      printf("Can't send status request, because not connected to AP. Time: %u\n", milliseconds_counter_g);
      #endif

      xSemaphoreGive(requests_binary_semaphore_g);
      schedule_sending_status_info(2000);
      vTaskDelete(NULL);
   }

   char signal_strength[5];
   snprintf(signal_strength, 5, "%d", signal_strength_g);
   char *device_name = get_string_from_rom(DEVICE_NAME);
   char errors_counter[6];
   snprintf(errors_counter, 6, "%u", errors_counter_g);
   char pending_connection_errors_counter[4];
   snprintf(pending_connection_errors_counter, 4, "%u", pending_connection_errors_counter_g);
   char uptime[11];
   snprintf(uptime, 11, "%u", milliseconds_counter_g / MILLISECONDS_COUNTER_DIVIDER);
   char *build_timestamp = "";
   char free_heap_space[7];
   snprintf(free_heap_space, 7, "%u", xPortGetFreeHeapSize());
   char *reset_reason = "";
   char *system_restart_reason = "";

   if (xEventGroupGetBits(general_event_group_g) & FIRST_STATUS_INFO_SENT_FLAG == 0) {
      char build_timestamp_filled[30];
      snprintf(build_timestamp_filled, 30, "%s", __TIMESTAMP__);
      build_timestamp = build_timestamp_filled;

      reset_reason = generate_reset_reason();

      SYSTEM_RESTART_REASON_TYPE system_restart_reason_type;

      system_rtc_mem_read(SYSTEM_RESTART_REASON_TYPE_RTC_ADDRESS, &system_restart_reason_type, 4);

      if (system_restart_reason_type == ACCESS_POINT_CONNECTION_ERROR) {
         int connection_error_code;
         char system_restart_reason_inner[31];

         system_rtc_mem_read(CONNECTION_ERROR_CODE_RTC_ADDRESS, &connection_error_code, 4);

         snprintf(system_restart_reason_inner, 31, "AP connection error. Code: %d", connection_error_code);
         system_restart_reason = system_restart_reason_inner;
      } else if (system_restart_reason_type == REQUEST_CONNECTION_ERROR) {
         int connection_error_code;
         char system_restart_reason_inner[25];

         system_rtc_mem_read(CONNECTION_ERROR_CODE_RTC_ADDRESS, &connection_error_code, 4);

         snprintf(system_restart_reason_inner, 25, "Request error. Code: %d", connection_error_code);
         system_restart_reason = system_restart_reason_inner;
      } else if (system_restart_reason_type == SOFTWARE_UPGRADE) {
         system_restart_reason = "Software upgrade";
      }

      unsigned int overwrite_value = 0xFF;
      system_rtc_mem_write(SYSTEM_RESTART_REASON_TYPE_RTC_ADDRESS, &overwrite_value, 4);
      system_rtc_mem_write(CONNECTION_ERROR_CODE_RTC_ADDRESS, &overwrite_value, 4);
   }

   char *status_info_request_payload_template_parameters[] =
         {signal_strength, device_name, errors_counter, pending_connection_errors_counter, uptime, build_timestamp, free_heap_space, reset_reason, system_restart_reason, NULL};
   char *status_info_request_payload_template = get_string_from_rom(STATUS_INFO_REQUEST_PAYLOAD_TEMPLATE);
   char *request_payload = set_string_parameters(status_info_request_payload_template, status_info_request_payload_template_parameters);

   FREE(device_name);
   FREE(status_info_request_payload_template);
   if (strlen(reset_reason) > 1) {
      FREE(reset_reason);
   }

   char *request_template = get_string_from_rom(STATUS_INFO_POST_REQUEST);
   unsigned short request_payload_length = strnlen(request_payload, 0xFFFF);
   char request_payload_length_string[6];
   snprintf(request_payload_length_string, 6, "%u", request_payload_length);
   char *server_ip_address = get_string_from_rom(SERVER_IP_ADDRESS);
   char *request_template_parameters[] = {request_payload_length_string, server_ip_address, request_payload, NULL};
   char *request = set_string_parameters(request_template, request_template_parameters);

   FREE(request_payload);
   FREE(request_template);
   FREE(server_ip_address);

   #ifdef ALLOW_USE_PRINTF
   printf("Request created:\n<<<\n%s>>>\n", request);
   #endif

   struct espconn *connection = (struct espconn *) ZALLOC(sizeof(struct espconn), __LINE__, milliseconds_counter_g);
   struct connection_user_data *user_data =
         (struct connection_user_data *) ZALLOC(sizeof(struct connection_user_data), __LINE__, milliseconds_counter_g);

   user_data->response_received = false;
   user_data->timeout_request_supervisor_task = NULL;
   user_data->request = request;
   user_data->response = NULL;
   user_data->execute_on_disconnect = status_request_on_disconnect_callback;
   user_data->execute_on_error = status_request_on_error_callback;
   user_data->parent_task = NULL;
   user_data->request_max_duration_time = REQUEST_MAX_DURATION_TIME;
   connection->reserve = user_data;
   connection->type = ESPCONN_TCP;
   connection->state = ESPCONN_NONE;

   // remote IP of TCP server
   unsigned char tcp_server_ip[] = {SERVER_IP_ADDRESS_1, SERVER_IP_ADDRESS_2, SERVER_IP_ADDRESS_3, SERVER_IP_ADDRESS_4};

   connection->proto.tcp = &user_tcp;
   memcpy(&connection->proto.tcp->remote_ip, tcp_server_ip, 4);
   connection->proto.tcp->remote_port = SERVER_PORT;
   connection->proto.tcp->local_port = espconn_port(); // local port of ESP8266

   espconn_regist_connectcb(connection, successfull_connected_tcp_handler_callback);
   espconn_regist_disconcb(connection, successfull_disconnected_tcp_handler_callback);
   espconn_regist_reconcb(connection, tcp_connection_error_handler_callback);
   espconn_regist_sentcb(connection, tcp_request_successfully_sent_handler_callback);
   espconn_regist_recvcb(connection, tcp_response_received_handler_callback);
   //espconn_regist_write_finish(&connection, tcp_request_successfully_written_into_buffer_handler_callback);

   establish_connection(connection);

   vTaskDelete(NULL);
}

void send_status_info() {
   xTaskCreate(send_status_info_task, "send_status_info_task", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
}

void schedule_sending_status_info(unsigned int timeout_ms) {
   os_timer_disarm(&status_sender_timer_g);
   os_timer_setfn(&status_sender_timer_g, (os_timer_func_t *) send_status_info, NULL);
   os_timer_arm(&status_sender_timer_g, timeout_ms, false);
}

void send_general_request(struct request_data *request_data_param, unsigned char task_priority) {
   if (read_flag(general_flags, UPDATE_FIRMWARE_FLAG)) {
      if (request_data_param->ms != NULL) {
         FREE(request_data_param->ms->alarm_source);
         FREE(request_data_param->ms);
      }
      FREE(request_data_param);
      return;
   }

   if ((request_data_param->request_type == FALSE_ALARM || request_data_param->request_type == ALARM) &&
         is_alarm_being_ignored(request_data_param->ms, request_data_param->request_type)) {
      #ifdef ALLOW_USE_PRINTF
      printf("\n %s alarm is being ignored. Time: %u\n", request_data_param->ms->alarm_source, milliseconds_counter_g);
      #endif

      FREE(request_data_param->ms->alarm_source);
      FREE(request_data_param->ms);
      FREE(request_data_param);
      return;
   }

   if (request_data_param->request_type == IMMOBILIZER_ACTIVATION) {
      if (read_flag(general_flags, IGNORE_IMMOBILIZER_FLAG)) {
         FREE(request_data_param);

         #ifdef ALLOW_USE_PRINTF
         printf(" Immobilizer is being ignored. Time: %u\n", milliseconds_counter_g);
         #endif

         return;
      }

      if (!read_flag(general_flags, IGNORE_IMMOBILIZER_BEEPER_FLAG)) {
         xTaskCreate(beep_task, "beep_task", 200, NULL, 1, NULL);
      }
      set_flag(&general_flags, IGNORE_IMMOBILIZER_BEEPER_FLAG);
      os_timer_disarm(&immobilizer_beeper_ignore_timer_g);
      os_timer_setfn(&immobilizer_beeper_ignore_timer_g, (os_timer_func_t *) stop_ignoring_immobilizer_beeper, NULL);
      os_timer_arm(&immobilizer_beeper_ignore_timer_g, IGNORE_IMMOBILIZER_BEEPER_SEC * 1000, false);

      set_flag(&general_flags, IGNORE_IMMOBILIZER_FLAG);
      os_timer_disarm(&immobilizer_ignore_timer_g);
      os_timer_setfn(&immobilizer_ignore_timer_g, (os_timer_func_t *) stop_ignoring_immobilizer, NULL);
      os_timer_arm(&immobilizer_ignore_timer_g, IGNORE_IMMOBILIZER_SEC * 1000, false);
   }

   xTaskCreate(send_general_request_task, "send_general_request_task", 256, request_data_param, task_priority, NULL);
}

void send_general_request_task(void *pvParameters) {
   #ifdef ALLOW_USE_PRINTF
   printf("\n send_general_request_task has been created. Time: %u\n", milliseconds_counter_g);
   #endif

   struct request_data *request_data_param = pvParameters;

   if (request_data_param->request_type == ALARM) {
      ignore_alarm(request_data_param->ms, IGNORE_ALARMS_TIMEOUT_SEC * 1000);
   } else if (request_data_param->request_type == FALSE_ALARM) {
      vTaskDelay((IGNORE_FALSE_ALARMS_TIMEOUT_SEC * 1000 + 500) / portTICK_RATE_MS);

      if (is_alarm_being_ignored(request_data_param->ms, request_data_param->request_type)) {
         #ifdef ALLOW_USE_PRINTF
         printf("\n %s is being ignored after timeout. Time: %u\n", request_data_param->ms->alarm_source, milliseconds_counter_g);
         #endif

         // Alarm occurred after false alarm within interval
         FREE(request_data_param->ms->alarm_source);
         FREE(request_data_param->ms);
         FREE(request_data_param);
         vTaskDelete(NULL);
      }

      ignore_alarm(request_data_param->ms, IGNORE_FALSE_ALARMS_TIMEOUT_SEC * 1000);
   }

   GeneralRequestType request_type = request_data_param->request_type;
   unsigned char alarm_source_length = (request_type == ALARM || request_type == FALSE_ALARM) ? (strlen(request_data_param->ms->alarm_source) + 1) : 0;
   char alarm_source[alarm_source_length];

   if (alarm_source_length > 0) {
      memcpy(alarm_source, request_data_param->ms->alarm_source, alarm_source_length);
   }

   FREE(request_data_param);

   for (;;) {
      xSemaphoreTake(requests_binary_semaphore_g, portMAX_DELAY);

      #ifdef ALLOW_USE_PRINTF
      printf("\n send_general_request_task started. Time: %u\n", milliseconds_counter_g);
      #endif

      if (!read_flag(general_flags, CONNECTED_TO_AP_FLAG)) {
         #ifdef ALLOW_USE_PRINTF
         printf("\n Can't send alarm request, because not connected to AP. Time: %u\n", milliseconds_counter_g);
         #endif

         vTaskDelay(2000 / portTICK_RATE_MS);
         continue;
      }

      if (read_flag(general_flags, REQUEST_ERROR_OCCURRED_FLAG)) {
         reset_flag(&general_flags, REQUEST_ERROR_OCCURRED_FLAG);

         vTaskDelay(REQUEST_IDLE_TIME_ON_ERROR);
      }

      char *server_ip_address = get_string_from_rom(SERVER_IP_ADDRESS);
      char *request_template;
      char *request;

      if (request_type == ALARM) {
         request_template = get_string_from_rom(ALARM_GET_REQUEST);
         char *request_template_parameters[] = {alarm_source, server_ip_address, NULL};
         request = set_string_parameters(request_template, request_template_parameters);
      } else if (request_type == FALSE_ALARM) {
         request_template = get_string_from_rom(FALSE_ALARM_GET_REQUEST);
         char *request_template_parameters[] = {alarm_source, server_ip_address, NULL};
         request = set_string_parameters(request_template, request_template_parameters);
      } else if (request_type == IMMOBILIZER_ACTIVATION) {
         request_template = get_string_from_rom(IMMOBILIZER_ACTIVATION_REQUEST);
         char *request_template_parameters[] = {server_ip_address, NULL};
         request = set_string_parameters(request_template, request_template_parameters);
      }

      FREE(request_template);
      FREE(server_ip_address);

      #ifdef ALLOW_USE_PRINTF
      printf("\n Request created:\n<<<\n%s>>>\n", request);
      #endif

      struct espconn *connection = (struct espconn *) ZALLOC(sizeof(struct espconn), __LINE__, milliseconds_counter_g);
      struct connection_user_data *user_data =
            (struct connection_user_data *) ZALLOC(sizeof(struct connection_user_data), __LINE__, milliseconds_counter_g);

      user_data->response_received = false;
      user_data->timeout_request_supervisor_task = NULL;
      user_data->request = request;
      user_data->response = NULL;
      user_data->execute_on_disconnect = general_request_on_disconnect_callback;
      user_data->execute_on_error = general_request_on_error_callback;
      user_data->parent_task = xTaskGetCurrentTaskHandle();
      user_data->request_max_duration_time = REQUEST_MAX_DURATION_TIME;
      connection->reserve = user_data;
      connection->type = ESPCONN_TCP;
      connection->state = ESPCONN_NONE;

      // remote IP of TCP server
      unsigned char tcp_server_ip[] = {SERVER_IP_ADDRESS_1, SERVER_IP_ADDRESS_2, SERVER_IP_ADDRESS_3, SERVER_IP_ADDRESS_4};

      connection->proto.tcp = &user_tcp;
      memcpy(&connection->proto.tcp->remote_ip, tcp_server_ip, 4);
      connection->proto.tcp->remote_port = SERVER_PORT;
      connection->proto.tcp->local_port = espconn_port(); // local port of ESP8266

      espconn_regist_connectcb(connection, successfull_connected_tcp_handler_callback);
      espconn_regist_disconcb(connection, successfull_disconnected_tcp_handler_callback);
      espconn_regist_reconcb(connection, tcp_connection_error_handler_callback);
      espconn_regist_sentcb(connection, tcp_request_successfully_sent_handler_callback);
      espconn_regist_recvcb(connection, tcp_response_received_handler_callback);
      //espconn_regist_write_finish(&connection, tcp_request_successfully_written_into_buffer_handler_callback);

      establish_connection(connection);
   }
}

void pins_config() {
   gpio_config_t output_pins;
   output_pins.mode = GPIO_MODE_OUTPUT;
   output_pins.pin_bit_mask = AP_CONNECTION_STATUS_LED_PIN | SERVER_AVAILABILITY_STATUS_LED_PIN;
   output_pins.pull_up_en = GPIO_PULLUP_DISABLE;

   gpio_config(&output_pins);
}

static void uart_event_task(void *pvParameters) {
   uart_event_t event;
   uint8_t *dtmp = (uint8_t *) malloc(UART_RD_BUF_SIZE);

   for (;;) {
      // Waiting for UART event.
      if (xQueueReceive(uart0_queue, (void *) &event, (portTickType) portMAX_DELAY)) {
         bzero(dtmp, UART_RD_BUF_SIZE);
         //ESP_LOGI(TAG, "uart[%d] event:", EX_UART_NUM);

         switch (event.type) {
            // Event of UART receiving data
            // We'd better handler data event fast, there would be much more data events than
            // other types of events. If we take too much time on data event, the queue might be full.
            case UART_DATA:
               //ESP_LOGI(TAG, "[UART DATA]: %d", event.size);
               int read_bytes = uart_read_bytes(UART_NUM_0, dtmp, event.size, portMAX_DELAY);
               //ESP_LOGI(TAG, "[DATA EVT]:");
               break;

            // Event of HW FIFO overflow detected
            case UART_FIFO_OVF:
               //ESP_LOGI(TAG, "hw fifo overflow");
               // If fifo overflow happened, you should consider adding flow control for your application.
               // The ISR has already reset the rx FIFO,
               // As an example, we directly flush the rx buffer here in order to read more data.
               uart_flush_input(UART_NUM_0);
               xQueueReset(uart0_queue);
               break;

            // Event of UART ring buffer full
            case UART_BUFFER_FULL:
               //ESP_LOGI(TAG, "ring buffer full");
               // If buffer full happened, you should consider increasing your buffer size
               // As an example, we directly flush the rx buffer here in order to read more data.
               uart_flush_input(UART_NUM_0);
               xQueueReset(uart0_queue);
               break;

            case UART_PARITY_ERR:
               //ESP_LOGI(TAG, "uart parity error");
               break;

            // Event of UART frame error
            case UART_FRAME_ERR:
               //ESP_LOGI(TAG, "uart frame error");
               break;

            // Others
            default:
               //ESP_LOGI(TAG, "uart event type: %d", event.type);
               break;
         }
      }
   }

   free(dtmp);
   dtmp = NULL;
   vTaskDelete(NULL);
}

static void on_wifi_connected()
{
   gpio_set_level(AP_CONNECTION_STATUS_LED_PIN, 1);
   repetitive_ap_connecting_errors_counter_g = 0;

   if (xEventGroupGetBits(general_event_group_g) & FIRST_STATUS_INFO_SENT_FLAG == 0) {
      send_status_info();
   }
}

static void on_wifi_disconnected()
{
   gpio_set_level(AP_CONNECTION_STATUS_LED_PIN, 0);
}

static void blink_on_send(gpio_num_t pin) {
   int initial_pin_state = gpio_get_level(pin);
   bool pin_state_changed_during_blinking = false;
   unsigned char i;

   for (i = 0; i < 4; i++) {
      bool set_pin = initial_pin_state == 1 ? i % 2 == 1 : i % 2 == 0;

      if (set_pin) {
         gpio_set_level(pin, 1);
      } else {
         gpio_set_level(pin, 0);
      }
      vTaskDelay(100 / portTICK_RATE_MS);
   }

   if (pin == AP_CONNECTION_STATUS_LED_PIN) {
      if (is_connected_to_wifi()) {
         gpio_set_level(pin, 1);
      } else {
         gpio_set_level(pin, 0);
      }
   }
}

static void blink_on_wifi_connection_task(void *pvParameters) {
   blink_on_send(AP_CONNECTION_STATUS_LED_PIN);
   vTaskDelete(NULL);
}

static void blink_on_wifi_connection() {
   xTaskCreate(blink_on_wifi_connection_task, "blink_on_wifi_connection_task", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
}

static void uart_config() {
   uart_config_t uart_config;
   uart_config.baud_rate = 115200;
   uart_config.data_bits = UART_DATA_8_BITS;
   uart_config.parity    = UART_PARITY_DISABLE;
   uart_config.stop_bits = UART_STOP_BITS_1;
   uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
   uart_param_config(UART_NUM_0, &uart_config);

   uart_driver_install(UART_NUM_0, UART_BUF_SIZE, 0, 10, &uart0_queue);
   xTaskCreate(uart_event_task, "uart_event_task", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
}

/**
 * Created as a workaround to handle unknown issues.
 */
void check_errors_amount() {
   bool restart = false;

   if (repetitive_request_errors_counter_g >= MAX_REPETITIVE_ALLOWED_ERRORS_AMOUNT) {
      #ifdef ALLOW_USE_PRINTF
      printf("\n Request errors amount: %u\n", repetitive_request_errors_counter_g);
      #endif

      SYSTEM_RESTART_REASON_TYPE system_restart_reason_type = REQUEST_CONNECTION_ERROR;

      system_rtc_mem_write(SYSTEM_RESTART_REASON_TYPE_RTC_ADDRESS, &system_restart_reason_type, 4);
      system_rtc_mem_write(CONNECTION_ERROR_CODE_RTC_ADDRESS, &connection_error_code_g, 4);
      restart = true;
   } else if (repetitive_ap_connecting_errors_counter_g >= MAX_REPETITIVE_ALLOWED_ERRORS_AMOUNT) {
      #ifdef ALLOW_USE_PRINTF
      printf("\n AP connection errors amount: %u\n", repetitive_ap_connecting_errors_counter_g);
      #endif

      SYSTEM_RESTART_REASON_TYPE system_restart_reason_type = ACCESS_POINT_CONNECTION_ERROR;
      //STATION_STATUS status = wifi_station_get_connect_status();

      system_rtc_mem_write(SYSTEM_RESTART_REASON_TYPE_RTC_ADDRESS, &system_restart_reason_type, 4);
      system_rtc_mem_write(CONNECTION_ERROR_CODE_RTC_ADDRESS, 0xFFFF, 4);
      restart = true;
   }

   if (restart) {
      esp_restart();
   }
}

void user_init(void) {
   general_event_group_g = xEventGroupCreate();

   pins_config();
   uart_config();

   vTaskDelay(1000 / portTICK_RATE_MS);

   //ESP_LOGI(TAG, "Software is running from: %s\n", system_upgrade_userbin_check() ? "user2.bin" : "user1.bin");

   requests_binary_semaphore_g = xSemaphoreCreateBinary();

   //wifi_set_event_handler_cb(wifi_event_handler_callback);
   wifi_init_sta(on_wifi_connected, on_wifi_disconnected, blink_on_wifi_connection);

   //os_timer_setfn(&ap_autoconnect_timer_g, (os_timer_func_t *) ap_autoconnect, NULL);
   //os_timer_arm(&ap_autoconnect_timer_g, AP_AUTOCONNECT_INTERVAL_MS, true);
   //xTaskCreate(ap_connect_task, "ap_connect_task", 180, NULL, 1, NULL);

   xTaskCreate(scan_access_point_task, "scan_access_point_task", configMINIMAL_STACK_SIZE, NULL, 1, NULL);

   //xTaskCreate(testing_task, "testing_task", 200, NULL, 1, NULL);

   os_timer_setfn(&errors_checker_timer_g, (os_timer_func_t *) check_errors_amount, NULL);
   os_timer_arm(&errors_checker_timer_g, ERRORS_CHECKER_INTERVAL_MS, true);

   start_100millisecons_counter();
}
