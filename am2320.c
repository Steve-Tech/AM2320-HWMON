// SPDX-License-Identifier: GPL-2.0-only

/*
 * am2320.c - Linux hwmon driver for AM232X Temperature and Humidity sensors
 * Copyright (C) 2025 Stephen Horvath
 * 
 * Based on aht10.c
 * Copyright (C) 2020 Johannes Cornelis Draaijer
 */

#include <linux/delay.h>
#include <linux/hwmon.h>
#include <linux/i2c.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/unaligned.h>

#define AM2320_MEAS_SIZE	4
#define AM2320_FRAME_SIZE	AM2320_MEAS_SIZE + 4

/*
 * Poll intervals (in milliseconds)
 */
#define AM2320_DEFAULT_MIN_POLL_INTERVAL	2000
#define AM2320_MIN_POLL_INTERVAL		2000

/*
 * I2C command delays (in microseconds)
 */
#define AM2320_MEAS_DELAY	1500

/*
 * Command bytes
 */
#define AM2320_FUNC_READ	0x03
#define AM2320_FUNC_WRITE	0x10

/**
 *   struct am2320_data - All the data required to operate an AM2320 chip
 *   @client: The i2c client associated with the AM2320
 *   @lock: A mutex that is used to prevent parallel access to the i2c client
 *   @min_poll_interval: The minimum poll interval
 *                       The datasheet specifies a minimum sample rate of
 * 			 2000 ms. Default value is 2000 ms
 *   @previous_poll_time: The previous time that the AM2320 was polled
 *   @temperature: The latest temperature value received from the AM2320
 *   @humidity: The latest humidity value received from the AM2320
 */

struct am2320_data {
	struct i2c_client *client;
	/*
	 * Prevent simultaneous access to the i2c
	 * client and previous_poll_time
	 */
	struct mutex lock;
	ktime_t min_poll_interval;
	ktime_t previous_poll_time;
	int temperature;
	int humidity;
};

/*
 * am2320_polltime_expired() - check if the minimum poll interval has expired
 * @data: the data containing the time to compare
 * Return: 1 if the minimum poll interval has expired, 0 if not
 */
static int am2320_polltime_expired(struct am2320_data *data)
{
	ktime_t current_time = ktime_get_boottime();
	ktime_t difference = ktime_sub(current_time, data->previous_poll_time);

	return ktime_after(difference, data->min_poll_interval);
}

/*
 * am2320_crc16() - check crc of the sensor's measurements
 * @raw_data: data frame received from sensor (including crc as the last byte)
 * @count: size of the data frame
 * Return: 0 if successful, 1 if not
 */
static int am2320_crc16(u8 *raw_data, int count)
{
	u16 data_crc = raw_data[count - 2] << 8 | raw_data[count - 1];
	count -= 2; // Exclude CRC bytes from the count

	u16 crc = 0xFFFF;
	while (count--) {
		crc ^= *raw_data++;
		for (int i = 0; i < 8; i++) {
			if (crc & 0x01) {
				crc >>= 1;
				crc ^= 0xA001;
			} else
				crc >>= 1;
		}
	}

	return crc == data_crc;
}

/*
 * am2320_read_values() - read and parse the raw data from the AM2320
 * @data: the struct am2320_data to use for the lock
 * Return: 0 if successful, 1 if not
 */
static int am2320_read_values(struct am2320_data *data)
{
	const u8 cmd_wake[] = { 0x00 };
	const u8 cmd_meas[] = { AM2320_FUNC_READ, 0x00, AM2320_MEAS_SIZE };
	int temp, humid;
	int res;
	u8 raw_data[AM2320_FRAME_SIZE];
	struct i2c_client *client = data->client;

	/* Check if the poll interval has expired. */
	mutex_lock(&data->lock);
	if (!am2320_polltime_expired(data)) {
		mutex_unlock(&data->lock);
		return 0;
	}

	/* 
	 * Sensor goes to sleep to reduce self-heating.
	 * Wake it up by sending a dummy command.
	 * This may return an error, that's fine.
	 */
	i2c_master_send(client, cmd_wake, sizeof(cmd_wake));

	/* Send the measurement command */
	res = i2c_master_send(client, cmd_meas, sizeof(cmd_meas));
	if (res < 0) {
		mutex_unlock(&data->lock);
		return res;
	}

	/* Delay at least 1.5ms */
	usleep_range(AM2320_MEAS_DELAY, AM2320_MEAS_DELAY * 2);

	/* Read back the data */
	res = i2c_master_recv(client, raw_data, AM2320_FRAME_SIZE);
	if (res != AM2320_FRAME_SIZE) {
		mutex_unlock(&data->lock);
		if (res >= 0)
			return -ENODATA;
		return res;
	}

	/* Check if an error occurred */
	if (raw_data[0] != AM2320_FUNC_READ ||
	    raw_data[1] != AM2320_MEAS_SIZE) {
		mutex_unlock(&data->lock);
		return -EIO;
	}

	if (am2320_crc16(raw_data, AM2320_FRAME_SIZE)) {
		mutex_unlock(&data->lock);
		return -EIO;
	}

	/* Parse the data */
	humid = get_unaligned_be16(&raw_data[2]);
	temp  = get_unaligned_be16(&raw_data[4]);

	/* Bit 15 indicates a negative temperature */
	if (temp & 0x8000)
		temp = -(temp & 0x7FFF);

	data->temperature = temp * 100;
	data->humidity = humid * 100;
	data->previous_poll_time = ktime_get_boottime();

	mutex_unlock(&data->lock);
	return 0;
}

/*
 * am2320_interval_write() - store the given minimum poll interval.
 * Return: 0 on success, -EINVAL if a value lower than the
 *         AM2320_MIN_POLL_INTERVAL is given
 */
static ssize_t am2320_interval_write(struct am2320_data *data, long val)
{
	if (val < AM2320_MIN_POLL_INTERVAL)
		return -EINVAL;
	data->min_poll_interval = ms_to_ktime(val);
	return 0;
}

/*
 * am2320_interval_read() - read the minimum poll interval in milliseconds
 */
static ssize_t am2320_interval_read(struct am2320_data *data, long *val)
{
	*val = ktime_to_ms(data->min_poll_interval);
	return 0;
}

/*
 * am2320_temperature1_read() - read the temperature in millidegrees
 */
static int am2320_temperature1_read(struct am2320_data *data, long *val)
{
	int res;

	res = am2320_read_values(data);
	if (res < 0)
		return res;

	*val = data->temperature;
	return 0;
}

/*
 * am2320_humidity1_read() - read the relative humidity in millipercent
 */
static int am2320_humidity1_read(struct am2320_data *data, long *val)
{
	int res;

	res = am2320_read_values(data);
	if (res < 0)
		return res;

	*val = data->humidity;
	return 0;
}

static umode_t am2320_hwmon_visible(const void *data,
				    enum hwmon_sensor_types type, u32 attr,
				    int channel)
{
	switch (type) {
	case hwmon_temp:
	case hwmon_humidity:
		return 0444;
	case hwmon_chip:
		return 0644;
	default:
		return 0;
	}
}

static int am2320_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
			     u32 attr, int channel, long *val)
{
	struct am2320_data *data = dev_get_drvdata(dev);

	switch (type) {
	case hwmon_temp:
		return am2320_temperature1_read(data, val);
	case hwmon_humidity:
		return am2320_humidity1_read(data, val);
	case hwmon_chip:
		return am2320_interval_read(data, val);
	default:
		return -EOPNOTSUPP;
	}
}

static int am2320_hwmon_write(struct device *dev, enum hwmon_sensor_types type,
			      u32 attr, int channel, long val)
{
	struct am2320_data *data = dev_get_drvdata(dev);

	switch (type) {
	case hwmon_chip:
		return am2320_interval_write(data, val);
	default:
		return -EOPNOTSUPP;
	}
}

static const struct hwmon_channel_info *const am2320_info[] = {
	HWMON_CHANNEL_INFO(chip, HWMON_C_UPDATE_INTERVAL),
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT),
	HWMON_CHANNEL_INFO(humidity, HWMON_H_INPUT),
	NULL,
};

static const struct hwmon_ops am2320_hwmon_ops = {
	.is_visible = am2320_hwmon_visible,
	.read = am2320_hwmon_read,
	.write = am2320_hwmon_write,
};

static const struct hwmon_chip_info am2320_chip_info = {
	.ops = &am2320_hwmon_ops,
	.info = am2320_info,
};

static int am2320_probe(struct i2c_client *client)
{
	struct device *device = &client->dev;
	struct device *hwmon_dev;
	struct am2320_data *data;
	int res;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENOENT;

	data = devm_kzalloc(device, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->min_poll_interval = ms_to_ktime(AM2320_DEFAULT_MIN_POLL_INTERVAL);
	data->client = client;

	mutex_init(&data->lock);

	res = am2320_read_values(data);
	if (res < 0)
		return res;

	hwmon_dev = devm_hwmon_device_register_with_info(
		device, client->name, data, &am2320_chip_info, NULL);

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id am2320_id[] = {
	{ "am2320" },
	{ "am2321" },
	{ "am2322" },
	{ },
};
MODULE_DEVICE_TABLE(i2c, am2320_id);

static const struct of_device_id am2320_of_match[] = {
	{ .compatible = "aosong,am2320" },
	{ .compatible = "aosong,am2321" },
	{ .compatible = "aosong,am2322" },
	{ },
};
MODULE_DEVICE_TABLE(of, am2320_of_match);

static struct i2c_driver am2320_driver = {
	.driver = {
		.name = "am2320",
		.of_match_table = am2320_of_match,
	},
	.probe      = am2320_probe,
	.id_table   = am2320_id,
};

module_i2c_driver(am2320_driver);

MODULE_AUTHOR("Stephen Horvath <s.horvath@outlook.com.au>");
MODULE_DESCRIPTION("AM2320 Temperature and Humidity sensor driver");
MODULE_LICENSE("GPL v2");
