#ifndef HW_CONFIG_H
#define HW_CONFIG_H

// Dashboard HW configuration for NES
#define SD_SCK  41
#define SD_MISO 40
#define SD_MOSI 39
#define SD_CS   38

// Audio pins (if you have an external I2S DAC, otherwise we'll disable sound)
#define I2S_BCK 1
#define I2S_WS  2
#define I2S_DO  3

#define ENABLE_SOUND 0 // Disable sound for now to avoid crashes if pins are wrong

#endif
