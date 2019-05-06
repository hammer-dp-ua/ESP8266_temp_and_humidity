#include <stdio.h>
#include "esp_system.h"

#include "portmacro.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"

#include "driver/uart.h"
#include "driver/gpio.h"

#include "esp_wifi.h"
#include "string.h"
#include "utils.h"
#include "lwip/sys.h"
#include "lwip/inet.h"
#include "sys/socket.h"
#include "event_groups.h"
#include "global_definitions.h"
#include "malloc_logger.h"

#ifndef MAIN_HEADER
#define MAIN_HEADER

#define AP_CONNECTION_STATUS_LED_PIN         GPIO_NUM_5
#define SERVER_AVAILABILITY_STATUS_LED_PIN   GPIO_NUM_4

#define LONG_POLLING_REQUEST_ERROR_OCCURRED_FLAG   1
#define SERVER_IS_AVAILABLE_FLAG                   2
#define UPDATE_FIRMWARE_FLAG                       (1 << 1)
#define REQUEST_ERROR_OCCURRED_FLAG                8
#define IGNORE_ALARMS_FLAG                         16
#define IGNORE_FALSE_ALARMS_FLAG                   32
#define IGNORE_MOTION_DETECTOR_FLAG                64
#define MANUALLY_IGNORE_ALARMS_FLAG                128
#define FIRST_STATUS_INFO_SENT_FLAG                (1 << 0) //( 1 << 4 )

#define REQUEST_IDLE_TIME_ON_ERROR        (10000 / portTICK_RATE_MS) // 10 sec
#define REQUEST_MAX_DURATION_TIME         (10000 / portTICK_RATE_MS) // 10 sec
#define STATUS_REQUESTS_SEND_INTERVAL_MS  (30 * 1000)
#define STATUS_REQUESTS_SEND_INTERVAL     (STATUS_REQUESTS_SEND_INTERVAL_MS / portTICK_RATE_MS) // 30 sec

#define ERRORS_CHECKER_INTERVAL_MS        (30 * 1000)

#define IGNORE_MOTION_DETECTOR_TIMEOUT_AFTER_TURN_ON_SEC 60

#define IGNORE_ALARMS_TIMEOUT_SEC               60
#define IGNORE_FALSE_ALARMS_TIMEOUT_SEC         30
#define RECHECK_FALSE_ALARMS_STATE_TIMEOUT_SEC  5

#define MILLISECONDS_COUNTER_DIVIDER 10

#if RECHECK_FALSE_ALARMS_STATE_TIMEOUT_SEC >= IGNORE_FALSE_ALARMS_TIMEOUT_SEC
   #error "Check constants values"
#endif

#define MAX_REPETITIVE_ALLOWED_ERRORS_AMOUNT 15

#define UART_BUF_SIZE (UART_FIFO_LEN + 1)
#define UART_RD_BUF_SIZE (UART_BUF_SIZE)

#define SYSTEM_RESTART_REASON_TYPE_RTC_ADDRESS  64
#define CONNECTION_ERROR_CODE_RTC_ADDRESS       SYSTEM_RESTART_REASON_TYPE_RTC_ADDRESS + 1

typedef enum {
   ALARM,
   FALSE_ALARM
} GeneralRequestType;

typedef enum {
   ACCESS_POINT_CONNECTION_ERROR = 1,
   REQUEST_CONNECTION_ERROR,
   SOFTWARE_UPGRADE
} SYSTEM_RESTART_REASON_TYPE;

const char RESPONSE_SERVER_SENT_OK[] = "\"statusCode\":\"OK\"";
char STATUS_INFO_POST_REQUEST[] =
      "POST /server/esp8266/statusInfo HTTP/1.1\r\n"
      "Content-Length: <1>\r\n"
      "Host: <2>\r\n"
      "User-Agent: ESP8266\r\n"
      "Content-Type: application/json\r\n"
      "Connection: close\r\n"
      "Accept: application/json\r\n\r\n"
      "<3>\r\n";
char STATUS_INFO_REQUEST_PAYLOAD_TEMPLATE[] =
      "{\"gain\":\"<1>\","
      "\"deviceName\":\"<2>\","
      "\"errors\":<3>,"
      "\"pendingConnectionErrors\":<4>,"
      "\"uptime\":<5>,"
      "\"buildTimestamp\":\"<6>\","
      "\"freeHeapSpace\":<7>,"
      "\"resetReason\":\"<8>\","
      "\"systemRestartReason\":\"<9>\"}";
const char ALARM_GET_REQUEST[] =
      "GET /server/esp8266/alarm?alarmSource=<1> HTTP/1.1\r\n"
      "Host: <2>\r\n"
      "User-Agent: ESP8266\r\n"
      "Connection: close\r\n"
      "Accept: application/json\r\n\r\n";
const char FALSE_ALARM_GET_REQUEST[] =
      "GET /server/esp8266/falseAlarm?alarmSource=<1> HTTP/1.1\r\n"
      "Host: <2>\r\n"
      "User-Agent: ESP8266\r\n"
      "Connection: close\r\n"
      "Accept: application/json\r\n\r\n";
const char UPDATE_FIRMWARE[] = "\"updateFirmware\":true";
const char MANUALLY_IGNORE_ALARMS[] = "\"ignoreAlarms\":true";
const char FIRMWARE_UPDATE_GET_REQUEST[] =
      "GET /esp8266_fota/<1> HTTP/1.1\r\n"
      "Host: <2>\r\n"
      "User-Agent: ESP8266\r\n"
      "Connection: close\r\n\r\n";
const char MW_LED[] = "MW_LED";
const char MOTION_SENSOR[] = "MOTION_SENSOR";

void pins_config();
static void uart_config();
void scan_access_point_task(void *pvParameters);
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
void schedule_sending_status_info();
bool check_to_continue();

#endif

