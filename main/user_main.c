/**
 * Pins 4 and 5 on some ESP8266-07 are exchanged on silk screen!!!
 *
 * SPI SPI_CPOL & SPI_CPHA:
 *    SPI_CPOL - (0) Clock is low when inactive
 *               (1) Clock is high when inactive
 *    SPI_CPHA - (0) Data is valid on clock leading edge
 *               (1) Data is valid on clock trailing edge
 */

#include "user_main.h"

#ifdef ALLOW_USE_PRINTF
static const char REMAINING_STACK_SIZE_MSG[] = "\nRemaining stack size was %u bytes. Code line: %u\n";
static const char REQUEST_ERRORS_AMOUNT_MSG[] = "\nRequest errors amount: %u\n";
static const char CONNECTION_ERRORS_AMOUNT_MSG[] = "\nAP connection errors amount: %u\n";
static const char AP_SIGNAL_STRENGTH_MSG[] = "\nSignal strength of AP: %d\n";
static const char FAILED_TO_ALLOCATE_SOCKET_MSG[] = "\nFailed to allocate socket\n";
static const char ALLOCATED_SOCKET_MSG[] = "\nSocket %d has been allocated\n";
static const char SOCKET_CONNECTION_ERROR_MSG[] = "\nSocket connection failed. Error: %d\n";
static const char SOCKET_CONNECTED_ERROR_MSG[] = "\nSocket %d has been connected\n";
static const char NOT_CONNECTED_TO_WI_FI_MSG[] = "\nNot connected to Wi-Fi. To be deleted task\n";
static const char REQUEST_PAYLOAD_CONTENT_MSG[] = "\nRequest payload: %s\n";
static const char CREATED_REQUEST_CONTENT_MSG[] = "\nCreated request: %s\n";
static const char ERROR_ON_SENT_REQUEST_MSG[] = "\nError occurred during sending. Error no.: %d\n";
static const char SUCCESSFULLY_SENT_REQUEST_MSG[] = "\nRequest has been sent. Socket %d\n";
static const char ERROR_ON_RECEIVE_RESPONSE_MSG[] = "\nReceive failed. Error no.: %d\n";
static const char SHUTTING_DOWN_SOCKET_MSG[] = "Shutting down socket and restarting...";
#endif

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

char *responses[10];
static EventGroupHandle_t general_event_group_g;

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

void scan_access_point_task(void *pvParameters) {
   long rescan_when_connected_task_delay = 10 * 60 * 1000 / portTICK_RATE_MS; // 10 mins
   long rescan_when_not_connected_task_delay = 10 * 1000 / portTICK_RATE_MS; // 10 secs
   wifi_scan_config_t scan_config;
   unsigned short scanned_access_points_amount = 1;
   wifi_ap_record_t scanned_access_points[1];

   scan_config.ssid = (unsigned char *) ACCESS_POINT_NAME;
   scan_config.scan_type = WIFI_SCAN_TYPE_PASSIVE;

   for (;;) {
      if (esp_wifi_scan_start(&scan_config, true) == ESP_OK &&
            esp_wifi_scan_get_ap_records(&scanned_access_points_amount, scanned_access_points) == ESP_OK) {
         signal_strength_g = scanned_access_points[0].rssi;

         #ifdef ALLOW_USE_PRINTF
         printf(AP_SIGNAL_STRENGTH_MSG, signal_strength_g);
         #endif

         vTaskDelay(rescan_when_connected_task_delay);
      } else {
         vTaskDelay(rescan_when_not_connected_task_delay);
      }
   }
}

/*void timeout_request_supervisor_task(void *pvParameters) {
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
}*/

void blink_leds_while_updating_task(void *pvParameters) {
   for (;;) {
      if (gpio_get_level(AP_CONNECTION_STATUS_LED_PIN)) {
         gpio_set_level(AP_CONNECTION_STATUS_LED_PIN, 0);
         gpio_set_level(SERVER_AVAILABILITY_STATUS_LED_PIN, 1);
      } else {
         gpio_set_level(AP_CONNECTION_STATUS_LED_PIN, 1);
         gpio_set_level(SERVER_AVAILABILITY_STATUS_LED_PIN, 0);
      }

      vTaskDelay(100 / portTICK_RATE_MS);
   }
}

void check_for_update_firmware(char *response) {
   if (strstr(response, UPDATE_FIRMWARE) != NULL) {
      xEventGroupSetBits(general_event_group_g, UPDATE_FIRMWARE_FLAG);
   }
}

/*void upgrade_firmware() {
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
}*/

/*void ota_finished_callback(void *arg) {
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
}*/

/*void establish_connection(struct espconn *connection) {
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
}*/

char *sent_request(char *request) {
   int socket_result = socket(AF_INET, SOCK_STREAM, IPPROTO_IP); // SOCK_STREAM - TCP

   if(socket_result < 0) {
      #ifdef ALLOW_USE_PRINTF
      printf(FAILED_TO_ALLOCATE_SOCKET_MSG);
      #endif

      return NULL;
   }
   #ifdef ALLOW_USE_PRINTF
   printf(ALLOCATED_SOCKET_MSG, socket_result);
   #endif

   struct sockaddr_in destination_address;
   destination_address.sin_addr.s_addr = inet_addr(SERVER_IP_ADDRESS);
   destination_address.sin_family = AF_INET;
   destination_address.sin_port = htons(SERVER_PORT);

   int connection_result = connect(socket_result, (struct sockaddr *) &destination_address, sizeof(destination_address));

   if(connection_result != 0) {
      #ifdef ALLOW_USE_PRINTF
      printf(SOCKET_CONNECTION_ERROR_MSG, connection_result);
      #endif

      close(socket_result);
      return NULL;
   }

   #ifdef ALLOW_USE_PRINTF
   printf(SOCKET_CONNECTED_ERROR_MSG, socket_result);
   #endif

   if (!is_connected_to_wifi()) {
      #ifdef ALLOW_USE_PRINTF
      printf(NOT_CONNECTED_TO_WI_FI_MSG);
      #endif

      return NULL;
   }

   int send_result = send(socket_result, request, strlen(request), 0);

   if (send_result < 0) {
      #ifdef ALLOW_USE_PRINTF
      printf(ERROR_ON_SENT_REQUEST_MSG, send_result);
      #endif

      return NULL;
   }
   #ifdef ALLOW_USE_PRINTF
   printf(SUCCESSFULLY_SENT_REQUEST_MSG, socket_result);
   #endif

   unsigned char buffer_size = 255;
   char *rx_buffer = MALLOC(buffer_size, __LINE__, milliseconds_counter_g);
   int len = recv(socket_result, rx_buffer, buffer_size - 1, 0);

   if (len < 0) {
      #ifdef ALLOW_USE_PRINTF
      printf(ERROR_ON_RECEIVE_RESPONSE_MSG, len);
      #endif

      FREE(rx_buffer, __LINE__);
      return NULL;
   } else {
      rx_buffer[len] = 0; // Null-terminate whatever we received and treat like a string

      #ifdef ALLOW_USE_PRINTF
      printf("\nReceived %d bytes\n", len);
      printf("\nResponse: %s\n", rx_buffer);
      #endif
   }

   if (socket_result != -1) {
      #ifdef ALLOW_USE_PRINTF
      printf(SHUTTING_DOWN_SOCKET_MSG);
      #endif

      shutdown(socket_result, 0);
      close(socket_result);
   }

   return rx_buffer;
}

void send_status_info_task(void *pvParameters) {
   /*if ((xEventGroupGetBits(general_event_group_g) & UPDATE_FIRMWARE_FLAG) == UPDATE_FIRMWARE_FLAG) {
      vTaskDelete(NULL);
   }*/

   char signal_strength[5];
   snprintf(signal_strength, 5, "%d", signal_strength_g);
   char errors_counter[6];
   snprintf(errors_counter, 6, "%u", errors_counter_g);
   char pending_connection_errors_counter[4];
   snprintf(pending_connection_errors_counter, 4, "%u", pending_connection_errors_counter_g);
   char uptime[11];
   snprintf(uptime, 11, "%u", milliseconds_counter_g / MILLISECONDS_COUNTER_DIVIDER);
   char *build_timestamp = "";
   char free_heap_space[7];
   snprintf(free_heap_space, 7, "%u", esp_get_free_heap_size());
   char *reset_reason = "";
   char *system_restart_reason = "";

   if ((xEventGroupGetBits(general_event_group_g) & FIRST_STATUS_INFO_SENT_FLAG) == 0) {
      char build_timestamp_filled[30];
      snprintf(build_timestamp_filled, 30, "%s", __TIMESTAMP__);
      build_timestamp = build_timestamp_filled;

      esp_reset_reason_t rst_info = esp_reset_reason();
      char filled_reset_reason[2];
      itoa(rst_info, filled_reset_reason, 2);
      reset_reason = filled_reset_reason;

      SYSTEM_RESTART_REASON_TYPE system_restart_reason_type = ACCESS_POINT_CONNECTION_ERROR;

      //system_rtc_mem_read(SYSTEM_RESTART_REASON_TYPE_RTC_ADDRESS, &system_restart_reason_type, 4);

      if (system_restart_reason_type == ACCESS_POINT_CONNECTION_ERROR) {
         int connection_error_code = 1;
         char system_restart_reason_inner[31];

         //system_rtc_mem_read(CONNECTION_ERROR_CODE_RTC_ADDRESS, &connection_error_code, 4);

         snprintf(system_restart_reason_inner, 31, "AP connection error. Code: %d", connection_error_code);
         system_restart_reason = system_restart_reason_inner;
      } else if (system_restart_reason_type == REQUEST_CONNECTION_ERROR) {
         int connection_error_code = 1;
         char system_restart_reason_inner[25];

         //system_rtc_mem_read(CONNECTION_ERROR_CODE_RTC_ADDRESS, &connection_error_code, 4);

         snprintf(system_restart_reason_inner, 25, "Request error. Code: %d", connection_error_code);
         system_restart_reason = system_restart_reason_inner;
      } else if (system_restart_reason_type == SOFTWARE_UPGRADE) {
         system_restart_reason = "Software upgrade";
      }

      //unsigned int overwrite_value = 0xFF;
      //system_rtc_mem_write(SYSTEM_RESTART_REASON_TYPE_RTC_ADDRESS, &overwrite_value, 4);
      //system_rtc_mem_write(CONNECTION_ERROR_CODE_RTC_ADDRESS, &overwrite_value, 4);
   }

   const char *status_info_request_payload_template_parameters[] =
         {signal_strength, DEVICE_NAME, errors_counter, pending_connection_errors_counter, uptime, build_timestamp, free_heap_space, reset_reason, system_restart_reason, NULL};
   char *request_payload = set_string_parameters(STATUS_INFO_REQUEST_PAYLOAD_TEMPLATE, status_info_request_payload_template_parameters);

   #ifdef ALLOW_USE_PRINTF
   printf(REQUEST_PAYLOAD_CONTENT_MSG, request_payload);
   #endif

   unsigned short request_payload_length = strnlen(request_payload, 0xFFFF);
   char request_payload_length_string[6];
   snprintf(request_payload_length_string, 6, "%u", request_payload_length);
   const char *request_template_parameters[] = {request_payload_length_string, SERVER_IP_ADDRESS, request_payload, NULL};
   char *request = set_string_parameters(STATUS_INFO_POST_REQUEST, request_template_parameters);

   #ifdef ALLOW_USE_PRINTF
   printf(CREATED_REQUEST_CONTENT_MSG, request);
   #endif

   FREE(request_payload, __LINE__);

   sent_request(request);

   FREE(request, __LINE__);


   vTaskDelete(NULL);
}

void send_status_info() {
   xTaskCreate(send_status_info_task, "send_status_info_task", configMINIMAL_STACK_SIZE * 2, NULL, 1, NULL);
}

void schedule_sending_status_info(unsigned int timeout_ms) {
   os_timer_disarm(&status_sender_timer_g);
   os_timer_setfn(&status_sender_timer_g, (os_timer_func_t *) send_status_info, NULL);
   os_timer_arm(&status_sender_timer_g, timeout_ms, false);
}

/*void send_general_request(struct request_data *request_data_param, unsigned char task_priority) {
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
}*/

/*void send_general_request_task(void *pvParameters) {
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
}*/

void pins_config() {
   gpio_config_t output_pins;
   output_pins.mode = GPIO_MODE_OUTPUT;
   output_pins.pin_bit_mask = AP_CONNECTION_STATUS_LED_PIN | SERVER_AVAILABILITY_STATUS_LED_PIN;
   output_pins.pull_up_en = GPIO_PULLUP_DISABLE;

   gpio_config(&output_pins);
}

static void uart_event_task(void *pvParameters) {
   uart_event_t event;
   unsigned char *dtmp = (unsigned char *) ZALLOC(UART_RD_BUF_SIZE, __LINE__, milliseconds_counter_g);

   for (;;) {
      #ifdef MONITOR_STACK_SIZE
      unsigned int stack_size = (unsigned int) uxTaskGetStackHighWaterMark(NULL);
      printf(REMAINING_STACK_SIZE_MSG, stack_size * 4, __LINE__);
      #endif

      // Waiting for UART event.
      if (xQueueReceive(uart0_queue, (void *) &event, (portTickType) portMAX_DELAY)) {
         //ESP_LOGI(TAG, "uart[%d] event:", EX_UART_NUM);

         switch (event.type) {
            // Event of UART receiving data
            // We'd better handler data event fast, there would be much more data events than
            // other types of events. If we take too much time on data event, the queue might be full.
            case UART_DATA:
               //ESP_LOGI(TAG, "[UART DATA]: %d", event.size);
               uart_read_bytes(UART_NUM_0, dtmp, event.size, portMAX_DELAY);
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

   FREE(dtmp, __LINE__);
   dtmp = NULL;
   vTaskDelete(NULL);
}

static void on_wifi_connected()
{
   gpio_set_level(AP_CONNECTION_STATUS_LED_PIN, 1);
   repetitive_ap_connecting_errors_counter_g = 0;

   if ((xEventGroupGetBits(general_event_group_g) & FIRST_STATUS_INFO_SENT_FLAG) == 0) {
      send_status_info();
   }
}

static void on_wifi_disconnected()
{
   gpio_set_level(AP_CONNECTION_STATUS_LED_PIN, 0);
}

static void blink_on_send(gpio_num_t pin) {
   int initial_pin_state = gpio_get_level(pin);
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
   xTaskCreate(uart_event_task, "uart_event_task", configMINIMAL_STACK_SIZE * 2, NULL, 1, NULL);
}

/**
 * Created as a workaround to handle unknown issues.
 */
void check_errors_amount() {
   bool restart = false;

   if (repetitive_request_errors_counter_g >= MAX_REPETITIVE_ALLOWED_ERRORS_AMOUNT) {
      #ifdef ALLOW_USE_PRINTF
      printf(REQUEST_ERRORS_AMOUNT_MSG, repetitive_request_errors_counter_g);
      #endif

      //SYSTEM_RESTART_REASON_TYPE system_restart_reason_type = REQUEST_CONNECTION_ERROR;

      //system_rtc_mem_write(SYSTEM_RESTART_REASON_TYPE_RTC_ADDRESS, &system_restart_reason_type, 4);
      //system_rtc_mem_write(CONNECTION_ERROR_CODE_RTC_ADDRESS, &connection_error_code_g, 4);
      restart = true;
   } else if (repetitive_ap_connecting_errors_counter_g >= MAX_REPETITIVE_ALLOWED_ERRORS_AMOUNT) {
      #ifdef ALLOW_USE_PRINTF
      printf(CONNECTION_ERRORS_AMOUNT_MSG, repetitive_ap_connecting_errors_counter_g);
      #endif

      //SYSTEM_RESTART_REASON_TYPE system_restart_reason_type = ACCESS_POINT_CONNECTION_ERROR;
      //STATION_STATUS status = wifi_station_get_connect_status();

      //system_rtc_mem_write(SYSTEM_RESTART_REASON_TYPE_RTC_ADDRESS, &system_restart_reason_type, 4);
      //system_rtc_mem_write(CONNECTION_ERROR_CODE_RTC_ADDRESS, 0xFFFF, 4);
      restart = true;
   }

   if (restart) {
      esp_restart();
   }
}

void app_main(void) {
   general_event_group_g = xEventGroupCreate();

   pins_config();
   uart_config();

   vTaskDelay(1000 / portTICK_RATE_MS);

   tcpip_adapter_init();
   tcpip_adapter_dhcpc_stop(TCPIP_ADAPTER_IF_STA); // Stop DHCP client
   tcpip_adapter_ip_info_t ip_info;
   ip_info.ip.addr = inet_addr(OWN_IP_ADDRESS);
   ip_info.gw.addr = inet_addr(OWN_GETAWAY_ADDRESS);
   ip_info.netmask.addr = inet_addr(OWN_NETMASK);
   tcpip_adapter_set_ip_info(TCPIP_ADAPTER_IF_STA, &ip_info);

   //ESP_LOGI(TAG, "Software is running from: %s\n", system_upgrade_userbin_check() ? "user2.bin" : "user1.bin");

   //wifi_set_event_handler_cb(wifi_event_handler_callback);
   wifi_init_sta(on_wifi_connected, on_wifi_disconnected, blink_on_wifi_connection);

   //os_timer_setfn(&ap_autoconnect_timer_g, (os_timer_func_t *) ap_autoconnect, NULL);
   //os_timer_arm(&ap_autoconnect_timer_g, AP_AUTOCONNECT_INTERVAL_MS, true);
   //xTaskCreate(ap_connect_task, "ap_connect_task", 180, NULL, 1, NULL);

   //xTaskCreate(scan_access_point_task, "scan_access_point_task", configMINIMAL_STACK_SIZE, NULL, 1, NULL);

   //xTaskCreate(testing_task, "testing_task", 200, NULL, 1, NULL);

   os_timer_setfn(&errors_checker_timer_g, (os_timer_func_t *) check_errors_amount, NULL);
   os_timer_arm(&errors_checker_timer_g, ERRORS_CHECKER_INTERVAL_MS, true);

   start_100millisecons_counter();
}
