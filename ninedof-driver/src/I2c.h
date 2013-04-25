/* 
* Funkcje obsługujące magistralę I2C
*/

#ifndef I2C_H_
#define I2C_H_

int i2c_open(const char *i2c_bus);
ssize_t i2c_read(int file, __u8 slave_address, __u8 register_address, ssize_t bytes, __u8 *buf);
ssize_t i2c_write(int file, __u8 slave_address, __u8 register_address, ssize_t bytes, __u8 *buf);
int i2c_close(int file);

#endif
