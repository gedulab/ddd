// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Rockchip RK3588 Temperature Sensor ADC (TSADC)
 *
 * Copyright (c) 2023 Your Name <your.email@example.com>
 *
 * This driver is based on the Rockchip thermal driver for previous SoCs.
 * It supports the TSADC controller found in the RK3588 SoC, which provides
 * multiple channels for on-chip temperature sensors.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/thermal.h>

/* RK3588 TSADC Register Definitions */
#define TSADC_AUTO_CON			0x0004
#define TSADC_INT_EN			0x0008
#define TSADC_INT_PD			0x000c
#define TSADC_DATA(chn)		(0x0020 + (chn) * 4)
#define TSADC_COMP_INT(chn)	(0x0030 + (chn) * 4)
#define TSADC_COMP_SHUT(chn)	(0x0040 + (chn) * 4)
#define TSADC_HIGHT_INT_DEBOUNCE	0x0060
#define TSADC_HIGHT_TSHUT_DEBOUNCE	0x0064
#define TSADC_AUTO_PERIOD		0x0068
#define TSADC_AUTO_PERIOD_HT		0x006c

#define TSADC_AUTO_CON_SRC_EN(chn)	BIT(4 + (chn))
#define TSADC_AUTO_CON_START		BIT(0)
#define TSADC_INT_SRC_EN(chn)		BIT(chn)
#define TSADC_INT_SRC_MASK(chn)		BIT(chn)
#define TSADC_SHUT_2GPIO_SRC_EN(chn)	BIT(chn)
#define TSADC_SHUT_CRU_SRC_EN(chn)	BIT((chn) + 8)

#define TSADC_DATA_MASK			0xfff
#define TSADC_MAX_CHANNELS		8

/* The conversion time is affected by the clock rate. */
#define TSADC_SAMPLE_RATE		(20 * 1000) /* 20 kHz */
#define TSADC_SAMPLE_CYCLE		(4800 / TSADC_SAMPLE_RATE)

/**
 * struct rk3588_tsadc_chip - hold the private data for the TSADC driver
 * @dev: pointer to the device structure
 * @regs: base address of the TSADC registers
 * @clk: the TSADC controller clock
 * @rst: the TSADC controller reset control
 * @irq: the interrupt number for the TSADC
 * @tzd: pointer to the thermal zone device structure
 * @id: the sensor channel ID
 * @name: the name of the temperature sensor
 */
struct rk3588_tsadc_chip {
	struct device *dev;
	void __iomem *regs;
	struct clk *clk;
	struct reset_control *rst;
	struct thermal_zone_device *tzd;
	int irq;
	int id;
	const char *name;
};

/**
 * struct rk3588_tsadc_table - holds the temperature-to-code conversion table
 * @temp: temperature in Celsius
 * @code: the corresponding ADC raw value
 */
struct rk3588_tsadc_table {
	int temp;
	int code;
};

/*
 * Temperature-to-code conversion table for RK3588.
 * This table is derived from the RK3588 Technical Reference Manual (TRM).
 * The driver uses linear interpolation between points in this table.
 */
static const struct rk3588_tsadc_table rk3588_code_table[] = {
	{ -40, 3800 }, { -30, 3630 }, { -20, 3440 }, { -10, 3240 },
	{ 0, 3020 }, { 10, 2790 }, { 20, 2550 }, { 30, 2290 },
	{ 40, 2020 }, { 50, 1730 }, { 60, 1420 }, { 70, 1090 },
	{ 80, 740 }, { 90, 360 }, { 100, -50 }, { 110, -530 }
};

/**
 * rk3588_tsadc_code_to_temp() - Convert the raw ADC value to temperature
 * @table: conversion table
 * @code: the raw ADC value
 * @temp: the pointer to store the result
 *
 * This function performs a linear interpolation to find the temperature
 * corresponding to the given ADC code.
 *
 * Return: 0 on success, -EINVAL if the code is out of range.
 */
static int rk3588_tsadc_code_to_temp(const struct rk3588_tsadc_table *table,
				     int code, int *temp)
{
	unsigned int low = 1;
	unsigned int high = ARRAY_SIZE(rk3588_code_table) - 1;
	unsigned int mid;
	u32 num;
	u32 den;

	/* Code is out of range, either too cold or too hot */
	if (code > table[0].code)
		return -EAGAIN; /* Not ready yet, or sensor disconnected */
	if (code < table[high].code)
		return -EINVAL; /* Temperature is too high */

	while (low <= high) {
		mid = (low + high) / 2;
		if (code >= table[mid].code && code <= table[mid - 1].code) {
			break;
		} else if (code > table[mid].code) {
			high = mid - 1;
		} else {
			low = mid + 1;
		}
	}

	/* Linear interpolation */
	num = table[mid - 1].temp - table[mid].temp;
	den = table[mid - 1].code - table[mid].code;
	*temp = table[mid - 1].temp - ((code - table[mid].code) * num) / den;

	return 0;
}

/**
 * rk3588_tsadc_irq() - Interrupt handler for the TSADC
 * @irq: the interrupt number
 * @dev_id: pointer to the private data structure
 *
 * This function handles the temperature threshold interrupts.
 * It clears the interrupt status and notifies the thermal framework.
 *
 * Return: IRQ_HANDLED on success.
 */
static irqreturn_t rk3588_tsadc_irq(int irq, void *dev_id)
{
	struct rk3588_tsadc_chip *chip = dev_id;
	u32 val;

	val = readl_relaxed(chip->regs + TSADC_INT_PD);

	/* Clear all pending interrupts */
	writel_relaxed(val, chip->regs + TSADC_INT_PD);

	if (val & TSADC_INT_SRC_MASK(chip->id)) {
		/*
		 * The temperature for this channel has crossed the high threshold.
		 * Notify the thermal framework to re-evaluate the thermal zone.
		 */
		thermal_zone_device_update(chip->tzd, THERMAL_EVENT_UNSPECIFIED);
	}

	return IRQ_HANDLED;
}

/**
 * rk3588_tsadc_get_temp() - Read the temperature from the sensor
 * @data: pointer to the private data structure
 * @temp: pointer to store the temperature value
 *
 * This is the callback function for the thermal framework.
 * It reads the raw ADC value, converts it to Celsius, and returns it.
 *
 * Return: 0 on success, a negative error code on failure.
 */
static int rk3588_tsadc_get_temp(void *data, int *temp)
{
	struct rk3588_tsadc_chip *chip = data;
	int raw_val, ret;

	raw_val = readl_relaxed(chip->regs + TSADC_DATA(chip->id));
	raw_val &= TSADC_DATA_MASK;

	ret = rk3588_tsadc_code_to_temp(rk3588_code_table, raw_val, temp);
	if (ret == -EAGAIN) {
		/* Sensor not ready, return a very cold temperature */
		*temp = -40000; /* -40 C */
		return 0;
	}

	return ret;
}

static const struct thermal_zone_of_device_ops rk3588_of_thermal_ops = {
	.get_temp = rk3588_tsadc_get_temp,
};

/**
 * rk3588_tsadc_initialize() - Initialize the TSADC hardware
 * @chip: pointer to the private data structure
 *
 * This function enables the clock, de-asserts the reset, and configures
 * the TSADC controller registers for operation.
 *
 * Return: 0 on success, a negative error code on failure.
 */
static int rk3588_tsadc_initialize(struct rk3588_tsadc_chip *chip)
{
	int ret;

	/* 1. Enable the clock */
	ret = clk_prepare_enable(chip->clk);
	if (ret) {
		dev_err(chip->dev, "failed to enable tsadc clock: %d\n", ret);
		return ret;
	}

	/* 2. Assert and de-assert reset */
	ret = reset_control_assert(chip->rst);
	if (ret) {
		dev_err(chip->dev, "failed to assert tsadc reset: %d\n", ret);
		goto err_disable_clk;
	}
	
	udelay(10); /* Hold reset for a short time */

	ret = reset_control_deassert(chip->rst);
	if (ret) {
		dev_err(chip->dev, "failed to deassert tsadc reset: %d\n", ret);
		goto err_disable_clk;
	}

	/* 3. Configure sampling period */
	writel_relaxed(TSADC_SAMPLE_CYCLE, chip->regs + TSADC_AUTO_PERIOD);
	writel_relaxed(TSADC_SAMPLE_CYCLE, chip->regs + TSADC_AUTO_PERIOD_HT);

	/* 4. Set debounce times for high temp interrupt and shutdown */
	writel_relaxed(0, chip->regs + TSADC_HIGHT_INT_DEBOUNCE);
	writel_relaxed(0, chip->regs + TSADC_HIGHT_TSHUT_DEBOUNCE);

	/* 5. Enable the specific channel */
	writel_relaxed(TSADC_AUTO_CON_SRC_EN(chip->id), chip->regs + TSADC_AUTO_CON);

	/* 6. Enable the interrupt for this channel */
	writel_relaxed(TSADC_INT_SRC_EN(chip->id), chip->regs + TSADC_INT_EN);
	
	/* 7. Start the TSADC controller */
	writel_relaxed(TSADC_AUTO_CON_START | TSADC_AUTO_CON_SRC_EN(chip->id),
		       chip->regs + TSADC_AUTO_CON);

	return 0;

err_disable_clk:
	clk_disable_unprepare(chip->clk);
	return ret;
}

/**
 * rk3588_tsadc_disable() - Disable the TSADC hardware
 * @chip: pointer to the private data structure
 *
 * This function stops the TSADC controller, disables interrupts,
 * and disables the clock.
 */
static void rk3588_tsadc_disable(struct rk3588_tsadc_chip *chip)
{
	/* Stop the controller */
	writel_relaxed(0, chip->regs + TSADC_AUTO_CON);

	/* Disable interrupts */
	writel_relaxed(0, chip->regs + TSADC_INT_EN);

	/* Assert reset and disable clock */
	reset_control_assert(chip->rst);
	clk_disable_unprepare(chip->clk);
}

/**
 * rk3588_tsadc_probe() - Probe function for the TSADC driver
 * @pdev: pointer to the platform device structure
 *
 * This function is called when a matching device is found.
 * It initializes the hardware, requests the IRQ, and registers
 * the thermal zone device.
 *
 * Return: 0 on success, a negative error code on failure.
 */
static int rk3588_tsadc_probe(struct platform_device *pdev)
{
	struct rk3588_tsadc_chip *chip;
	struct device_node *np = pdev->dev.of_node;
	int ret;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = &pdev->dev;
	platform_set_drvdata(pdev, chip);

	/* Get resources from device tree */
	chip->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(chip->regs))
		return PTR_ERR(chip->regs);

	chip->clk = devm_clk_get(&pdev->dev, "tsadc");
	if (IS_ERR(chip->clk)) {
		dev_err(&pdev->dev, "failed to get tsadc clock\n");
		return PTR_ERR(chip->clk);
	}

	chip->rst = devm_reset_control_get_exclusive(&pdev->dev, "tsadc-apb");
	if (IS_ERR(chip->rst)) {
		dev_err(&pdev->dev, "failed to get tsadc reset\n");
		return PTR_ERR(chip->rst);
	}

	chip->irq = platform_get_irq(pdev, 0);
	if (chip->irq < 0) {
		dev_err(&pdev->dev, "failed to get tsadc irq\n");
		return chip->irq;
	}

	ret = of_property_read_u32(np, "reg", &chip->id);
	if (ret) {
		dev_err(&pdev->dev, "failed to get sensor id\n");
		return ret;
	}
	
	chip->name = np->name;

	/* Initialize hardware */
	ret = rk3588_tsadc_initialize(chip);
	if (ret)
		return ret;

	/* Register interrupt handler */
	ret = devm_request_threaded_irq(&pdev->dev, chip->irq, NULL,
					rk3588_tsadc_irq,
					IRQF_ONESHOT,
					dev_name(&pdev->dev), chip);
	if (ret) {
		dev_err(&pdev->dev, "failed to request irq: %d\n", ret);
		goto err_disable_hw;
	}

	/* Register with the thermal framework */
	chip->tzd = devm_thermal_zone_of_sensor_register(&pdev->dev, chip->id, chip,
							 &rk3588_of_thermal_ops);
	if (IS_ERR(chip->tzd)) {
		dev_err(&pdev->dev, "failed to register thermal zone sensor: %ld\n",
			PTR_ERR(chip->tzd));
		ret = PTR_ERR(chip->tzd);
		goto err_disable_hw;
	}

	dev_info(&pdev->dev, "Rockchip RK3588 TSADC '%s' (channel %d) initialized\n",
		 chip->name, chip->id);

	return 0;

err_disable_hw:
	rk3588_tsadc_disable(chip);
	return ret;
}

/**
 * rk3588_tsadc_remove() - Remove function for the TSADC driver
 * @pdev: pointer to the platform device structure
 *
 * This function is called when the device is removed.
 * It disables the hardware.
 *
 * Return: 0 always.
 */
static int rk3588_tsadc_remove(struct platform_device *pdev)
{
	struct rk3588_tsadc_chip *chip = platform_get_drvdata(pdev);

	rk3588_tsadc_disable(chip);

	return 0;
}

/* Match table for of_platform binding */
static const struct of_device_id rk3588_tsadc_of_match[] = {
	{ .compatible = "rockchip,rk3588-tsadc" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, rk3588_tsadc_of_match);

static struct platform_driver rk3588_tsadc_driver = {
	.driver = {
		.name = "rk3588-tsadc",
		.of_match_table = rk3588_tsadc_of_match,
	},
	.probe = rk3588_tsadc_probe,
	.remove = rk3588_tsadc_remove,
};
module_platform_driver(rk3588_tsadc_driver);

MODULE_AUTHOR("GSL <yinkui.zhang@nanocode.cn>");
MODULE_DESCRIPTION("Rockchip RK3588 TSADC driver");
MODULE_LICENSE("GPL v2");

