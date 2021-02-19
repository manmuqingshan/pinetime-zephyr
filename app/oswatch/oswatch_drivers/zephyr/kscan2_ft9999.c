/*
 * Copyright (c) 2020 NXP
 * Copyright (c) 2020 Mark Olsson <mark@markolsson.se>
 * Copyright (c) 2020 Teslabs Engineering S.L.
 * Copyright (c) 2021 <najnesnaj@yahoo.com> 
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT focaltech_ft9999

#include <drivers/kscan.h>
#include <drivers/i2c.h>
#include <drivers/gpio.h>

#include <logging/log.h>
LOG_MODULE_REGISTER(ft9999, CONFIG_KSCAN_LOG_LEVEL);

/* FT9999 used registers */
#define REG_TD_STATUS		0x02U
#define REG_P1_XH		0x03U

/* REG_TD_STATUS: Touch points. */
#define TOUCH_POINTS_POS	0U
#define TOUCH_POINTS_MSK	0x0FU

/* REG_Pn_XH: Events. */
#define EVENT_POS		6U
#define EVENT_MSK		0x03U

#define EVENT_PRESS_DOWN	0x00U
#define EVENT_LIFT_UP		0x01U
#define EVENT_CONTACT		0x02U
#define EVENT_NONE		0x03U

/* REG_Pn_XH: Position */
#define POSITION_H_MSK		0x0FU

/** GPIO DT information. */
struct gpio_dt_info {
	/** Port. */
	const char *port;
	/** Pin. */
	gpio_pin_t pin;
	/** Flags. */
	gpio_dt_flags_t flags;
};

/** FT9999 configuration (DT). */
struct ft9999_config {
	/** I2C controller device name. */
	char *i2c_name;
	/** I2C chip address. */
	uint8_t i2c_address;
#ifdef CONFIG_KSCAN_FT9999_INTERRUPT
	/** Interrupt GPIO information. */
	struct gpio_dt_info int_gpio;
#endif
};

/** FT9999 data. */
struct ft9999_data {
	/** Device pointer. */
	const struct device *dev;
	/** I2C controller device. */
	const struct device *i2c;
	/** KSCAN Callback. */
	kscan_callback_t callback;
	/** Work queue (for deferred read). */
	struct k_work work;
#ifdef CONFIG_KSCAN_FT9999_INTERRUPT
	/** Interrupt GPIO controller. */
	const struct device *int_gpio;
	/** Interrupt GPIO callback. */
	struct gpio_callback int_gpio_cb;
#else
	/** Timer (polling mode). */
	struct k_timer timer;
#endif
};

static int ft9999_process(const struct device *dev)
{
	const struct ft9999_config *config = dev->config;
	struct ft9999_data *data = dev->data;

	int r;
	uint8_t points;
	//uint8_t coords[4U];
	uint8_t coords[9U];
	uint8_t gesture;
	uint8_t event;
	uint16_t row, col;
	bool pressed=0;

	/* obtain number of touch points (NOTE: multi-touch ignored) */
/*r = i2c_reg_read_byte(data->i2c, config->i2c_address, REG_TD_STATUS,
			      &points);
	if (r < 0) {
		return r;
	}

	points = (points >> TOUCH_POINTS_POS) & TOUCH_POINTS_MSK;
	if (points != 0U && points != 1U) {
		return 0;
	}
*/
	/* obtain first point X, Y coordinates and event from:
	 * REG_P1_XH, REG_P1_XL, REG_P1_YH, REG_P1_YL.
	 */
	//r = i2c_burst_read(data->i2c, config->i2c_address, REG_P1_XH, coords,
	//		   sizeof(coords));
	r = i2c_burst_read(data->i2c, config->i2c_address, 0x0, coords, sizeof(coords));
	if (r < 0) {
		return r;
	}


        points= coords[2] & TOUCH_POINTS_MSK;
	if (points != 0U && points != 1U) {
		return 0;
	}




        gesture = coords[1];
	//event = (coords[0] >> EVENT_POS) & EVENT_MSK;
	event = (coords[3] >> EVENT_POS) & EVENT_MSK;
	//row = ((coords[0] & POSITION_H_MSK) << 8U) | coords[1];
	row = ((coords[3] & POSITION_H_MSK) << 8U) | coords[4];
	//col = ((coords[2] & POSITION_H_MSK) << 8U) | coords[3];
	col = ((coords[5] & POSITION_H_MSK) << 8U) | coords[6];
	//pressed = (event == EVENT_PRESS_DOWN) || (event == EVENT_CONTACT);
	//(0=down 1=up 2=contact)
	//if (event >= 0) pressed=1; //to make distinction between sliding and touched
	//pressed = (event == EVENT_PRESS_DOWN) || (event == EVENT_CONTACT);
           
        pressed=0;
	//0x05 single click 0x0B double click 0x0C long press)
	if ((gesture==0x05) || (gesture==0x0B) || (gesture==0x0C)) pressed=1;

	//LOG_DBG("gesture: %d,event: %d, row: %d, col: %d", gesture, event, row, col);
	LOG_INF("gesture: %d,event: %d, row: %d, col: %d", gesture, event, row, col);

	data->callback(dev, row, col, pressed);

	return 0;
}

static void ft9999_work_handler(struct k_work *work)
{
	struct ft9999_data *data = CONTAINER_OF(work, struct ft9999_data, work);

	ft9999_process(data->dev);
}

#ifdef CONFIG_KSCAN_FT9999_INTERRUPT
static void ft9999_isr_handler(const struct device *dev,
			       struct gpio_callback *cb, uint32_t pins)
{
	struct ft9999_data *data = CONTAINER_OF(cb, struct ft9999_data, int_gpio_cb);

	k_work_submit(&data->work);
}
#else
static void ft9999_timer_handler(struct k_timer *timer)
{
	struct ft9999_data *data = CONTAINER_OF(timer, struct ft9999_data, timer);

	k_work_submit(&data->work);
}
#endif

static int ft9999_configure(const struct device *dev,
			    kscan_callback_t callback)
{
	struct ft9999_data *data = dev->data;

	if (!callback) {
		LOG_ERR("Invalid callback (NULL)");
		return -EINVAL;
	}

	data->callback = callback;

	return 0;
}

static int ft9999_enable_callback(const struct device *dev)
{
	struct ft9999_data *data = dev->data;

#ifdef CONFIG_KSCAN_FT9999_INTERRUPT
	gpio_add_callback(data->int_gpio, &data->int_gpio_cb);
#else
	k_timer_start(&data->timer, K_MSEC(CONFIG_KSCAN_FT9999_PERIOD),
		      K_MSEC(CONFIG_KSCAN_FT9999_PERIOD));
#endif

	return 0;
}

static int ft9999_disable_callback(const struct device *dev)
{
	struct ft9999_data *data = dev->data;

#ifdef CONFIG_KSCAN_FT9999_INTERRUPT
	gpio_remove_callback(data->int_gpio, &data->int_gpio_cb);
#else
	k_timer_stop(&data->timer);
#endif

	return 0;
}

static int ft9999_init(const struct device *dev)
{
	const struct ft9999_config *config = dev->config;
	struct ft9999_data *data = dev->data;

	data->i2c = device_get_binding(config->i2c_name);
	if (!data->i2c) {
		LOG_ERR("Could not find I2C controller");
		return -ENODEV;
	}

	data->dev = dev;

	k_work_init(&data->work, ft9999_work_handler);

#ifdef CONFIG_KSCAN_FT9999_INTERRUPT
	int r;

	data->int_gpio = device_get_binding(config->int_gpio.port);
	if (!data->int_gpio) {
		LOG_ERR("Could not find interrupt GPIO controller");
		return -ENODEV;
	}

	r = gpio_pin_configure(data->int_gpio, config->int_gpio.pin,
			       config->int_gpio.flags | GPIO_INPUT);
	if (r < 0) {
		LOG_ERR("Could not configure interrupt GPIO pin");
		return r;
	}

	r = gpio_pin_interrupt_configure(data->int_gpio, config->int_gpio.pin,
					 GPIO_INT_EDGE_TO_ACTIVE);
	if (r < 0) {
		LOG_ERR("Could not configure interrupt GPIO interrupt.");
		return r;
	}

	gpio_init_callback(&data->int_gpio_cb, ft9999_isr_handler,
			   BIT(config->int_gpio.pin));
#else
	k_timer_init(&data->timer, ft9999_timer_handler, NULL);
#endif

	return 0;
}

static const struct kscan_driver_api ft9999_driver_api = {
	.config = ft9999_configure,
	.enable_callback = ft9999_enable_callback,
	.disable_callback = ft9999_disable_callback,
};

#define DT_INST_GPIO(index, gpio_pha)					       \
	{								       \
		DT_INST_GPIO_LABEL(index, gpio_pha),			       \
		DT_INST_GPIO_PIN(index, gpio_pha),			       \
		DT_INST_GPIO_FLAGS(index, gpio_pha),			       \
	}

#ifdef CONFIG_KSCAN_FT9999_INTERRUPT
#define FT9999_DEFINE_CONFIG(index)					       \
	static const struct ft9999_config ft9999_config_##index = {	       \
		.i2c_name = DT_INST_BUS_LABEL(index),			       \
		.i2c_address = DT_INST_REG_ADDR(index),			       \
		.int_gpio = DT_INST_GPIO(index, int_gpios)		       \
	}
#else
#define FT9999_DEFINE_CONFIG(index)					       \
	static const struct ft9999_config ft9999_config_##index = {	       \
		.i2c_name = DT_INST_BUS_LABEL(index),			       \
		.i2c_address = DT_INST_REG_ADDR(index)			       \
	}
#endif

#define FT9999_INIT(index)                                                     \
	FT9999_DEFINE_CONFIG(index);					       \
	static struct ft9999_data ft9999_data_##index;			       \
	DEVICE_AND_API_INIT(ft9999_##index, DT_INST_LABEL(index), ft9999_init, \
			    &ft9999_data_##index, &ft9999_config_##index,      \
			    POST_KERNEL, CONFIG_KSCAN_INIT_PRIORITY,	       \
			    &ft9999_driver_api);

DT_INST_FOREACH_STATUS_OKAY(FT9999_INIT)
