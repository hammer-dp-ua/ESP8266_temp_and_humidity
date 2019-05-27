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
static const char REQUEST_PAYLOAD_CONTENT_MSG[] = "\nRequest payload: %s\n";
static const char CREATED_REQUEST_CONTENT_MSG[] = "\nCreated request: %s\n";
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
      rtc_mem_write(SYSTEM_RESTART_REASON_TYPE_RTC_ADDRESS, &reason, 4);

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

void send_status_info_task(void *pvParameters) {
   if ((xEventGroupGetBits(general_event_group_g) & UPDATE_FIRMWARE_FLAG) == UPDATE_FIRMWARE_FLAG) {
      vTaskDelete(NULL);
   }

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

      switch(rst_info) {
         case ESP_RST_UNKNOWN:
            reset_reason = "Unknown";
            break;
         case ESP_RST_POWERON:
            reset_reason = "Power on";
            break;
         case ESP_RST_EXT:
            reset_reason = "Reset by external pin";
            break;
         case ESP_RST_SW:
            reset_reason = "Software";
            break;
         case ESP_RST_PANIC:
            reset_reason = "Exception/panic";
            break;
         case ESP_RST_INT_WDT:
            reset_reason = "Watchdog";
            break;
         case ESP_RST_TASK_WDT:
            reset_reason = "Task watchdog";
            break;
         case ESP_RST_WDT:
            reset_reason = "Other watchdog";
            break;
         case ESP_RST_DEEPSLEEP:
            reset_reason = "Deep sleep";
            break;
         case ESP_RST_BROWNOUT:
            reset_reason = "Brownout";
            break;
         case ESP_RST_SDIO:
            reset_reason = "SDIO";
            break;
      }

      SYSTEM_RESTART_REASON_TYPE system_restart_reason_type;

      rtc_mem_read(SYSTEM_RESTART_REASON_TYPE_RTC_ADDRESS, &system_restart_reason_type, 4);

      if (system_restart_reason_type == ACCESS_POINT_CONNECTION_ERROR) {
         int connection_error_code = 1;
         char system_restart_reason_inner[31];

         rtc_mem_read(CONNECTION_ERROR_CODE_RTC_ADDRESS, &connection_error_code, 4);

         snprintf(system_restart_reason_inner, 31, "AP connections error. Code: %d", connection_error_code);
         system_restart_reason = system_restart_reason_inner;
      } else if (system_restart_reason_type == REQUEST_CONNECTION_ERROR) {
         int connection_error_code = 1;
         char system_restart_reason_inner[25];

         rtc_mem_read(CONNECTION_ERROR_CODE_RTC_ADDRESS, &connection_error_code, 4);

         snprintf(system_restart_reason_inner, 25, "Requests error. Code: %d", connection_error_code);
         system_restart_reason = system_restart_reason_inner;
      } else if (system_restart_reason_type == SOFTWARE_UPGRADE) {
         system_restart_reason = "Software upgrade";
      }

      unsigned int overwrite_value = 0xFFFF;
      rtc_mem_write(SYSTEM_RESTART_REASON_TYPE_RTC_ADDRESS, &overwrite_value, 4);
      rtc_mem_write(CONNECTION_ERROR_CODE_RTC_ADDRESS, &overwrite_value, 4);
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
   FREE(request_payload, __LINE__);

   #ifdef ALLOW_USE_PRINTF
   printf(CREATED_REQUEST_CONTENT_MSG, request);
   #endif

   char *response = send_request(request, 255, milliseconds_counter_g);

   if (response == NULL) {
      repetitive_request_errors_counter_g++;
      gpio_set_level(SERVER_AVAILABILITY_STATUS_LED_PIN, 0);
   } else {
      if (strstr(response, RESPONSE_SERVER_SENT_OK)) {
         gpio_set_level(SERVER_AVAILABILITY_STATUS_LED_PIN, 1);

         #ifdef ALLOW_USE_PRINTF
         printf("\nResponse OK\n");
         #endif

         if (strstr(response, UPDATE_FIRMWARE)) {
            xEventGroupSetBits(general_event_group_g, UPDATE_FIRMWARE_FLAG);
            xTaskCreate(blink_leds_while_updating_task, "blink_leds_while_updating_task", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
            update_firmware();
         }
      } else {
         repetitive_request_errors_counter_g++;
         gpio_set_level(SERVER_AVAILABILITY_STATUS_LED_PIN, 0);
      }

      FREE(response, __LINE__);
   }

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

void pins_config() {
   gpio_config_t output_pins;
   output_pins.mode = GPIO_MODE_OUTPUT;
   output_pins.pin_bit_mask = (1<<AP_CONNECTION_STATUS_LED_PIN) | (1<<SERVER_AVAILABILITY_STATUS_LED_PIN);
   output_pins.pull_up_en = GPIO_PULLUP_DISABLE;
   output_pins.pull_down_en = GPIO_PULLDOWN_DISABLE;

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

static void on_wifi_connected() {
   gpio_set_level(AP_CONNECTION_STATUS_LED_PIN, 1);
   repetitive_ap_connecting_errors_counter_g = 0;

   if ((xEventGroupGetBits(general_event_group_g) & FIRST_STATUS_INFO_SENT_FLAG) == 0) {
      send_status_info();
   }
}

static void on_wifi_disconnected() {
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

      SYSTEM_RESTART_REASON_TYPE system_restart_reason_type = REQUEST_CONNECTION_ERROR;

      rtc_mem_write(SYSTEM_RESTART_REASON_TYPE_RTC_ADDRESS, &system_restart_reason_type, 4);
      rtc_mem_write(CONNECTION_ERROR_CODE_RTC_ADDRESS, &connection_error_code_g, 4);
      restart = true;
   } else if (repetitive_ap_connecting_errors_counter_g >= MAX_REPETITIVE_ALLOWED_ERRORS_AMOUNT) {
      #ifdef ALLOW_USE_PRINTF
      printf(CONNECTION_ERRORS_AMOUNT_MSG, repetitive_ap_connecting_errors_counter_g);
      #endif

      SYSTEM_RESTART_REASON_TYPE system_restart_reason_type = ACCESS_POINT_CONNECTION_ERROR;

      rtc_mem_write(SYSTEM_RESTART_REASON_TYPE_RTC_ADDRESS, &system_restart_reason_type, 4);
      restart = true;
   }

   if (restart) {
      esp_restart();
   }
}

static esp_err_t i2c_master_init() {
   i2c_config_t conf;
   conf.mode = I2C_MODE_MASTER;
   conf.sda_io_num = I2C_MASTER_SDA_IO;
   conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
   conf.scl_io_num = I2C_MASTER_SCL_IO;
   conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
   ESP_ERROR_CHECK(i2c_driver_install(I2C_MASTER_NUM, conf.mode));
   ESP_ERROR_CHECK(i2c_param_config(I2C_MASTER_NUM, &conf));
   return ESP_OK;
}

static void testing_task(void *pvParameters) {
   while(1) {
      float temp;
      esp_err_t i2c_result = sht21_get_temperature(&temp);

      #ifdef ALLOW_USE_PRINTF
      printf("\nMeasured temp.: %d, I2C result: 0x%X\n", (int) (temp * 100), i2c_result);
      #endif

      float humidity;
      i2c_result = sht21_get_humidity(&humidity);

      #ifdef ALLOW_USE_PRINTF
      printf("\nMeasured humidity: %d, I2C result: 0x%X\n", (int) (humidity * 100), i2c_result);
      #endif

      vTaskDelay(3000 / portTICK_RATE_MS);
   }
}

void app_main(void) {
   general_event_group_g = xEventGroupCreate();

   pins_config();
   uart_config();
   i2c_master_init();

   vTaskDelay(3000 / portTICK_RATE_MS);

   #ifdef ALLOW_USE_PRINTF
   const esp_partition_t *running = esp_ota_get_running_partition();
   printf("\nRunning partition type: %d, subtype: %d, size: 0x%X, offset: 0x%X\n",
         running->type, running->subtype, running->size, running->address);
   #endif

   tcpip_adapter_init();
   tcpip_adapter_dhcpc_stop(TCPIP_ADAPTER_IF_STA); // Stop DHCP client
   tcpip_adapter_ip_info_t ip_info;
   ip_info.ip.addr = inet_addr(OWN_IP_ADDRESS);
   ip_info.gw.addr = inet_addr(OWN_GETAWAY_ADDRESS);
   ip_info.netmask.addr = inet_addr(OWN_NETMASK);
   tcpip_adapter_set_ip_info(TCPIP_ADAPTER_IF_STA, &ip_info);

   //ESP_LOGI(TAG, "Software is running from: %s\n", system_upgrade_userbin_check() ? "user2.bin" : "user1.bin");

   wifi_init_sta(on_wifi_connected, on_wifi_disconnected, blink_on_wifi_connection);

   //os_timer_setfn(&ap_autoconnect_timer_g, (os_timer_func_t *) ap_autoconnect, NULL);
   //os_timer_arm(&ap_autoconnect_timer_g, AP_AUTOCONNECT_INTERVAL_MS, true);

   //xTaskCreate(scan_access_point_task, "scan_access_point_task", configMINIMAL_STACK_SIZE, NULL, 1, NULL);

   //xTaskCreate(testing_task, "testing_task", configMINIMAL_STACK_SIZE, NULL, 1, NULL);

   os_timer_setfn(&errors_checker_timer_g, (os_timer_func_t *) check_errors_amount, NULL);
   os_timer_arm(&errors_checker_timer_g, ERRORS_CHECKER_INTERVAL_MS, true);

   start_100millisecons_counter();
}
