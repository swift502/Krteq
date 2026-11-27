#define I2C_DRIVER I2CD0
#define I2C1_SDA_PIN GP0
#define I2C1_SCL_PIN GP1

#define IS31FL3733_I2C_ADDRESS_1 IS31FL3733_I2C_ADDRESS_GND_GND
#define IS31FL3733_SDB_PIN GP2
#define IS31FL3733_PWM_FREQUENCY IS31FL3733_PWM_FREQUENCY_26K7_HZ

#define OLED_TIMEOUT 0
#define OLED_BRIGHTNESS 0
#define OLED_PRE_CHARGE_PERIOD 0x11
#define OLED_VCOM_DETECT 0x00

#define RGB_TRIGGER_ON_KEYDOWN
#define RGB_MATRIX_MODE_NAME_ENABLE

// Persistent memory
// - Screen selection (uint8_t): 1 byte
// - Bongo cat hits (uint32_t): 4 bytes
// - OLED brightness, pre-charge and VCOM (uint8_t): 3 bytes
#define EECONFIG_KB_DATA_SIZE 8
