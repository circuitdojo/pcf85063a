/*
 * Copyright (c) 2022 Circuit Dojo LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <string.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/sys/util.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/sys/timeutil.h>
#include <zephyr/logging/log.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(pcf85063a);

#include <drivers/rtc/pcf85063a.h>

#define DT_DRV_COMPAT nxp_pcf85063a

struct pcf85063a_config
{
	const struct i2c_dt_spec i2c;
};

struct pcf85063a_data
{
};

int pcf85063a_set_time(const struct device *dev, const struct rtc_time *new_time)
{
	const struct pcf85063a_config *config = dev->config;
	int ret = 0;
	uint8_t raw_time[7] = {0};

	raw_time[0] = bin2bcd(new_time->tm_sec);
	raw_time[1] = bin2bcd(new_time->tm_min);
	raw_time[2] = bin2bcd(new_time->tm_hour);
	raw_time[3] = bin2bcd(new_time->tm_mday);
	raw_time[4] = new_time->tm_wday;
	raw_time[5] = bin2bcd(new_time->tm_mon);
	raw_time[6] = bin2bcd(new_time->tm_year % 100);

	ret = i2c_burst_write_dt(&config->i2c, PCF85063A_SECONDS, raw_time, sizeof(raw_time));
	if (ret)
	{
		LOG_ERR("Error when setting time: %i", ret);
		return ret;
	}

	return 0;
}

int pcf85063a_get_time(const struct device *dev, struct rtc_time *dest_time)
{
	const struct pcf85063a_config *config = dev->config;
	int ret = 0;
	uint8_t raw_time[7] = {0};

	ret = i2c_burst_read_dt(&config->i2c, PCF85063A_SECONDS, raw_time, sizeof(raw_time));
	if (ret)
	{
		LOG_ERR("Unable to get time. Err: %i", ret);
		return ret;
	}

	dest_time->tm_nsec = 0;
	dest_time->tm_sec = bcd2bin(raw_time[0] & PCF85063A_SECONDS_MASK);
	dest_time->tm_min = bcd2bin(raw_time[1] & PCF85063A_MINUTES_MASK);
	dest_time->tm_hour = bcd2bin(raw_time[2] & PCF85063A_HOURS_MASK);
	dest_time->tm_mday = bcd2bin(raw_time[3] & PCF85063A_DAYS_MASK);
	dest_time->tm_wday = raw_time[4] & PCF85063A_WEEKDAYS_MASK;
	dest_time->tm_mon = bcd2bin(raw_time[5] & PCF85063A_MONTHS_MASK);
	dest_time->tm_year = bcd2bin(raw_time[6]) + 100;
	dest_time->tm_yday = -1;
	dest_time->tm_isdst = -1;

	return 0;
}

#ifdef CONFIG_RTC_ALARM
static int pcf85063a_alarm_get_supported_fields(const struct device *dev, uint16_t id, uint16_t *mask)
{
	if (id != 0)
	{
		LOG_ERR("invalid ID %d", id);
		return -EINVAL;
	}

	*mask = PCF85063A_RTC_ALARM_TIME_MASK; // Define this mask appropriately
	return 0;
}

static int pcf85063a_alarm_set_time(const struct device *dev, uint16_t id, uint16_t mask, const struct rtc_time *timeptr)
{
	const struct pcf85063a_config *config = dev->config;
	uint8_t regs[4];
	int ret;

	if (id != 0)
	{
		LOG_ERR("invalid ID %d", id);
		return -EINVAL;
	}

	if ((mask & ~PCF85063A_RTC_ALARM_TIME_MASK) != 0)
	{
		LOG_ERR("invalid alarm field mask 0x%04x", mask);
		return -EINVAL;
	}

	if ((mask & RTC_ALARM_TIME_MASK_MINUTE) != 0)
	{
		regs[0] = bin2bcd(timeptr->tm_min) & PCF85063A_MINUTES_MASK;
	}
	else
	{
		regs[0] = BIT(7);
	}

	if ((mask & RTC_ALARM_TIME_MASK_HOUR) != 0)
	{
		regs[1] = bin2bcd(timeptr->tm_hour) & PCF85063A_HOURS_MASK;
	}
	else
	{
		regs[1] = BIT(7);
	}

	if ((mask & RTC_ALARM_TIME_MASK_MONTHDAY) != 0)
	{
		regs[2] = bin2bcd(timeptr->tm_mday) & PCF85063A_DAYS_MASK;
	}
	else
	{
		regs[2] = BIT(7);
	}

	if ((mask & RTC_ALARM_TIME_MASK_WEEKDAY) != 0)
	{
		regs[3] = bin2bcd(timeptr->tm_wday) & PCF85063A_WEEKDAYS_MASK;
	}
	else
	{
		regs[3] = BIT(7);
	}

	ret = i2c_burst_write_dt(&config->i2c, PCF85063A_ALARM_REGISTER, regs, sizeof(regs));
	if (ret)
	{
		LOG_ERR("Error when setting alarm: %i", ret);
		return ret;
	}

	return i2c_reg_write_byte_dt(&config->i2c, PCF85063A_CONTROL2_REGISTER, PCF85063A_CONTROL2_REGISTER_TIE_EN | PCF85063A_CONTROL2_REGISTER_AIE_EN);
}

static int pcf85063a_alarm_get_time(const struct device *dev, uint16_t id, uint16_t *mask, struct rtc_time *timeptr)
{
	const struct pcf85063a_config *config = dev->config;
	uint8_t regs[4];
	int err;

	if (id != 0)
	{
		LOG_ERR("invalid ID %d", id);
		return -EINVAL;
	}

	err = i2c_burst_read_dt(&config->i2c, PCF85063A_ALARM_REGISTER, regs, sizeof(regs));
	if (err)
	{
		LOG_ERR("Error when getting alarm time: %i", err);
		return err;
	}

	memset(timeptr, 0U, sizeof(*timeptr));
	*mask = 0U;

	if (regs[0] & BIT(7))
	{
		timeptr->tm_min = bcd2bin(regs[0] & GENMASK(6, 0));
		*mask |= RTC_ALARM_TIME_MASK_MINUTE;
	}

	if (regs[1] & BIT(7))
	{
		timeptr->tm_hour = bcd2bin(regs[1] & GENMASK(5, 0));
		*mask |= RTC_ALARM_TIME_MASK_HOUR;
	}

	if (regs[2] & BIT(7))
	{
		timeptr->tm_mday = bcd2bin(regs[2] & GENMASK(5, 0));
		*mask |= RTC_ALARM_TIME_MASK_MONTHDAY;
	}

	if (regs[3] & BIT(7))
	{
		timeptr->tm_wday = bcd2bin(regs[3] & GENMASK(2, 0));
		*mask |= RTC_ALARM_TIME_MASK_WEEKDAY;
	}

	return 0;
}

static int pcf85063a_alarm_is_pending(const struct device *dev, uint16_t id)
{
	const struct pcf85063a_config *config = dev->config;
	uint8_t reg;
	int err;

	if (id != 0)
	{
		LOG_ERR("invalid ID %d", id);
		return -EINVAL;
	}

	err = i2c_reg_read_byte_dt(&config->i2c, PCF85063A_CONTROL2_REGISTER, &reg);
	if (err)
	{
		LOG_ERR("Error when getting the control register 2: %i", err);
		return err;
	}

	if (reg & GENMASK(3, 2))
	{
		err = i2c_reg_write_byte_dt(&config->i2c, PCF85063A_CONTROL2_REGISTER, GENMASK(1, 0));
		if (err)
		{
			LOG_ERR("Error when clearing alarms: %d", err);
			return err;
		}
		return 1;
	}
	return 0;
}
#endif

static const struct rtc_driver_api pcf85063a_driver_api = {
	.set_time = pcf85063a_set_time,
	.get_time = pcf85063a_get_time,
#ifdef CONFIG_RTC_ALARM
	.alarm_get_supported_fields = pcf85063a_alarm_get_supported_fields,
	.alarm_set_time = pcf85063a_alarm_set_time,
	.alarm_get_time = pcf85063a_alarm_get_time,
	.alarm_is_pending = pcf85063a_alarm_is_pending,
#endif
};

int pcf85063a_init(const struct device *dev)
{
	const struct pcf85063a_config *config = dev->config;
	int ret;
	uint8_t reg;

	if (!device_is_ready(config->i2c.bus))
	{
		LOG_ERR("Failed to get pointer to %s device!", config->i2c.bus->name);
		return -EINVAL;
	}

	ret = i2c_reg_read_byte_dt(&config->i2c, PCF85063A_CTRL1, &reg);
	if (ret)
	{
		LOG_ERR("Failed to read from PCF85063A! (err %i)", ret);
		return -EIO;
	}

	LOG_INF("%s is initialized!", dev->name);

	return 0;
}

#define PCF85063A_INIT(inst)                                                \
	static const struct pcf85063a_config pcf85063a_config_##inst = {        \
		.i2c = I2C_DT_SPEC_INST_GET(inst),                                  \
	};                                                                      \
                                                                            \
	static struct pcf85063a_data pcf85063a_data_##inst;                     \
                                                                            \
	DEVICE_DT_INST_DEFINE(inst, &pcf85063a_init, NULL,                      \
						  &pcf85063a_data_##inst, &pcf85063a_config_##inst, \
						  POST_KERNEL, CONFIG_RTC_INIT_PRIORITY, &pcf85063a_driver_api);

DT_INST_FOREACH_STATUS_OKAY(PCF85063A_INIT)