#include <stdio.h>
#include "esp_system.h"

#include "portmacro.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"

#include "driver/uart.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/adc.h"

#include "esp8266/rtc_register.h"
#include "internal/esp_system_internal.h"
#include "esp_wifi.h"
#include "string.h"
#include "utils.h"
#include "event_groups.h"
#include "global_definitions.h"
#include "malloc_logger.h"

// components
#include "sht21.h"
#include "ota.h"

#ifndef MAIN_HEADER
#define MAIN_HEADER

#define AP_CONNECTION_STATUS_LED_PIN         GPIO_NUM_5
#define SERVER_AVAILABILITY_STATUS_LED_PIN   GPIO_NUM_4

#define I2C_MASTER_SCL_IO  GPIO_NUM_2  // gpio number for I2C master clock, D4
#define I2C_MASTER_SDA_IO  GPIO_NUM_12 // gpio number for I2C master data, D6
#define I2C_MASTER_NUM     I2C_NUM_0 // I2C port number for master dev

#define SERVER_IS_AVAILABLE_FLAG
#define FIRST_STATUS_INFO_SENT_FLAG (1 << 0)
#define UPDATE_FIRMWARE_FLAG        (1 << 1)
#define REQUEST_ERROR_OCCURRED_FLAG

#define REQUEST_IDLE_TIME_ON_ERROR        (10000 / portTICK_RATE_MS) // 10 sec
#define REQUEST_MAX_DURATION_TIME         (10000 / portTICK_RATE_MS) // 10 sec
#define STATUS_REQUESTS_SEND_INTERVAL_MS  (30 * 1000)
#define STATUS_REQUESTS_SEND_INTERVAL     (STATUS_REQUESTS_SEND_INTERVAL_MS / portTICK_RATE_MS) // 30 sec

#define ERRORS_CHECKER_INTERVAL_MS        (10 * 1000)

#define MILLISECONDS_COUNTER_DIVIDER 10

#define MAX_REPETITIVE_ALLOWED_ERRORS_AMOUNT 15

#define UART_BUF_SIZE (UART_FIFO_LEN + 1)
#define UART_RD_BUF_SIZE (UART_BUF_SIZE)

#define SYSTEM_RESTART_REASON_TYPE_RTC_ADDRESS  64
#define CONNECTION_ERROR_CODE_RTC_ADDRESS       SYSTEM_RESTART_REASON_TYPE_RTC_ADDRESS + 1

typedef enum {
   ACCESS_POINT_CONNECTION_ERROR = 1,
   REQUEST_CONNECTION_ERROR,
   SOFTWARE_UPGRADE
} SYSTEM_RESTART_REASON_TYPE;

const char SEND_STATUS_INFO_TASK_NAME[] = "send_status_info_task";
const char BLINK_ON_WIFI_CONNECTION_TASK_NAME[] = "blink_on_wifi_connection_task";
const char SCAN_ACCESS_POINT_TASK_NAME[] = "scan_access_point_task";
const char BLINK_LEDS_WHILE_UPDATING_TASK_NAME[] = "blink_leds_while_updating_task";
const char UART_EVENT_TASK_NAME[] = "uart_event_task";

const char RESPONSE_SERVER_SENT_OK[] = "\"statusCode\":\"OK\"";
const char STATUS_INFO_POST_REQUEST[] =
      "POST /server/esp8266/statusInfo HTTP/1.1\r\n"
      "Content-Length: <1>\r\n"
      "Host: <2>\r\n"
      "User-Agent: ESP8266\r\n"
      "Content-Type: application/json\r\n"
      "Connection: close\r\n"
      "Accept: application/json\r\n\r\n"
      "<3>\r\n";
const char STATUS_INFO_REQUEST_PAYLOAD_TEMPLATE[] =
      "{"
      "\"gain\":\"<1>\","
      "\"deviceName\":\"<2>\","
      "\"errors\":<3>,"
      "\"pendingConnectionErrors\":<4>,"
      "\"uptime\":<5>,"
      "\"buildTimestamp\":\"<6>\","
      "\"freeHeapSpace\":<7>,"
      "\"resetReason\":\"<8>\","
      "\"systemRestartReason\":\"<9>\","
      "\"temperature\":<10>,"
      "\"temperatureRaw\":<11>,"
      "\"humidity\":<12>,"
      "\"light\":<13>"
      "}";
const char UPDATE_FIRMWARE[] = "\"updateFirmware\":true";

static void pins_config();
static void uart_config();
static void i2c_master_init();
static void i2c_master_deinit();
void send_long_polling_requests_task(void *pvParameters);
void autoconnect_task(void *pvParameters);
void send_status_info_request_task(void *pvParameters);
void send_general_request_task(void *pvParameters);
void successfull_connected_tcp_handler_callback(void *arg);
void successfull_disconnected_tcp_handler_callback();
void tcp_connection_error_handler_callback(void *arg, signed int err);
void tcp_response_received_handler_callback(void *arg, char *pdata, unsigned short len);
void tcp_request_successfully_sent_handler_callback();
void tcp_request_successfully_written_into_buffer_handler_callback();
void upgrade_firmware();
void pins_interrupt_handler();
void stop_ignoring_alarms_timer_callback();
void stop_ignoring_false_alarms_timer_callback();
void recheck_false_alarm_callback();
void disconnect_connection_task(void *pvParameters);
static void schedule_sending_status_info();
bool check_to_continue();
static void start_both_leds_blinking();
static void blink_both_leds();
static void stop_both_leds_blinking();

#endif

