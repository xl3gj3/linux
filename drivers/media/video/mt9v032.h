/*
 * Interface for the MT9V032 camera sensor kernel module.
 *
 * Author:
 * 	Ignacio Garcia Perez <iggarpe@gmail.com>
 *
 * Based on mt9p012.h
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#ifndef MT9V032_H
#define MT9V032_H

#include <media/v4l2-int-device.h>

#define MT9V032_MAX_WIDTH				752
#define MT9V032_MAX_HEIGHT				480

/* MT9V032 register addresses */
#define MT9V032_CHIP_VERSION			0x00
#define MT9V032_COLUMN_START			0x01
#define MT9V032_ROW_START				0x02
#define MT9V032_WINDOW_HEIGHT			0x03
#define MT9V032_WINDOW_WIDTH			0x04
#define MT9V032_HORIZONTAL_BLANKING		0x05
#define MT9V032_VERTICAL_BLANKING		0x06
#define MT9V032_CHIP_CONTROL			0x07
#define MT9V032_SHUTTER_WIDTH1			0x08
#define MT9V032_SHUTTER_WIDTH2			0x09
#define MT9V032_SHUTTER_WIDTH_CTRL		0x0a
#define MT9V032_TOTAL_SHUTTER_WIDTH		0x0b
#define MT9V032_RESET					0x0c
#define MT9V032_READ_MODE				0x0d
#define MT9V032_MONITOR_MODE			0x0e
#define MT9V032_PIXEL_OPERATION_MODE	0x0f
#define MT9V032_LED_OUT_CONTROL			0x1b
#define MT9V032_ADC_MODE_CONTROL		0x1c
#define MT9V032_VREF_ADC_CTRL			0x2c
#define MT9V032_V1						0x31
#define MT9V032_V2						0x32
#define MT9V032_V3						0x33
#define MT9V032_V4						0x34
#define MT9V032_ANALOG_GAIN				0x35
#define MT9V032_MAXIMUM_ANALOG_GAIN		0x36
#define MT9V032_FRAME_DARK_AVERAGE		0x42
#define MT9V032_DARK_AVG_THRESHOLDS		0x46
#define MT9V032_BL_CALIB_CTRL			0x47
#define MT9V032_BL_CALIB_VALUE			0x48
#define MT9V032_BL_CALIB_STEP			0x4C
#define MT9V032_RN_CORR_CTRL_1			0x70
#define MT9V032_RN_CONSTANT				0x72
#define MT9V032_RN_CORR_CTRL_2			0x73
#define MT9V032_PIXCLK_FV_LV			0x74
#define MT9V032_DIGITAL_TEST_PATTERN	0x7f
#define MT9V032_AEC_AGC_BIN				0xA5
#define MT9V032_AEC_UPDATE_FREQUENCY	0xA6
#define MT9V032_AEC_LPF					0xA8
#define MT9V032_AGC_UPDATE_FREQUENCY	0xA9
#define MT9V032_AGC_LPF					0xAB
#define MT9V032_AEC_AGC_ENABLE			0xAF
#define MT9V032_AEC_AGC_PIX_COUNT		0xB0
#define MT9V032_MAX_SHUTTER_WIDTH		0xBD
#define MT9V032_BIN_DIFF_THRESHOLD		0xBE

/* read only registers */
#define MT9V032_AGC_OUTPUT				0xBA
#define MT9V032_AEC_OUTPUT				0xBB

/* configuration bits */
#define MT9V032_AUTO_EXPOSURE			0x01
#define MT9V032_AUTO_GAIN				0x02
#define MT9V032_LINEAR_ADC				0x02
#define MT9V032_COMPANDING_ADC			0x03
#define MTV032_NOISE_CORRECTION			0x20
#define MT9V032_SNAPSHOT_MODE			0x10
#define MT9V032_COLOR_SENSOR			0x04
#define MT9V032_HIGH_DYNAMIC_RANGE		0x40
#define MT9V032_VERTICAL_FLIP			0x10
#define MT9V032_HORIZONTAL_FLIP			0x20

/**
 * struct mt9v032_platform_data - platform data values and access functions
 * @set_power: power state access function, zero is off, non-zero is on.
 * @set_xclk: ISP clock setting function.
 * @set_priv_data: device private data (pointer) access function.
 */
struct mt9v032_platform_data {
	int (*set_power)(struct v4l2_int_device *, enum v4l2_power power);
	u32 (*set_xclk)(struct v4l2_int_device *, u32 xclkfreq);
	int (*set_priv_data)(void *);
};

#endif /* ifndef MT9V032_H */


