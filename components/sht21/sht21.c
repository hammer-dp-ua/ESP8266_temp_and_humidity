#include "include/sht21.h"

static esp_err_t i2c_master_sht21_write(unsigned char command) {
   int ret;
   i2c_cmd_handle_t cmd = i2c_cmd_link_create();
   i2c_master_start(cmd);
   i2c_master_write_byte(cmd, SHT21_ADDRESS << 1 | I2C_MASTER_WRITE, ACK_CHECK_EN);
   i2c_master_write_byte(cmd, command, ACK_CHECK_EN);
   //i2c_master_write(cmd, data, data_len, ACK_CHECK_EN);
   i2c_master_stop(cmd);
   ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, 100 / portTICK_RATE_MS);
   i2c_cmd_link_delete(cmd);

   return ret;
}

static esp_err_t i2c_master_sht21_read(unsigned char *data, size_t data_len) {
   i2c_cmd_handle_t cmd = i2c_cmd_link_create();
   i2c_master_start(cmd);
   i2c_master_write_byte(cmd, SHT21_ADDRESS << 1 | I2C_MASTER_READ, ACK_CHECK_EN);
   i2c_master_read(cmd, data, data_len, LAST_NACK_VAL);
   i2c_master_stop(cmd);
   int ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, 500 / portTICK_RATE_MS);
   i2c_cmd_link_delete(cmd);
   return ret;
}

static esp_err_t i2c_master_sht21_write_and_read(unsigned char command,
                                                 unsigned char *read_data,
                                                 size_t data_len,
                                                 TickType_t measurement_time) {
   if (data_len <= 0) {
      return ESP_ERR_INVALID_STATE;
   }

   i2c_cmd_handle_t cmd = i2c_cmd_link_create();
   esp_err_t ret = i2c_master_start(cmd);
   if (ret == ESP_ERR_INVALID_ARG) {
      return ret;
   }
   ret = i2c_master_write_byte(cmd, SHT21_ADDRESS << 1 | I2C_MASTER_WRITE, ACK_CHECK_EN);
   if (ret == ESP_ERR_INVALID_ARG) {
      i2c_cmd_link_delete(cmd);
      return ret;
   }
   ret = i2c_master_write_byte(cmd, command, ACK_CHECK_EN);
   if (ret == ESP_ERR_INVALID_ARG) {
      i2c_cmd_link_delete(cmd);
      return ret;
   }
   ret = i2c_master_start(cmd); // Start condition is generated before read every time. There could be a few of re-reads
   if (ret == ESP_ERR_INVALID_ARG) {
      i2c_cmd_link_delete(cmd);
      return ret;
   }
   ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, 500 / portTICK_RATE_MS);
   if (ret != ESP_OK) {
      i2c_cmd_link_delete(cmd);
      return ret;
   }
   i2c_cmd_link_delete(cmd);

   vTaskDelay(measurement_time);
   cmd = i2c_cmd_link_create();
   i2c_master_start(cmd);
   i2c_master_write_byte(cmd, SHT21_ADDRESS << 1 | I2C_MASTER_READ, ACK_CHECK_EN);

   for (unsigned char i = 0; i < (data_len - 1); i++) {
      i2c_master_read_byte(cmd, read_data++, ACK_VAL);
   }
   i2c_master_read_byte(cmd, read_data, NACK_VAL);

   i2c_master_stop(cmd);
   i2c_master_cmd_begin(I2C_NUM_0, cmd, 500 / portTICK_RATE_MS);

   i2c_cmd_link_delete(cmd);
   return ret;
}

static unsigned char sht21_calculate_crc(unsigned short data) {
  for (unsigned char bit = 0; bit < 16; bit++) {
    if (data & 0x8000) {
       data = (data << 1) ^ SHT21_CRC8_POLYNOMIAL;
    } else {
       data <<= 1;
    }
  }
  return (unsigned char) (data >>= 8);
}

static float sht21_calculate_temperature(unsigned short data, unsigned char checksum) {
   if (checksum != sht21_calculate_crc(data)) {
      return SHT21_CRC_ERROR;
   }

   if (data == 0) {
      return 0.0F;
   } else if (data & 0x2) {
      #ifdef ALLOW_USE_PRINTF
      printf("\nI2C ERROR. Humidity measurement instead of Temperature\n");
      #endif

      return SHT21_NOT_TEMPERATURE_MEASUREMENT_ERROR;
   }

   float temperature = (float) (data & ((unsigned short) 0x3FFFC));
   return 175.72 * temperature / 0xFFFF - 46.85;
}

static float sht21_calculate_humidity(unsigned short data, unsigned char checksum) {
   if (checksum != sht21_calculate_crc(data)) {
      return SHT21_CRC_ERROR;
   }

   if (data == 0) {
      return 0.0F;
   } else if (!(data & 0x2)) {
      #ifdef ALLOW_USE_PRINTF
      printf("\nI2C ERROR. Temperature measurement instead of Humidity\n");
      #endif

      return SHT21_NOT_HUMIDITY_MEASUREMENT_ERROR;
   }

   float humidity = (float) (data & ((unsigned short) 0x3FFFC));
   return 125 * humidity / 0xFFFF - 6;
}

esp_err_t sht21_get_temperature(float *temperature) {
   unsigned char data[3];
   esp_err_t i2c_result_status = i2c_master_sht21_write_and_read(TRIGGER_T_MEASUREMENT, data, 3, 100 / portTICK_RATE_MS);

   if (i2c_result_status == ESP_OK) {
      unsigned short raw_data = (data[0] << 8) | data[1];

      #ifdef ALLOW_USE_PRINTF
      printf("\nTemperature raw: 0x%X\n", raw_data);
      #endif

      *temperature = sht21_calculate_temperature(raw_data, data[2]);
   } else {
      #ifdef ALLOW_USE_PRINTF
      printf("\nI2C ERROR. Result status: 0x%X\n", i2c_result_status);
      #endif
   }
   return i2c_result_status;
}

esp_err_t sht21_get_humidity(float *humidity) {
   unsigned char data[3];
   esp_err_t i2c_result_status = i2c_master_sht21_write_and_read(TRIGGER_RH_MEASUREMENT, data, 3, 50 / portTICK_RATE_MS);

   if (i2c_result_status == ESP_OK) {
      unsigned short raw_data = (data[0] << 8) | data[1];

      #ifdef ALLOW_USE_PRINTF
      printf("\nHumidity raw: 0x%X\n", raw_data);
      #endif

      *humidity = sht21_calculate_humidity(raw_data, data[2]);
   } else {
      #ifdef ALLOW_USE_PRINTF
      printf("\nI2C ERROR. Result status: 0x%x\n", i2c_result_status);
      #endif
   }
   return i2c_result_status;
}
