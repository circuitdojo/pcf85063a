/*
 * Copyright (c) 2022 Circuit Dojo LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/sys/util.h>

#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/counter.h>

#include <zephyr/sys/timeutil.h>

#include <drivers/counter/pcf85063a.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(pcf85063a);

#define DT_DRV_COMPAT nxp_pcf85063a

/*
 * Function from https://stackoverflow.com/questions/19377396/c-get-day-of-year-from-date
 */
static int yisleap(int year)
{
	return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

/*
 * Function from https://stackoverflow.com/questions/19377396/c-get-day-of-year-from-date
 */
static int get_yday(int mon, int day, int year)
{
	static const int days[2][12] = {
		{0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334},
		{0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335}};
	int leap = yisleap(year);

	/* Minus 1 since gmtime starts from 0 */
	return days[leap][mon] + day - 1;
}

int pcf85063a_set_time(const struct device *dev, const struct tm *time)
{

	int ret = 0;

	// Get the data pointer
	struct pcf85063a_data *data = dev->data;

	/* Set seconds */
	uint8_t raw_time[7] = {0};
	raw_time[0] = PCF85063A_SECONDS_MASK & (((time->tm_sec / 10) << PCF85063A_BCD_UPPER_SHIFT) + (time->tm_sec % 10));

	/* Set minutes */
	raw_time[1] = PCF85063A_MINUTES_MASK & (((time->tm_min / 10) << PCF85063A_BCD_UPPER_SHIFT) + (time->tm_min % 10));

	/* Set hours */
	raw_time[2] = PCF85063A_HOURS_MASK & (((time->tm_hour / 10) << PCF85063A_BCD_UPPER_SHIFT) + (time->tm_hour % 10));

	/* Set days */
	raw_time[3] = PCF85063A_DAYS_MASK & (((time->tm_mday / 10) << PCF85063A_BCD_UPPER_SHIFT) + (time->tm_mday % 10));

	/* Set weekdays */
	raw_time[4] = PCF85063A_WEEKDAYS_MASK & time->tm_wday;

	/*Set month */
	raw_time[5] = PCF85063A_MONTHS_MASK & (((time->tm_mon / 10) << PCF85063A_BCD_UPPER_SHIFT) + (time->tm_mon % 10));

	/* Set year */
	uint8_t year = time->tm_year % 100;
	raw_time[6] = ((year / 10) << PCF85063A_BCD_UPPER_SHIFT) + (year % 10);

	/* Write to device */
	ret = i2c_burst_write_dt(&data->i2c, PCF85063A_SECONDS,
						  raw_time,
						  sizeof(raw_time));
	if (ret)
	{
		LOG_ERR("Unable to set time. Err: %i", ret);
		return ret;
	}

	return 0;
}

int pcf85063a_get_time(const struct device *dev, struct tm *time)
{
	int ret = 0;
	uint8_t raw_time[7] = {0};

	// Get the data pointer
	struct pcf85063a_data *data = dev->data;

	ret = i2c_burst_read_dt(&data->i2c, PCF85063A_SECONDS,
						 raw_time,
						 sizeof(raw_time));
	if (ret)
	{
		LOG_ERR("Unable to get time. Err: %i", ret);
		return ret;
	}

	/* Make sure the time is set properly.. */
	if (raw_time[0] & PCF85063A_SECONDS_OS)
	{
		LOG_WRN("Clock integrity error.");
		return -EIO;
	}

	/* Get seconds */
	time->tm_sec = (raw_time[0] & PCF85063A_BCD_LOWER_MASK) + (((raw_time[0] & PCF85063A_BCD_UPPER_MASK_SEC) >> PCF85063A_BCD_UPPER_SHIFT) * 10);

	/* Get minutes */
	time->tm_min = (raw_time[1] & PCF85063A_BCD_LOWER_MASK) + (((raw_time[1] & PCF85063A_BCD_UPPER_MASK) >> PCF85063A_BCD_UPPER_SHIFT) * 10);

	/* Get hours */
	time->tm_hour = (raw_time[2] & PCF85063A_BCD_LOWER_MASK) + (((raw_time[2] & PCF85063A_BCD_UPPER_MASK) >> PCF85063A_BCD_UPPER_SHIFT) * 10);

	/* Get days */
	time->tm_mday = (raw_time[3] & PCF85063A_BCD_LOWER_MASK) + (((raw_time[3] & PCF85063A_BCD_UPPER_MASK) >> PCF85063A_BCD_UPPER_SHIFT) * 10);

	/* Get weekdays */
	time->tm_wday = (raw_time[4] & PCF85063A_BCD_LOWER_MASK) + (((raw_time[4] & PCF85063A_BCD_UPPER_MASK) >> PCF85063A_BCD_UPPER_SHIFT) * 10);

	/* Get month */
	time->tm_mon = (raw_time[5] & PCF85063A_BCD_LOWER_MASK) + (((raw_time[5] & PCF85063A_BCD_UPPER_MASK) >> PCF85063A_BCD_UPPER_SHIFT) * 10);

	/* Get year with offset of 100 since we're in 2000+ */
	time->tm_year = (raw_time[6] & PCF85063A_BCD_LOWER_MASK) + (((raw_time[6] & PCF85063A_BCD_UPPER_MASK) >> PCF85063A_BCD_UPPER_SHIFT) * 10) + 100;

	/* Get day number in year */
	time->tm_yday = get_yday(time->tm_mon, time->tm_mday, time->tm_year + 1900);

	/* DST not used  */
	time->tm_isdst = 0;

	return 0;
}

static int pcf85063a_start(const struct device *dev)
{

	// Get the data pointer
	struct pcf85063a_data *data = dev->data;

	// Turn it back on (active low)
	uint8_t reg = 0;
	uint8_t mask = PCF85063A_CTRL1_STOP;

	// Write back the updated register value
	int ret = i2c_reg_update_byte_dt(&data->i2c, PCF85063A_CTRL1, mask, reg);
	if (ret)
	{
		LOG_ERR("Unable to stop RTC. (err %i)", ret);
		return ret;
	}

	return 0;
}

static int pcf85063a_stop(const struct device *dev)
{

	// Get the data pointer
	struct pcf85063a_data *data = dev->data;

	// Turn it off
	uint8_t reg = PCF85063A_CTRL1_STOP;
	uint8_t mask = PCF85063A_CTRL1_STOP;

	// Write back the updated register value
	int ret = i2c_reg_update_byte_dt(&data->i2c, PCF85063A_CTRL1, mask, reg);
	if (ret)
	{
		LOG_ERR("Unable to stop RTC. (err %i)", ret);
		return ret;
	}

	return 0;
}

static int pcf85063a_get_value(const struct device *dev, uint32_t *ticks)
{
	return 0;
}

static int pcf85063a_set_alarm(
	const struct device *dev, uint8_t chan_id, const struct counter_alarm_cfg *alarm_cfg)
{
	ARG_UNUSED(chan_id);

	uint8_t ticks = (uint8_t)alarm_cfg->ticks;

	// Get the data pointer
	struct pcf85063a_data *data = dev->data;

	// Ret val for error checking
	int ret;

	// Clear any flags in CTRL2
	uint8_t reg = 0;
	uint8_t mask = PCF85063A_CTRL2_TF;

	ret = i2c_reg_update_byte_dt(&data->i2c, PCF85063A_CTRL2, mask, reg);
	if (ret)
	{
		LOG_ERR("Unable to set RTC alarm. (err %i)", ret);
		return ret;
	}

	// Write the tick count. Ticks are 1 sec
	ret = i2c_reg_write_byte_dt(&data->i2c, PCF85063A_TIMER_VALUE, ticks);
	if (ret)
	{
		LOG_ERR("Unable to set RTC timer value. (err %i)", ret);
		return ret;
	}

	// Set to 1 second mode
	reg = (PCF85063A_TIMER_MODE_FREQ_1 << PCF85063A_TIMER_MODE_FREQ_SHIFT) | PCF85063A_TIMER_MODE_EN | PCF85063A_TIMER_MODE_INT_EN;
	mask = PCF85063A_TIMER_MODE_FREQ_MASK | PCF85063A_TIMER_MODE_EN | PCF85063A_TIMER_MODE_INT_EN;

	LOG_INF("mode 0x%x", reg);

	// Write back the updated register value
	ret = i2c_reg_update_byte_dt(&data->i2c, PCF85063A_TIMER_MODE, mask, reg);
	if (ret)
	{
		LOG_ERR("Unable to set RTC alarm. (err %i)", ret);
		return ret;
	}

	return 0;
}

static int pcf85063a_cancel_alarm(const struct device *dev, uint8_t chan_id)
{

	// Get the data pointer
	struct pcf85063a_data *data = dev->data;

	// Ret val for error checking
	int ret;

	// Clear any flags in CTRL2
	uint8_t reg = 0;
	uint8_t mask = PCF85063A_CTRL2_TF;

	ret = i2c_reg_update_byte_dt(&data->i2c, PCF85063A_CTRL2, mask, reg);
	if (ret)
	{
		LOG_ERR("Unable to set RTC alarm. (err %i)", ret);
		return ret;
	}

	// Turn off all itnerrupts/timer mode
	reg = 0;
	mask = PCF85063A_TIMER_MODE_EN | PCF85063A_TIMER_MODE_INT_EN | PCF85063A_TIMER_MODE_INT_TI_TP;

	LOG_INF("mode 0x%x", reg);

	// Write back the updated register value
	ret = i2c_reg_update_byte_dt(&data->i2c, PCF85063A_TIMER_MODE, mask, reg);
	if (ret)
	{
		LOG_ERR("Unable to cancel RTC alarm. (err %i)", ret);
		return ret;
	}

	return 0;
}

static int pcf85063a_set_top_value(const struct device *dev, const struct counter_top_cfg *cfg)
{
	return 0;
}

static uint32_t pcf85063a_get_pending_int(const struct device *dev)
{

	// Get the data pointer
	struct pcf85063a_data *data = dev->data;

	// Start with 0
	uint8_t reg = 0;

	// Write back the updated register value
	int ret = i2c_reg_read_byte_dt(&data->i2c, PCF85063A_CTRL2, &reg);
	if (ret)
	{
		LOG_ERR("Unable to get RTC CTRL2 reg. (err %i)", ret);
		return ret;
	}

	// Return 1 if interrupt. 0 if no flag.
	return (reg & PCF85063A_CTRL2_TF) ? 1U : 0U;
}

static uint32_t pcf85063a_get_top_value(const struct device *dev)
{
	return 0;
}

static const struct counter_driver_api pcf85063a_api = {
	.start = pcf85063a_start,
	.stop = pcf85063a_stop,
	.get_value = pcf85063a_get_value,
	.set_alarm = pcf85063a_set_alarm,
	.cancel_alarm = pcf85063a_cancel_alarm,
	.set_top_value = pcf85063a_set_top_value,
	.get_pending_int = pcf85063a_get_pending_int,
	.get_top_value = pcf85063a_get_top_value,
};

int pcf85063a_init(const struct device *dev)
{

	/* Get the i2c device binding*/
	struct pcf85063a_data *data = dev->data;
	/* Set I2C Device. */
	if (!device_is_ready(data->i2c.bus))
	{
		LOG_ERR("Failed to get pointer to %s device!", data->i2c.bus->name);
		return -EINVAL;
	}

	/* Check if it's alive. */
	uint8_t reg;
	int ret = i2c_reg_read_byte_dt(&data->i2c, PCF85063A_CTRL1, &reg);
	if (ret)
	{
		LOG_ERR("Failed to read from PCF85063A! (err %i)", ret);
		return -EIO;
	}

	LOG_INF("%s is initialized!", dev->name);

	return 0;
}

/* Main instantiation matcro */
#define PCF85063A_DEFINE(inst)							\
	static struct pcf85063a_data pcf85063a_data_##inst = {			\
		.i2c = I2C_DT_SPEC_INST_GET(inst),				\
	};									\
	static const struct counter_config_info pcf85063_cfg_info_##inst = {	\
		.max_top_value = 0xff,						\
		.freq = 1,							\
		.channels = 1,							\
	};									\
	DEVICE_DT_INST_DEFINE(inst,						\
						  pcf85063a_init, NULL,                              \
						  &pcf85063a_data_##inst, &pcf85063_cfg_info_##inst, \
						  POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY,             \
						  &pcf85063a_api);

/* Create the struct device for every status "okay"*/
DT_INST_FOREACH_STATUS_OKAY(PCF85063A_DEFINE)
