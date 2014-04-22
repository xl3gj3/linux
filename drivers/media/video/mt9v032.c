/*
 * MT9V032 sensor driver.
 *
 * Author:
 *  Ignacio Garcia Perez <iggarpe@gmail.com>
 *
 * Leverages mt9v022.c and leanXcam code.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/i2c.h>
#include <linux/delay.h>
#include <media/v4l2-int-device.h>

#include "mt9v032.h"

#define DRIVER_NAME "mt9v032"
#define MOD_NAME "MT9V032: "

/* MT9V032 i2c address 0x48, 0x4c, 0x58, 0x5c
 * The platform has to define i2c_board_info
 * and call i2c_register_board_info() */

static char *sensor_type;
module_param(sensor_type, charp, S_IRUGO);
MODULE_PARM_DESC(sensor_type, "Sensor type: \"color\" or \"mono\"");

static int auto_exp = 1;
module_param(auto_exp, int, S_IRUGO);
MODULE_PARM_DESC(auto_exp, "Initial state of automatic exposure");

static int auto_gain = 1;
module_param(auto_gain, int, S_IRUGO);
MODULE_PARM_DESC(auto_gain, "Initial state of automatic gain");

static int hdr = 1;
module_param(hdr, int, S_IRUGO);
MODULE_PARM_DESC(hdr, "High dynamic range");

static int low_light = 0;
module_param(low_light, int, S_IRUGO);
MODULE_PARM_DESC(low_light, "Enable companding");

static int hflip = 0;
module_param(hflip, int, S_IRUGO);
MODULE_PARM_DESC(hflip, "Horizontal flip");

static int vflip = 0;
module_param(vflip, int, S_IRUGO);
MODULE_PARM_DESC(vflip, "Vertical flip");

/**
 * struct mt9v032_sensor - main structure for storage of sensor information
 * @pdata: access functions and data for platform level information
 * @v4l2_int_device: V4L2 device structure structure
 * @i2c_client: iic client device structure
 * @pix: V4L2 pixel format information structure
 * @timeperframe: time per frame expressed as V4L fraction
 * @scaler:
 * @ver: mt9v032 chip version
 * @fps: frames per second value
 */
struct mt9v032_sensor {
	const struct mt9v032_platform_data *pdata;
	struct v4l2_int_device *v4l2_int_device;
	struct i2c_client *i2c_client;
	struct v4l2_pix_format pix;
	struct v4l2_fract timeperframe;
	int version;
	int fps;
	int detected;

	u16 chip_control;
	u16 read_mode;
	u16 aec_agc_enable;
	u16 horiz_blank;
	u16 row_clocks;

	int shutter;
	int gain;

	int pixel_mode;
	int adc_mode;
};

static struct mt9v032_sensor mt9v032_sensor = {
	.timeperframe = {
		.numerator = 1,
		.denominator = 60,
	},
	.shutter = 480,			/* Make these coherent with the */
	.gain = 16,				/* default control values */
};

/* list of image formats supported by mt9p012 sensor */
const static struct v4l2_fmtdesc mt9v032_color_formats[] = {
	{
		.description    = "Bayer10 (GrR/BGb)",
		//.pixelformat    = V4L2_PIX_FMT_SGRBG10,
		.pixelformat	= V4L2_PIX_FMT_SBGGR10,
	}
};

const static struct v4l2_fmtdesc mt9v032_mono_formats[] = {
	{
		.description    = "Bayer10 (GrR/BGb)",
		.pixelformat    = V4L2_PIX_FMT_SGRBG10,
	}
};

static const struct v4l2_fmtdesc *mt9v032_formats;
static int mt9v032_num_formats;

/**
 * struct vcontrol - Video controls
 * @v4l2_queryctrl: V4L2 VIDIOC_QUERYCTRL ioctl structure
 * @current_value: current value of this control
 */
static const struct v4l2_queryctrl mt9v032_controls [] = {
	{
		.id			= V4L2_CID_VFLIP,
		.type			= V4L2_CTRL_TYPE_BOOLEAN,
		.name			= "Flip Vertically",
		.minimum		= 0,
		.maximum		= 1,
		.step			= 1,
		.default_value	= 0,
		.flags			= 0,
	},{
		.id			= V4L2_CID_HFLIP,
		.type			= V4L2_CTRL_TYPE_BOOLEAN,
		.name			= "Flip Horizontally",
		.minimum		= 0,
		.maximum		= 1,
		.step			= 1,
		.default_value	= 0,
		.flags			= 0,
	},{
		.id			= V4L2_CID_EXPOSURE,
		.type			= V4L2_CTRL_TYPE_INTEGER,
		.name			= "Exposure",
		.minimum		= 2,
		.maximum		= 480,
		.step			= 1,
		.default_value	= 480,
		.flags			= V4L2_CTRL_FLAG_SLIDER,
	},{
		.id			= V4L2_CID_GAIN,
		.type			= V4L2_CTRL_TYPE_INTEGER,
		.name			= "Analog Gain",
		.minimum		= 16,
		.maximum		= 64,
		.step			= 1,
		.default_value	= 16,
		.flags			= V4L2_CTRL_FLAG_SLIDER,
	},{
		.id			= V4L2_CID_EXPOSURE_AUTO,
		.type			= V4L2_CTRL_TYPE_BOOLEAN,
		.name			= "Automatic Exposure",
		.minimum		= 0,
		.maximum		= 1,
		.step			= 1,
		.default_value	= 1,
		.flags			= 0,
	},{
		.id			= V4L2_CID_AUTOGAIN,
		.type			= V4L2_CTRL_TYPE_BOOLEAN,
		.name			= "Automatic Gain",
		.minimum		= 0,
		.maximum		= 1,
		.step			= 1,
		.default_value	= 1,
		.flags			= 0,
	}
};

static int reg_read(struct mt9v032_sensor *sensor, u8 reg)
{
	struct i2c_client *client = sensor->i2c_client;
	int value = i2c_smbus_read_word_data(client, reg);
	if (value < 0) return value;
	return swab16(value);
}

static int reg_write(struct mt9v032_sensor *sensor, u8 reg, u16 value)
{
	struct i2c_client *client = sensor->i2c_client;
	return i2c_smbus_write_word_data(client, reg, swab16(value));
}

/* Default Register Values */
static struct { u8 addr; u16 value; const char *name; } reg_default [] = {
	{ MT9V032_COLUMN_START,			0x0001, "Column Start" },
	{ MT9V032_ROW_START,			0x0004, "Row Start" },
	{ MT9V032_WINDOW_HEIGHT,		0x01e0, "Window Height" },
	{ MT9V032_WINDOW_WIDTH,			0x02f0, "Window Width" },
	{ MT9V032_HORIZONTAL_BLANKING,	0x005e, "Horizontal Blanking" },
	{ MT9V032_VERTICAL_BLANKING,	0x002d, "Vertical Blanking" },
	{ MT9V032_CHIP_CONTROL,			0x0388, "Chip Control" },
	{ MT9V032_SHUTTER_WIDTH1, 		0x01bb, "Shutter Width 1" },
	{ MT9V032_SHUTTER_WIDTH2, 		0x01d9, "Shutter Width 2" },
	{ MT9V032_SHUTTER_WIDTH_CTRL, 	0x0164, "Shutter Width Ctrl" },
	{ MT9V032_TOTAL_SHUTTER_WIDTH, 	0x01e0, "Total Shutter Width" },
	{ MT9V032_RESET, 				0x0000, "Reset" },
	{ MT9V032_READ_MODE, 			0x0300, "Read Mode" },
	{ MT9V032_MONITOR_MODE, 		0x0000, "Monitor Mode" },
	{ MT9V032_PIXEL_OPERATION_MODE, 0x0011, "Pixel Operation Mode" },
	{ MT9V032_LED_OUT_CONTROL, 		0x0000, "LED_OUT Ctrl" },
	{ MT9V032_ADC_MODE_CONTROL, 	0x0002, "ADC Mode Control" },
	{ MT9V032_VREF_ADC_CTRL, 		0x0004, "VREF_ADC Control" },
	{ MT9V032_V1,					0x001d, "V1" },
	{ MT9V032_V2,					0x0018, "V2" },
	{ MT9V032_V3,					0x0015, "V3" },
	{ MT9V032_V4,					0x0004, "V4" },
	{ MT9V032_ANALOG_GAIN,			0x0010, "Analog Gain (16-64)" },
	{ MT9V032_MAXIMUM_ANALOG_GAIN, 	0x0040, "Max Analog Gain" },
	{ MT9V032_DARK_AVG_THRESHOLDS,	0x231d, "Dark Avg Thresholds" },
	{ MT9V032_BL_CALIB_CTRL, 		0x8080, "Black Level Calib Control" },
	{ MT9V032_BL_CALIB_STEP, 		0x0002, "BL Calib Step Size" },
	{ MT9V032_RN_CORR_CTRL_1, 		0x0034, "Row Noise Corr Ctrl 1" },
	{ MT9V032_RN_CONSTANT, 			0x002a, "Row Noise Constant" },
	{ MT9V032_RN_CORR_CTRL_2, 		0x02f7, "Row Noise Corr Ctrl 2" },
	{ MT9V032_PIXCLK_FV_LV, 		0x0000, "Pixclk, FV, LV" },
	{ MT9V032_DIGITAL_TEST_PATTERN, 0x0000, "Digital Test Pattern" },
	{ MT9V032_AEC_AGC_BIN, 			0x003a, "AEC/AGC Desired Bin" },
	{ MT9V032_AEC_UPDATE_FREQUENCY, 0x0002, "AEC Update Frequency" },
	{ MT9V032_AEC_LPF, 				0x0000, "AEC LPF" },
	{ MT9V032_AGC_UPDATE_FREQUENCY, 0x0002, "AGC Update Frequency" },
	{ MT9V032_AGC_LPF, 				0x0002, "AGC LPF" },
	{ MT9V032_AEC_AGC_ENABLE,		0x0003, "AEC/AGC Enable" },
	{ MT9V032_AEC_AGC_PIX_COUNT,	0xabe0, "AEC/AGC Pix Count" },
	{ MT9V032_MAX_SHUTTER_WIDTH,	0x01e0, "Maximum Shutter Width" },
	{ MT9V032_BIN_DIFF_THRESHOLD,	0x0014, "AGC/AEC Bin Difference Threshold" },
};

/**
 * mt9v032_configure - Configure the mt9v032 for the specified image mode
 */

static int mt9v032_configure(struct mt9v032_sensor *sensor)
{
	struct i2c_client *client = sensor->i2c_client;
	int i, result, window_height, gain;

	/*
	 *	blanking values were calculated using the equations on page 15 of the datasheet
	 *	for 60 fps with a 27MHz pixel clock
	 */
	int horizontal_blanking = 43;
	int vertical_blanking = 88;

	/* Soft reset (wait 15 clock cycles, actually a bit longer...) */
	reg_write(sensor, MT9V032_RESET, 0x0003);
	mdelay(1);

	/* Initialize register values */
	for (i = 0; i < ARRAY_SIZE(reg_default); i++)
		reg_write(sensor, reg_default[i].addr, reg_default[i].value);

	/* Update shadowed registers to prevents verification 
	 * from erroneously reporting failed writes. */
	reg_write(sensor, MT9V032_RESET, 0x0001);
	mdelay(1);

	/* Verify */
	for (i = 0; i < ARRAY_SIZE(reg_default); i++)
	{
		result = reg_read(sensor, reg_default[i].addr);
		if (result != reg_default[i].value)
			dev_info(&client->dev, "Attempted to set %s to:%04X read:%04X\n", 
			reg_default[i].name,
			reg_default[i].value,
			result);
	}

	/* Set horizontal and vertical blanking */
	reg_write(sensor, MT9V032_HORIZONTAL_BLANKING, horizontal_blanking);
	reg_write(sensor, MT9V032_VERTICAL_BLANKING, vertical_blanking);

	/* Set snapshot mode on startup */
	sensor->chip_control = reg_read(sensor, MT9V032_CHIP_CONTROL) | MT9V032_SNAPSHOT_MODE;
	reg_write(sensor, MT9V032_CHIP_CONTROL, sensor->chip_control);

	/* Find the max shutter width - this will be the default if auto exposure is disabled */
	window_height = reg_read(sensor, MT9V032_WINDOW_HEIGHT);
	sensor->shutter = window_height + vertical_blanking - 2;
	reg_write(sensor, MT9V032_TOTAL_SHUTTER_WIDTH, sensor->shutter);

	/* Set sensor operation mode */
	sensor->pixel_mode = reg_read(sensor, MT9V032_PIXEL_OPERATION_MODE);
	if (hdr) sensor->pixel_mode |= MT9V032_HIGH_DYNAMIC_RANGE;
	else sensor->pixel_mode &= ~MT9V032_HIGH_DYNAMIC_RANGE;
	reg_write(sensor, MT9V032_PIXEL_OPERATION_MODE, sensor->pixel_mode);

	/* Set read mode */
	sensor->read_mode = reg_read(sensor, MT9V032_READ_MODE);
	if (vflip) sensor->read_mode |= MT9V032_VERTICAL_FLIP;
	else sensor->read_mode &= ~MT9V032_VERTICAL_FLIP;
	if (hflip) sensor->read_mode |= MT9V032_HORIZONTAL_FLIP;
	else sensor->read_mode &= ~MT9V032_HORIZONTAL_FLIP;
	reg_write(sensor, MT9V032_READ_MODE, sensor->read_mode);

	/* Enable AEC and AGC and set their values */
	sensor->aec_agc_enable = reg_read(sensor, MT9V032_AEC_AGC_ENABLE);
	if (auto_exp) sensor->aec_agc_enable |= MT9V032_AUTO_EXPOSURE;
	else sensor->aec_agc_enable &= ~MT9V032_AUTO_EXPOSURE;
	if (auto_gain) sensor->aec_agc_enable |= MT9V032_AUTO_GAIN;
	else sensor->aec_agc_enable &= ~MT9V032_AUTO_GAIN;
	reg_write(sensor, MT9V032_AEC_AGC_ENABLE, sensor->aec_agc_enable);

	/* Enable companding */
	if (low_light) sensor->adc_mode = MT9V032_COMPANDING_ADC;
	else sensor->adc_mode = MT9V032_LINEAR_ADC;
	reg_write(sensor, MT9V032_ADC_MODE_CONTROL, sensor->adc_mode);

	/* Set manual analog gain to its maximum */
	gain = reg_read(sensor, MT9V032_MAXIMUM_ANALOG_GAIN);
	reg_write(sensor, MT9V032_ANALOG_GAIN, gain);
	
	/* Increase the maximum total shutter width to improve performance in low light */
	reg_write(sensor, MT9V032_MAX_SHUTTER_WIDTH, 4*sensor->shutter);

	return 0;
}

static int mt9v032_stop_capture(struct mt9v032_sensor *sensor)
{
	sensor->chip_control |= 0x0010; 	/* Set snapshot mode */
	return reg_write(sensor, MT9V032_CHIP_CONTROL, sensor->chip_control);
}

static int mt9v032_start_capture(struct mt9v032_sensor *sensor)
{
	sensor->chip_control &= ~0x0010;	/* Set stream mode */
	return reg_write(sensor, MT9V032_CHIP_CONTROL, sensor->chip_control);
}

static int mt9v032_detect(struct mt9v032_sensor *sensor)
{
	struct i2c_client *client = sensor->i2c_client;
	int version;
	
	version = reg_read(sensor, 0x00);
	if (version < 0)
		return -ENODEV;
	
	if (version != 0x1311 && version != 0x1313) {
		dev_warn(&client->dev, "chip version mismatch (0x%04X)\n", version);
		return -ENODEV;
	}

	dev_info(&client->dev, "chip version 0x%04X\n", version);

	return version;
}

/**
 * ioctl_queryctrl - V4L2 sensor interface handler for VIDIOC_QUERYCTRL ioctl
 * @s: pointer to standard V4L2 device structure
 * @qc: standard V4L2 VIDIOC_QUERYCTRL ioctl structure
 *
 * If the requested control is supported, returns the control information
 * from the video_control[] array.  Otherwise, returns -EINVAL if the
 * control is not supported.
 */
static int ioctl_queryctrl(struct v4l2_int_device *s, struct v4l2_queryctrl *vc)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mt9v032_controls); i++) {
		if (vc->id == mt9v032_controls[i].id) {
			*vc = mt9v032_controls[i];
			return 0;
		}
	}

	vc->flags = V4L2_CTRL_FLAG_DISABLED;
	return -EINVAL;
}

/**
 * ioctl_g_ctrl - V4L2 sensor interface handler for VIDIOC_G_CTRL ioctl
 * @s: pointer to standard V4L2 device structure
 * @vc: standard V4L2 VIDIOC_G_CTRL ioctl structure
 *
 * If the requested control is supported, returns the control's current
 * value from the video_control[] array.  Otherwise, returns -EINVAL
 * if the control is not supported.
 */
static int ioctl_g_ctrl(struct v4l2_int_device *s, struct v4l2_control *vc)
{
	struct mt9v032_sensor *sensor = s->priv;

	switch (vc->id) {

	case V4L2_CID_VFLIP:
		vc->value = !!(sensor->read_mode & 0x0010);
		break;
		
	case V4L2_CID_HFLIP:
		vc->value = !!(sensor->read_mode & 0x0020);
		break;

	case V4L2_CID_EXPOSURE:
		vc->value = sensor->shutter;
		break;

	case V4L2_CID_GAIN:
		vc->value = sensor->gain;
		break;

	case V4L2_CID_EXPOSURE_AUTO:
		vc->value = !!(sensor->aec_agc_enable & 0x0001);
		break;

	case V4L2_CID_AUTOGAIN:
		vc->value = !!(sensor->aec_agc_enable & 0x0002);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

/**
 * ioctl_s_ctrl - V4L2 sensor interface handler for VIDIOC_S_CTRL ioctl
 * @s: pointer to standard V4L2 device structure
 * @vc: standard V4L2 VIDIOC_S_CTRL ioctl structure
 *
 * If the requested control is supported, sets the control's current
 * value in HW (and updates the video_control[] array).  Otherwise,
 * returns -EINVAL if the control is not supported.
 */
static int ioctl_s_ctrl(struct v4l2_int_device *s, struct v4l2_control *vc)
{
	struct mt9v032_sensor *sensor = s->priv;
	struct i2c_client *client = sensor->i2c_client;
	const struct v4l2_queryctrl *lvc;

	switch (vc->id) {

	case V4L2_CID_VFLIP:
		sensor->read_mode &= ~0x0010;
		if ((vflip = vc->value))
			sensor->read_mode |= 0x0010;
		reg_write(sensor, MT9V032_READ_MODE, sensor->read_mode);

		dev_dbg(&client->dev, "setting vertical flip %d (read_mode=0x%04X)\n", vc->value, sensor->read_mode);
		break;
		
	case V4L2_CID_HFLIP:
		sensor->read_mode &= ~0x0020;
		if ((hflip = vc->value))
			sensor->read_mode |= 0x0020;
		reg_write(sensor, MT9V032_READ_MODE, sensor->read_mode);

		dev_dbg(&client->dev, "setting horizontal flip %d (read_mode=0x%04X)\n", vc->value, sensor->read_mode);
		break;

	case V4L2_CID_EXPOSURE:
		lvc = &mt9v032_controls[2];

		if (vc->value < lvc->minimum || vc->value > lvc->maximum)
			return -EINVAL;

		/* Turn off AEC and set new shutter value */
		if (auto_exp) {
			auto_exp = 0;
			sensor->aec_agc_enable &= ~0x0001;
			reg_write(sensor, MT9V032_AEC_AGC_ENABLE, sensor->aec_agc_enable);
		}

		sensor->shutter = vc->value;
		reg_write(sensor, MT9V032_TOTAL_SHUTTER_WIDTH, sensor->shutter);

		dev_dbg(&client->dev, "setting exposure %d\n", sensor->shutter);
		break;

	case V4L2_CID_GAIN:
		lvc = &mt9v032_controls[3];

		if (vc->value < lvc->minimum || vc->value > lvc->maximum)
			return -EINVAL;

		/* Turn off AGC and set new gain value */
		if (auto_gain) {
			auto_gain = 0;
			sensor->aec_agc_enable &= ~0x0002;
			reg_write(sensor, MT9V032_AEC_AGC_ENABLE, sensor->aec_agc_enable);
		}

		sensor->gain = vc->value;
		if (sensor->gain >= 32) sensor->gain &= ~1;
		reg_write(sensor, MT9V032_ANALOG_GAIN, sensor->gain);

		dev_dbg(&client->dev, "setting gain %d\n", sensor->gain);
		break;

	case V4L2_CID_EXPOSURE_AUTO:
		sensor->aec_agc_enable &= ~0x0001;
		if ((auto_exp = vc->value))
			sensor->aec_agc_enable |= 0x0001;
		reg_write(sensor, MT9V032_AEC_AGC_ENABLE, sensor->aec_agc_enable);

		dev_dbg(&client->dev, "setting automatic exposure %d\n", vc->value);
		break;

	case V4L2_CID_AUTOGAIN:
		sensor->aec_agc_enable &= ~0x0002;
		if ((auto_gain = vc->value))
			sensor->aec_agc_enable |= 0x0002;
		reg_write(sensor, MT9V032_AEC_AGC_ENABLE, sensor->aec_agc_enable);

		dev_dbg(&client->dev, "Setting automatic gain %d\n", vc->value);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

/**
 * ioctl_enum_fmt_cap - Implement the CAPTURE buffer VIDIOC_ENUM_FMT ioctl
 * @s: pointer to standard V4L2 device structure
 * @fmt: standard V4L2 VIDIOC_ENUM_FMT ioctl structure
 *
 * Implement the VIDIOC_ENUM_FMT ioctl for the CAPTURE buffer type.
 */
static int ioctl_enum_fmt_cap(struct v4l2_int_device *s,
				   struct v4l2_fmtdesc *fmt)
{
	int index = fmt->index;
	enum v4l2_buf_type type = fmt->type;

	memset(fmt, 0, sizeof(*fmt));
	fmt->index = index;
	fmt->type = type;

	switch (fmt->type) {

	case V4L2_BUF_TYPE_VIDEO_CAPTURE:

		if (index >= mt9v032_num_formats)
			return -EINVAL;

		fmt->flags = mt9v032_formats[index].flags;
		strlcpy(fmt->description, mt9v032_formats[index].description, sizeof(fmt->description));
		fmt->pixelformat = mt9v032_formats[index].pixelformat;

		return 0;

	default:
		return -EINVAL;
	}
}

/**
 * ioctl_try_fmt_cap - Implement the CAPTURE buffer VIDIOC_TRY_FMT ioctl
 * @s: pointer to standard V4L2 device structure
 * @f: pointer to standard V4L2 VIDIOC_TRY_FMT ioctl structure
 *
 * Implement the VIDIOC_TRY_FMT ioctl for the CAPTURE buffer type.  This
 * ioctl is used to negotiate the image capture size and pixel format
 * without actually making it take effect.
 */
static int ioctl_try_fmt_cap(struct v4l2_int_device *s, struct v4l2_format *f)
{
	int ifmt;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct mt9v032_sensor *sensor = s->priv;

	pix->width = MT9V032_MAX_WIDTH;
	pix->height = MT9V032_MAX_HEIGHT;

	for (ifmt = 0; ifmt < mt9v032_num_formats; ifmt++) {
		if (pix->pixelformat == mt9v032_formats[ifmt].pixelformat)
			break;
	}
	if (ifmt == mt9v032_num_formats)
		ifmt = 0;

	pix->pixelformat = mt9v032_formats[ifmt].pixelformat;
	pix->field = V4L2_FIELD_NONE;
	pix->bytesperline = pix->width * 2;
	pix->sizeimage = pix->bytesperline * pix->height;
	pix->priv = 0;
	pix->colorspace = V4L2_COLORSPACE_SRGB;

	sensor->pix = *pix;
	return 0;
}

/**
 * ioctl_s_fmt_cap - V4L2 sensor interface handler for VIDIOC_S_FMT ioctl
 * @s: pointer to standard V4L2 device structure
 * @f: pointer to standard V4L2 VIDIOC_S_FMT ioctl structure
 *
 * If the requested format is supported, configures the HW to use that
 * format, returns error code if format not supported or HW can't be
 * correctly configured.
 */
static int ioctl_s_fmt_cap(struct v4l2_int_device *s, struct v4l2_format *f)
{
	struct mt9v032_sensor *sensor = s->priv;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	int rval;

	rval = ioctl_try_fmt_cap(s, f);
	if (!rval)
		sensor->pix = *pix;

	return rval;
}

/**
 * ioctl_g_fmt_cap - V4L2 sensor interface handler for ioctl_g_fmt_cap
 * @s: pointer to standard V4L2 device structure
 * @f: pointer to standard V4L2 v4l2_format structure
 *
 * Returns the sensor's current pixel format in the v4l2_format
 * parameter.
 */
static int ioctl_g_fmt_cap(struct v4l2_int_device *s, struct v4l2_format *f)
{
	struct mt9v032_sensor *sensor = s->priv;

	f->fmt.pix = sensor->pix;

	return 0;
}

/**
 * ioctl_g_parm - V4L2 sensor interface handler for VIDIOC_G_PARM ioctl
 * @s: pointer to standard V4L2 device structure
 * @a: pointer to standard V4L2 VIDIOC_G_PARM ioctl structure
 *
 * Returns the sensor's video CAPTURE parameters.
 */
static int ioctl_g_parm(struct v4l2_int_device *s, struct v4l2_streamparm *a)
{
	struct mt9v032_sensor *sensor = s->priv;
	struct v4l2_captureparm *cparm = &a->parm.capture;

	if (a->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	memset(a, 0, sizeof(*a));
	a->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	cparm->capability = V4L2_CAP_TIMEPERFRAME;
	cparm->timeperframe = sensor->timeperframe;

	return 0;
}

/**
 * ioctl_s_parm - V4L2 sensor interface handler for VIDIOC_S_PARM ioctl
 * @s: pointer to standard V4L2 device structure
 * @a: pointer to standard V4L2 VIDIOC_S_PARM ioctl structure
 *
 * Configures the sensor to use the input parameters, if possible.  If
 * not possible, reverts to the old parameters and returns the
 * appropriate error code.
 */
static int ioctl_s_parm(struct v4l2_int_device *s, struct v4l2_streamparm *a)
{
	struct mt9v032_sensor *sensor = s->priv;
	struct v4l2_fract *timeperframe = &a->parm.capture.timeperframe;

	sensor->fps = 60;
	sensor->timeperframe.numerator = 1;
	sensor->timeperframe.denominator = sensor->fps;

	*timeperframe = sensor->timeperframe;

	return 0;
}

/**
 * ioctl_g_priv - V4L2 sensor interface handler for vidioc_int_g_priv_num
 * @s: pointer to standard V4L2 device structure
 * @p: void pointer to hold sensor's private data address
 *
 * Returns device's (sensor's) private data area address in p parameter
 */
static int ioctl_g_priv(struct v4l2_int_device *s, void *p)
{
	struct mt9v032_sensor *sensor = s->priv;

	return sensor->pdata->set_priv_data(p);
}

/**
 * ioctl_s_power - V4L2 sensor interface handler for vidioc_int_s_power_num
 * @s: pointer to standard V4L2 device structure
 * @on: power state to which device is to be set
 *
 * Sets devices power state to requested state, if possible.
 */
static int ioctl_s_power(struct v4l2_int_device *s, enum v4l2_power power)
{
	struct mt9v032_sensor *sensor = s->priv;
	struct i2c_client *client = sensor->i2c_client;
	int rval;

	/* If STANDBY or OFF, stop capture */
	if (power == V4L2_POWER_STANDBY || power == V4L2_POWER_OFF)
		if (sensor->detected)
			mt9v032_stop_capture(sensor);

	/* If ON enable clock, otherwise disable it */
	if (power == V4L2_POWER_ON)
		sensor->pdata->set_xclk(s, 27000000);
	else {
		/* Wait for the sensor to shut down cleanly leaving LED_OUT disabled */
		msleep(50);								
		sensor->pdata->set_xclk(s, 0);
	}

	/*	
	 *	Platform-specific call to change sensor power state (it should do
	 *	its own sleep to allow power to settle)
	 */
	rval = sensor->pdata->set_power(s, power);
	if (rval < 0) {
		dev_err(&client->dev, "unable to set the power state: " DRIVER_NAME " sensor\n");
		sensor->pdata->set_xclk(s, 0);
		return rval;
	}

	/* If powered on, detect (only if not detected) and configure */
	if (power == V4L2_POWER_ON) {
		if (!sensor->detected) {
			rval = mt9v032_detect(sensor);
			if (rval < 0) {
				dev_err(&client->dev, "unable to detect " DRIVER_NAME " sensor\n");
				return rval;
			}
			sensor->detected = 1;
			sensor->version = rval;
		}
		mt9v032_configure(sensor);
		mt9v032_start_capture(sensor);
	}

	return 0;
}

/**
 * ioctl_init - V4L2 sensor interface handler for VIDIOC_INT_INIT
 * @s: pointer to standard V4L2 device structure
 *
 * Initialize the sensor device (call mt9v032_configure())
 */
static int ioctl_init(struct v4l2_int_device *s)
{
	return 0;
}

/**
 * ioctl_dev_exit - V4L2 sensor interface handler for vidioc_int_dev_exit_num
 * @s: pointer to standard V4L2 device structure
 *
 * Delinitialise the dev. at slave detach.  The complement of ioctl_dev_init.
 */
static int ioctl_dev_exit(struct v4l2_int_device *s)
{
	return 0;
}

/**
 * ioctl_dev_init - V4L2 sensor interface handler for vidioc_int_dev_init_num
 * @s: pointer to standard V4L2 device structure
 *
 * Initialise the device when slave attaches to the master.  Returns 0 if
 * mt9v032 device could be found, otherwise returns appropriate error.
 */
static int ioctl_dev_init(struct v4l2_int_device *s)
{
	return 0;
}
/**
 * ioctl_enum_framesizes - V4L2 sensor if handler for vidioc_int_enum_framesizes
 * @s: pointer to standard V4L2 device structure
 * @frms: pointer to standard V4L2 framesizes enumeration structure
 *
 * Returns possible framesizes depending on choosen pixel format
 **/
static int ioctl_enum_framesizes(struct v4l2_int_device *s, struct v4l2_frmsizeenum *frms)
{
	int ifmt;

	for (ifmt = 0; ifmt < mt9v032_num_formats; ifmt++) {
		if (frms->pixel_format == mt9v032_formats[ifmt].pixelformat)
			break;
	}
	/* Is requested pixelformat not found on sensor? */
	if (ifmt == mt9v032_num_formats)
		return -EINVAL;

	/* Do we already reached all discrete framesizes? */
	if (frms->index > 1)
		return -EINVAL;

	frms->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	frms->discrete.width = MT9V032_MAX_WIDTH;
	frms->discrete.height = MT9V032_MAX_HEIGHT;

	return 0;
}

const struct v4l2_fract mt9v032_frameintervals[] = {
	/*{  .numerator = 1, .denominator = 15 },
	{  .numerator = 1, .denominator = 20 },
	{  .numerator = 1, .denominator = 25 },
	{  .numerator = 1, .denominator = 30 }, */
	{  .numerator = 1, .denominator = 60 },
};

static int ioctl_enum_frameintervals(struct v4l2_int_device *s, struct v4l2_frmivalenum *frmi)
{
	int ifmt;

	for (ifmt = 0; ifmt < mt9v032_num_formats; ifmt++) {
		if (frmi->pixel_format == mt9v032_formats[ifmt].pixelformat)
			break;
	}
	/* Is requested pixelformat not found on sensor? */
	if (ifmt == mt9v032_num_formats)
		return -EINVAL;


	/* Do we already reached all discrete framesizes? */
	if (frmi->index >= ARRAY_SIZE(mt9v032_frameintervals))
		return -EINVAL;

	frmi->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	frmi->discrete.numerator = mt9v032_frameintervals[frmi->index].numerator;
	frmi->discrete.denominator = mt9v032_frameintervals[frmi->index].denominator;

	return 0;
}

static struct v4l2_int_ioctl_desc mt9v032_ioctl_desc[] = {
	{ .num = vidioc_int_enum_framesizes_num,
	  .func = (v4l2_int_ioctl_func *)ioctl_enum_framesizes },
	{ .num = vidioc_int_enum_frameintervals_num,
	  .func = (v4l2_int_ioctl_func *)ioctl_enum_frameintervals },
	{ .num = vidioc_int_dev_init_num,
	  .func = (v4l2_int_ioctl_func *)ioctl_dev_init },
	{ .num = vidioc_int_dev_exit_num,
	  .func = (v4l2_int_ioctl_func *)ioctl_dev_exit },
	{ .num = vidioc_int_s_power_num,
	  .func = (v4l2_int_ioctl_func *)ioctl_s_power },
	{ .num = vidioc_int_g_priv_num,
	  .func = (v4l2_int_ioctl_func *)ioctl_g_priv },
	{ .num = vidioc_int_init_num,
	  .func = (v4l2_int_ioctl_func *)ioctl_init },
	{ .num = vidioc_int_enum_fmt_cap_num,
	  .func = (v4l2_int_ioctl_func *)ioctl_enum_fmt_cap },
	{ .num = vidioc_int_try_fmt_cap_num,
	  .func = (v4l2_int_ioctl_func *)ioctl_try_fmt_cap },
	{ .num = vidioc_int_g_fmt_cap_num,
	  .func = (v4l2_int_ioctl_func *)ioctl_g_fmt_cap },
	{ .num = vidioc_int_s_fmt_cap_num,
	  .func = (v4l2_int_ioctl_func *)ioctl_s_fmt_cap },
	{ .num = vidioc_int_g_parm_num,
	  .func = (v4l2_int_ioctl_func *)ioctl_g_parm },
	{ .num = vidioc_int_s_parm_num,
	  .func = (v4l2_int_ioctl_func *)ioctl_s_parm },
	{ .num = vidioc_int_queryctrl_num,
	  .func = (v4l2_int_ioctl_func *)ioctl_queryctrl },
	{ .num = vidioc_int_g_ctrl_num,
	  .func = (v4l2_int_ioctl_func *)ioctl_g_ctrl },
	{ .num = vidioc_int_s_ctrl_num,
	  .func = (v4l2_int_ioctl_func *)ioctl_s_ctrl },
};

static struct v4l2_int_slave mt9v032_slave = {
	.attach_to = "omap34xxcam",
	.ioctls = mt9v032_ioctl_desc,
	.num_ioctls = ARRAY_SIZE(mt9v032_ioctl_desc),
};

static struct v4l2_int_device mt9v032_int_device = {
	.module = THIS_MODULE,
	.name = DRIVER_NAME,
	.priv = &mt9v032_sensor,
	.type = v4l2_int_type_slave,
	.u = {
		.slave = &mt9v032_slave,
	},
};

/**
 * mt9v032_probe - sensor driver i2c probe handler
 * @client: i2c driver client device structure
 *
 * Register sensor as an i2c client device and V4L2
 * device.
 */
static int mt9v032_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct mt9v032_sensor *sensor = &mt9v032_sensor;
	int err;

	if (i2c_get_clientdata(client))
		return -EBUSY;

	sensor->pdata = client->dev.platform_data;

	if (!sensor->pdata) {
		dev_err(&client->dev, "no platform data?\n");
		return -ENODEV;
	}

	dev_info(&client->dev, "%s sensor\n", (sensor->pixel_mode & MT9V032_COLOR_SENSOR) ? "color" : "mono");
	dev_info(&client->dev, "hflip=%d vflip=%d auto_gain=%d auto_exp=%d hdr=%d low_light=%d\n",
		hflip, vflip, auto_gain, auto_exp, hdr, low_light
	);

	sensor->v4l2_int_device = &mt9v032_int_device;
	sensor->i2c_client = client;

	i2c_set_clientdata(client, sensor);

	sensor->pix.width = MT9V032_MAX_WIDTH;
	sensor->pix.height = MT9V032_MAX_HEIGHT;
	sensor->pix.pixelformat = mt9v032_formats[0].pixelformat;

	err = v4l2_int_device_register(sensor->v4l2_int_device);
	if (err)
		i2c_set_clientdata(client, NULL);

	return err;
}

/**
 * mt9v032_remove - sensor driver i2c remove handler
 * @client: i2c driver client device structure
 *
 * Unregister sensor as an i2c client device and V4L2
 * device.  Complement of mt9v032_probe().
 */
static int mt9v032_remove(struct i2c_client *client)
{
	struct mt9v032_sensor *sensor = i2c_get_clientdata(client);

	if (!client->adapter)
		return -ENODEV;	/* our client isn't attached */

	v4l2_int_device_unregister(sensor->v4l2_int_device);
	i2c_set_clientdata(client, NULL);

	return 0;
}

/**
 * Device driver structures.
 */
static const struct i2c_device_id mt9v032_id[] = {
	{ DRIVER_NAME, 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, mt9v032_id);

static struct i2c_driver mt9v032sensor_i2c_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
	},
	.probe = mt9v032_probe,
	.remove = mt9v032_remove,
	.id_table = mt9v032_id,
};

/**
 * Module init/cleanup.
 */
static int __init mt9v032_init(void)
{
	struct mt9v032_sensor *sensor = &mt9v032_sensor;

	sensor->pixel_mode |= MT9V032_COLOR_SENSOR;
	if (sensor_type) {
		if (!strcmp(sensor_type, "mono"))
			sensor->pixel_mode &= ~MT9V032_COLOR_SENSOR;
	}

	if (sensor->pixel_mode & MT9V032_COLOR_SENSOR) {
		mt9v032_formats = mt9v032_color_formats;
		mt9v032_num_formats = ARRAY_SIZE(mt9v032_color_formats);
	} else {
		mt9v032_formats = mt9v032_mono_formats;
		mt9v032_num_formats = ARRAY_SIZE(mt9v032_mono_formats);
	}

	return i2c_add_driver(&mt9v032sensor_i2c_driver);
}

static void __exit mt9v032_cleanup(void)
{
	i2c_del_driver(&mt9v032sensor_i2c_driver);
}

module_init(mt9v032_init);
module_exit(mt9v032_cleanup);

MODULE_DESCRIPTION("mt9v032 camera sensor driver");
MODULE_AUTHOR("Ignacio Garcia Perez <iggarpe@gmail.com>");
MODULE_LICENSE("GPL");


