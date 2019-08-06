#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "driver/i2c.h"
#include "esp_err.h"
#include "global_definitions.h"

#define ACK_CHECK_EN    0x1       // I2C master will check ACK from slave
#define ACK_VAL         0x0 // I2C ACK value
#define NACK_VAL        0x1 // I2C NACK value
#define LAST_NACK_VAL   0x2 // I2C last_nack value

#define SHT21_ADDRESS      (unsigned char) 0x40
#define SHT21_ADDRESS_READ (unsigned char) ((SHT21_ADDRESS << 1) | 0x1)

#define SHT21_CRC8_POLYNOMIAL 0x13100   //CRC-8 polynomial for 16bit value -> x^8 + x^5 + x^4 + 1

#define SHT21_CRC_ERROR                         -100.0F
#define SHT21_NOT_TEMPERATURE_MEASUREMENT_ERROR -200.0F
#define SHT21_NOT_HUMIDITY_MEASUREMENT_ERROR    -300.0F

typedef enum {
   TRIGGER_T_MEASUREMENT = 0xF3,
   TRIGGER_RH_MEASUREMENT = 0xF5
} SHT21_Commands;

static float sht21_calculate_humidity(unsigned short data, unsigned char checksum);
esp_err_t sht21_get_temperature(float *temperature);
esp_err_t sht21_get_humidity(float *humidity);
