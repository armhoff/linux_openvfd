#include "../protocols/i2c.h"
#include "../protocols/spi_3w.h"
#include "fd628.h"

/* ****************************** Define FD628 Commands ****************************** */
#define FD628_KEY_RDCMD		0x42	/* Read keys command			*/
#define FD628_4DIG_CMD			0x00	/* Set FD628 to work in 4-digit mode	*/
#define FD628_5DIG_CMD			0x01	/* Set FD628 to work in 5-digit mode	*/
#define FD628_6DIG_CMD			0x02	/* Set FD628 to work in 6-digit mode	*/
#define FD628_7DIG_CMD			0x03	/* Set FD628 to work in 7-digit mode	*/
#define FD628_DIGADDR_WRCMD		0xC0	/* Write FD628 address			*/
#define FD628_ADDR_INC_DIGWR_CMD	0x40	/* Set Address Increment Mode		*/
#define FD628_ADDR_STATIC_DIGWR_CMD	0x44	/* Set Static Address Mode		*/
#define FD628_DISP_STATUE_WRCMD	0x80	/* Set display brightness command	*/
/* *********************************************************************************** */

static void init(void);
static unsigned short get_brightness_levels_count(void);
static unsigned short get_brightness_level(void);
static unsigned char set_brightness_level(unsigned short level);
static unsigned char get_power(void);
static void set_power(unsigned char state);
static struct fd628_display *get_display_type(void);
static unsigned char set_display_type(struct fd628_display *display);
static void set_icon(const char *name, unsigned char state);
static size_t read_data(unsigned char *data, size_t length);
static size_t write_data(const unsigned char *data, size_t length);

static struct controller_interface fd628_interface = {
	.init = init,
	.get_brightness_levels_count = get_brightness_levels_count,
	.get_brightness_level = get_brightness_level,
	.set_brightness_level = set_brightness_level,
	.get_power = get_power,
	.set_power = set_power,
	.get_display_type = get_display_type,
	.set_display_type = set_display_type,
	.set_icon = set_icon,
	.read_data = read_data,
	.write_data = write_data,
};

static struct fd628_dev *dev = NULL;
static struct protocol_interface *protocol = NULL;
static unsigned char ram_grid_size = 2;
static unsigned char ram_grid_count = 7;
static unsigned char ram_size = 14;

struct controller_interface *init_fd628(struct fd628_dev *_dev)
{
	dev = _dev;
	init();
	return &fd628_interface;
}

static size_t fd628_write_data(unsigned char address, const unsigned char *data, size_t length)
{
	unsigned char cmd = FD628_DIGADDR_WRCMD | address;
	if (length + address > ram_size)
		return (-1);

	protocol->write_byte(FD628_ADDR_INC_DIGWR_CMD);
	protocol->write_cmd_data(&cmd, 1, data, length);
	return (0);
}

static void init(void)
{
	protocol = dev->dtb_active.display.controller == CONTROLLER_HBS658 ?
		init_i2c(0, dev->clk_pin, dev->dat_pin, I2C_DELAY_100KHz) :
		init_spi_3w(dev->clk_pin, dev->dat_pin, dev->stb_pin, SPI_DELAY_100KHz);
	switch (dev->dtb_active.display.controller) {
		case CONTROLLER_FD628:
		default:
			ram_grid_size = 2;
			ram_grid_count = 7;
			protocol->write_byte(FD628_7DIG_CMD);
			break;
		case CONTROLLER_FD620:
			ram_grid_size = 2;
			ram_grid_count = 5;
			switch (dev->dtb_active.display.type) {
			case DISPLAY_TYPE_FD620_REF:
				protocol->write_byte(FD628_4DIG_CMD);
				break;
			default:
				protocol->write_byte(FD628_5DIG_CMD);
				break;
			}
			break;
		case CONTROLLER_TM1618:
			ram_grid_size = 2;
			ram_grid_count = 7;
			switch (dev->dtb_active.display.type) {
			case DISPLAY_TYPE_4D_7S_COL:
				protocol->write_byte(FD628_7DIG_CMD);
				break;
			case DISPLAY_TYPE_FD620_REF:
				protocol->write_byte(FD628_4DIG_CMD);
				break;
			default:
				protocol->write_byte(FD628_5DIG_CMD);
				break;
			}
			break;
		case CONTROLLER_HBS658:
			ram_grid_size = 1;
			ram_grid_count = 5;
			break;
	}

	ram_size = ram_grid_size * ram_grid_count;
	set_brightness_level(dev->brightness);
	memset(dev->wbuf, 0x00, sizeof(dev->wbuf));
}

static unsigned short get_brightness_levels_count(void)
{
	return 8;
}

static unsigned short get_brightness_level(void)
{
	return dev->brightness;
}

static unsigned char set_brightness_level(unsigned short level)
{
	dev->brightness = level & 0x7;
	protocol->write_byte(FD628_DISP_STATUE_WRCMD | dev->brightness | FD628_DISP_ON);
	dev->power = 1;
	return 1;
}

static unsigned char get_power(void)
{
	return dev->power;
}

static void set_power(unsigned char state)
{
	dev->power = state;
	if (state)
		set_brightness_level(dev->brightness);
	else
		protocol->write_byte(FD628_DISP_STATUE_WRCMD | FD628_DISP_OFF);
}

static struct fd628_display *get_display_type(void)
{
	return &dev->dtb_active.display;
}

static unsigned char set_display_type(struct fd628_display *display)
{
	unsigned char ret = 0;
	if (display->type < DISPLAY_TYPE_MAX && display->controller < CONTROLLER_7S_MAX && display->controller == CONTROLLER_FD650)
	{
		dev->dtb_active.display = *display;
		init();
		ret = 1;
	}

	return ret;
}

static void set_icon(const char *name, unsigned char state)
{
	struct fd628_dtb_config *dtb = &dev->dtb_active;
	switch (dtb->display.type) {
	case DISPLAY_TYPE_5D_7S_NORMAL:
	case DISPLAY_TYPE_5D_7S_T95:
	default:
		if (strncmp(name,"alarm",5) == 0) {
			dev->status_led_mask = state ? (dev->status_led_mask | dtb->led_dots[LED_DOT1_ALARM]) : (dev->status_led_mask & ~dtb->led_dots[LED_DOT1_ALARM]);
		} else if (strncmp(name,"usb",3) == 0) {
			dev->status_led_mask = state ? (dev->status_led_mask | dtb->led_dots[LED_DOT1_USB]) : (dev->status_led_mask & ~dtb->led_dots[LED_DOT1_USB]);
		} else if (strncmp(name,"play",4) == 0) {
			dev->status_led_mask = state ? (dev->status_led_mask | dtb->led_dots[LED_DOT1_PLAY]) : (dev->status_led_mask & ~dtb->led_dots[LED_DOT1_PLAY]);
		} else if (strncmp(name,"pause",5) == 0) {
			dev->status_led_mask = state ? (dev->status_led_mask | dtb->led_dots[LED_DOT1_PAUSE]) : (dev->status_led_mask & ~dtb->led_dots[LED_DOT1_PAUSE]);
		} else if (strncmp(name,"colon",5) == 0) {
			dev->status_led_mask = state ? (dev->status_led_mask | dtb->led_dots[LED_DOT1_SEC]) : (dev->status_led_mask & ~dtb->led_dots[LED_DOT1_SEC]);
		} else if (strncmp(name,"eth",3) == 0) {
			dev->status_led_mask = state ? (dev->status_led_mask | dtb->led_dots[LED_DOT1_ETH]) : (dev->status_led_mask & ~dtb->led_dots[LED_DOT1_ETH]);
		} else if (strncmp(name,"wifi",4) == 0) {
			dev->status_led_mask = state ? (dev->status_led_mask | dtb->led_dots[LED_DOT1_WIFI]) : (dev->status_led_mask & ~dtb->led_dots[LED_DOT1_WIFI]);
		}
		break;
	case DISPLAY_TYPE_5D_7S_X92:
		if (strncmp(name,"apps",4) == 0) {
			dev->status_led_mask = state ? (dev->status_led_mask | dtb->led_dots[LED_DOT2_APPS]) : (dev->status_led_mask & ~dtb->led_dots[LED_DOT2_APPS]);
		} else if (strncmp(name,"setup",5) == 0) {
			dev->status_led_mask = state ? (dev->status_led_mask | dtb->led_dots[LED_DOT2_SETUP]) : (dev->status_led_mask & ~dtb->led_dots[LED_DOT2_SETUP]);
		} else if (strncmp(name,"usb",3) == 0) {
			dev->status_led_mask = state ? (dev->status_led_mask | dtb->led_dots[LED_DOT2_USB]) : (dev->status_led_mask & ~dtb->led_dots[LED_DOT2_USB]);
		} else if (strncmp(name,"sd",2) == 0) {
			dev->status_led_mask = state ? (dev->status_led_mask | dtb->led_dots[LED_DOT2_CARD]) : (dev->status_led_mask & ~dtb->led_dots[LED_DOT2_CARD]);
		} else if (strncmp(name,"colon",5) == 0) {
			dev->status_led_mask = state ? (dev->status_led_mask | dtb->led_dots[LED_DOT2_SEC]) : (dev->status_led_mask & ~dtb->led_dots[LED_DOT2_SEC]);
		} else if (strncmp(name,"hdmi",4) == 0) {
			dev->status_led_mask = state ? (dev->status_led_mask | dtb->led_dots[LED_DOT2_HDMI]) : (dev->status_led_mask & ~dtb->led_dots[LED_DOT2_HDMI]);
		} else if (strncmp(name,"cvbs",4) == 0) {
			dev->status_led_mask = state ? (dev->status_led_mask | dtb->led_dots[LED_DOT2_CVBS]) : (dev->status_led_mask & ~dtb->led_dots[LED_DOT2_CVBS]);
		}
		break;
	case DISPLAY_TYPE_5D_7S_ABOX:
		if (strncmp(name,"power",5) == 0) {
			dev->status_led_mask = state ? (dev->status_led_mask | dtb->led_dots[LED_DOT3_POWER]) : (dev->status_led_mask & ~dtb->led_dots[LED_DOT3_POWER]);
		} else if (strncmp(name,"eth",3) == 0) {
			dev->status_led_mask = state ? (dev->status_led_mask | dtb->led_dots[LED_DOT3_LAN]) : (dev->status_led_mask & ~dtb->led_dots[LED_DOT3_LAN]);
		} else if (strncmp(name,"colon",5) == 0) {
			dev->status_led_mask = state ? (dev->status_led_mask | dtb->led_dots[LED_DOT3_SEC]) : (dev->status_led_mask & ~dtb->led_dots[LED_DOT3_SEC]);
		} else if (strncmp(name,"wifi",4) == 0) {
			dev->status_led_mask = state ? (dev->status_led_mask | dtb->led_dots[LED_DOT3_WIFIHI] | dtb->led_dots[LED_DOT3_WIFILO]) : (dev->status_led_mask & ~(dtb->led_dots[LED_DOT3_WIFIHI] | dtb->led_dots[LED_DOT3_WIFILO]));
		}
		break;
	case DISPLAY_TYPE_5D_7S_M9_PRO:
		if (strncmp(name,"b-t",3) == 0) {
			dev->status_led_mask = state ? (dev->status_led_mask | dtb->led_dots[LED_DOT4_BT]) : (dev->status_led_mask & ~dtb->led_dots[LED_DOT4_BT]);
		} else if (strncmp(name,"eth",3) == 0) {
			dev->status_led_mask = state ? (dev->status_led_mask | dtb->led_dots[LED_DOT4_ETH]) : (dev->status_led_mask & ~dtb->led_dots[LED_DOT4_ETH]);
		} else if (strncmp(name,"wifi",4) == 0) {
			dev->status_led_mask = state ? (dev->status_led_mask | dtb->led_dots[LED_DOT4_WIFI]) : (dev->status_led_mask & ~dtb->led_dots[LED_DOT4_WIFI]);
		} else if (strncmp(name,"spdif",5) == 0) {
			dev->status_led_mask = state ? (dev->status_led_mask | dtb->led_dots[LED_DOT4_SPDIF]) : (dev->status_led_mask & ~dtb->led_dots[LED_DOT4_SPDIF]);
		} else if (strncmp(name,"colon",5) == 0) {
			dev->status_led_mask = state ? (dev->status_led_mask | dtb->led_dots[LED_DOT4_SEC]) : (dev->status_led_mask & ~dtb->led_dots[LED_DOT4_SEC]);
		} else if (strncmp(name,"hdmi",4) == 0) {
			dev->status_led_mask = state ? (dev->status_led_mask | dtb->led_dots[LED_DOT4_HDMI]) : (dev->status_led_mask & ~dtb->led_dots[LED_DOT4_HDMI]);
		} else if (strncmp(name,"cvbs",4) == 0) {
			dev->status_led_mask = state ? (dev->status_led_mask | dtb->led_dots[LED_DOT4_AV]) : (dev->status_led_mask & ~dtb->led_dots[LED_DOT4_AV]);
		}
		break;
	}
}

static size_t read_data(unsigned char *data, size_t length)
{
	protocol->write_byte(FD628_KEY_RDCMD);
	return protocol->read_data(data, length) == 0 ? length : -1;
}

extern void transpose8rS64(unsigned char* A, unsigned char* B);

static size_t write_data(const unsigned char *_data, size_t length)
{
	size_t i;
	struct fd628_dtb_config *dtb = &dev->dtb_active;
	unsigned short *data = (unsigned short *)_data;

	memset(dev->wbuf, 0x00, sizeof(dev->wbuf));
	length = length > ram_size ? ram_grid_count : (length / sizeof(unsigned short));
	if (data[0] & ledDots[LED_DOT_SEC]) {
		data[0] &= ~ledDots[LED_DOT_SEC];
		data[0] |= dtb->led_dots[LED_DOT_SEC];
	}
	// Apply LED indicators mask (usb, eth, wifi etc.)
	data[0] |= dev->status_led_mask;

	switch (dtb->display.type) {
	case DISPLAY_TYPE_5D_7S_NORMAL:
	case DISPLAY_TYPE_5D_7S_T95:
	case DISPLAY_TYPE_5D_7S_X92:
	case DISPLAY_TYPE_5D_7S_ABOX:
	case DISPLAY_TYPE_4D_7S_COL:
	case DISPLAY_TYPE_5D_7S_M9_PRO:
	default:
		for (i = 0; i < length; i++)
			dev->wbuf[dtb->dat_index[i]] = data[i];
		break;
	case DISPLAY_TYPE_FD620_REF:
		for (i = 1; i < length; i++)
			dev->wbuf[dtb->dat_index[i]] = data[i];
		if (data[0] & dtb->led_dots[LED_DOT_SEC])
			dev->wbuf[dtb->dat_index[0]] |= 0x80;				// DP is the colon.
		break;
	}

	if (dtb->display.flags & DISPLAY_FLAG_TRANSPOSED) {
		unsigned char trans[8];
		length = ram_grid_count;
		memset(trans, 0, sizeof(trans));
		for (i = 0; i < length; i++)
			trans[i] = (unsigned char)dev->wbuf[i] << 1;
		transpose8rS64(trans, trans);
		memset(dev->wbuf, 0x00, sizeof(dev->wbuf));
		for (i = 0; i < ram_grid_count; i++)
			dev->wbuf[i] = trans[i+1];
	}

	switch (dtb->display.controller) {
	case CONTROLLER_FD628:
		// Memory map:
		// S1 S2 S3 S4 S5 S6 S7 S8 S9 S10 xx S12 S13 S14 xx xx
		// b0 b1 b2 b3 b4 b5 b6 b7 b0 b1  b2 b3  b4  b5  b6 b7
		for (i = 0; i < length; i++)
			dev->wbuf[i] |= (dev->wbuf[i] & 0xFC00) << 1;
		break;
	case CONTROLLER_FD620:
		// Memory map:
		// S1 S2 S3 S4 S5 S6 S7 xx xx xx xx xx xx S8 xx xx
		// b0 b1 b2 b3 b4 b5 b6 b7 b0 b1 b2 b3 b4 b5 b6 b7
		for (i = 0; i < length; i++)
			dev->wbuf[i] |= (dev->wbuf[i] & 0x80) ? 0x2000 : 0;
		break;
	case CONTROLLER_TM1618:
		// Memory map:
		// S1 S2 S3 S4 S5 xx xx xx xx xx xx S12 S13 S14 xx xx
		// b0 b1 b2 b3 b4 b5 b6 b7 b0 b1 b2 b3  b4  b5  b6 b7
		for (i = 0; i < length; i++)
			dev->wbuf[i] |= (dev->wbuf[i] & 0xE0) << 6;
		break;
	case CONTROLLER_HBS658: {
		// Memory map:
		// S1 S2 S3 S4 S5 S6 S7 xx
		// b0 b1 b2 b3 b4 b5 b6 b7
			unsigned char *tempBuf = (unsigned char *)dev->wbuf;
			for (i = 1; i < length; i++)
				tempBuf[i] = (unsigned char)(dev->wbuf[i] & 0xFF);
		}
		break;
	}

	length *= ram_grid_size;
	return fd628_write_data(0, (unsigned char *)dev->wbuf, length) == 0 ? length : 0;
}
