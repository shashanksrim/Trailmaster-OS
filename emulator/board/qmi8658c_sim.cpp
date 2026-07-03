// L1: QMI8658 IMU-driver substitute. Implements the same function API
// declared in the real (unmodified) qmi8658c.h, returning a fixed "device
// lying flat" reading instead of polling the real I2C IMU chip.
#include "qmi8658c.h"
#include <cstring>

unsigned char qmi8658_write_reg(unsigned char, unsigned char) { return 0; }
unsigned char qmi8658_read_reg(unsigned char, unsigned char*, unsigned short) { return 0; }
unsigned char qmi8658_init(void) { return 1; }
void qmi8658_config_reg(unsigned char) {}
void qmi8658_enableSensors(unsigned char) {}
unsigned char qmi8658_readStatusInt(void) { return QMI8658_STATUS1_CMD_DONE; }
unsigned char qmi8658_readStatus0(void) { return 0; }
unsigned char qmi8658_readStatus1(void) { return QMI8658_STATUS1_CMD_DONE; }
float qmi8658_readTemp(void) { return 25.0f; }
void qmi8658_read_timestamp(unsigned int *tim_count) { if (tim_count) *tim_count = 0; }

// Fixed "lying flat, stationary" reading: 1g straight down on Z, no rotation.
void qmi8658_read_xyz(float acc[3], float gyro[3]) {
    acc[0] = 0.0f; acc[1] = 0.0f; acc[2] = 1.0f;
    gyro[0] = 0.0f; gyro[1] = 0.0f; gyro[2] = 0.0f;
}
void qmi8658_read_sensor_data(float acc[3], float gyro[3]) { qmi8658_read_xyz(acc, gyro); }

#if defined(QMI8658_USE_FIFO)
void qmi8658_config_fifo(unsigned char, enum qmi8658_FifoSize, enum qmi8658_FifoMode, enum qmi8658_Interrupt) {}
unsigned short qmi8658_read_fifo(unsigned char*) { return 0; }
#endif

void qmi8658_send_ctl9cmd(enum qmi8658_Ctrl9Command) {}
void qmi8658c_example(void*) {}
