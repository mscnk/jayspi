#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <glib.h>
#include <libjaylink/libjaylink.h>

#define JTAG_MAX_TRANSFER_SIZE	(UINT16_MAX / 8)

static uint32_t serial_number;
static gboolean use_serial_number;

static gboolean parse_serial_option(const gchar *option_name,
		const gchar *value, gpointer data, GError **error)
{
	int ret;

	(void)option_name;
	(void)data;
	(void)error;

	ret = jaylink_parse_serial_number(value, &serial_number);

	if (ret != JAYLINK_OK) {
		g_critical("Invalid serial number: %s.", value);
		return FALSE;
	}

	use_serial_number = TRUE;

	return TRUE;
}

static GOptionEntry entries[] = {
	{"serial", 's', 0, G_OPTION_ARG_CALLBACK, &parse_serial_option,
		"Serial number", NULL},
	{NULL, 0, 0, 0, NULL, NULL, NULL}
};

static gboolean parse_options(int *argc, char ***argv)
{
	GError *error;
	GOptionContext *context;

	error = NULL;

	context = g_option_context_new(NULL);
	g_option_context_add_main_entries(context, entries, NULL);

	if (!g_option_context_parse(context, argc, argv, &error)) {
		g_critical("%s.", error->message);
		g_error_free(error);
		g_option_context_free(context);
		return FALSE;
	}

	g_option_context_free(context);

	return TRUE;
}

static uint8_t reverse_byte(uint8_t x)
{
	x = ((x >> 1) & 0x55) | ((x << 1) & 0xaa);
	x = ((x >> 2) & 0x33) | ((x << 2) & 0xcc);
	x = ((x >> 4) & 0x0f) | ((x << 4) & 0xf0);

	return x;
}

static void reverse_bytes(uint8_t *dst, const uint8_t *src, size_t length)
{
	size_t i;

	for (i = 0; i < length; i++)
		dst[i] = reverse_byte(src[i]);
}

static gboolean assert_cs(struct jaylink_device_handle *devh, gboolean enable)
{
	int ret;

	if (enable)
		ret = jaylink_jtag_clear_trst(devh);
	else
		ret = jaylink_jtag_set_trst(devh);

	if (ret != JAYLINK_OK) {
		g_critical("jaylink_jtag_clear_trst() failed: %s.",
			jaylink_strerror(ret));
		return FALSE;
	}

	return TRUE;
}

static gboolean send_data(struct jaylink_device_handle *devh,
		const uint8_t *mosi, uint8_t *miso, size_t length)
{
	int ret;
	uint8_t *buffer;

	if (!assert_cs(devh, TRUE)) {
		g_critical("Failed to assert CS signal.");
		return FALSE;
	}

	buffer = malloc(length);

	if (!buffer) {
		g_critical("Failed to allocate buffer.");
		return FALSE;
	}

	reverse_bytes(buffer, mosi, length);

	ret = jaylink_jtag_io(devh, buffer, buffer, miso, length * 8,
		JAYLINK_JTAG_VERSION_2);

	free(buffer);

	if (ret != JAYLINK_OK) {
		g_critical("jaylink_jtag_io() failed: %s.",
			jaylink_strerror_name(ret));
		return FALSE;
	}

	if (!assert_cs(devh, FALSE)) {
		g_critical("Failed to de-assert CS signal.");
		return FALSE;
	}

	reverse_bytes(miso, miso, length);

	return TRUE;
}

static void log_handler(const gchar *domain, GLogLevelFlags level,
		const gchar *message, gpointer user_data)
{
	(void)domain;
	(void)user_data;

	fprintf(stderr, "%s\n", message);
	fflush(stderr);
}

int main(int argc, char **argv)
{
	ssize_t ret;
	struct jaylink_context *ctx;
	struct jaylink_device **devs;
	struct jaylink_device_handle *devh;
	uint8_t caps[JAYLINK_DEV_EXT_CAPS_SIZE];
	uint32_t interfaces;
	char *firmware_version;
	size_t length;
	gboolean device_found;
	uint32_t tmp;
	size_t i;
	uint8_t mosi[JTAG_MAX_TRANSFER_SIZE + 1];
	uint8_t miso[JTAG_MAX_TRANSFER_SIZE];
	size_t num_devices;

	use_serial_number = false;

	g_log_set_default_handler(&log_handler, NULL);

	if (!parse_options(&argc, &argv))
		return EXIT_FAILURE;

	ret = jaylink_init(&ctx);

	if (ret != JAYLINK_OK) {
		g_critical("jaylink_init() failed: %s.",
			jaylink_strerror_name(ret));
		return EXIT_FAILURE;
	}

	ret = jaylink_discovery_scan(ctx, 0);

	if (ret != JAYLINK_OK) {
		g_critical("jaylink_discovery_scan() failed: %s.",
			jaylink_strerror_name(ret));
		jaylink_exit(ctx);
		return EXIT_FAILURE;
	}

	ret = jaylink_get_devices(ctx, &devs, &num_devices);

	if (ret != JAYLINK_OK) {
		g_critical("jaylink_get_device_list() failed: %s.",
			jaylink_strerror_name(ret));
		jaylink_exit(ctx);
		return EXIT_FAILURE;
	}

	if (num_devices > 1 && !use_serial_number) {
		g_critical("Multiple devices found, use the serial number to "
			"select a specific device.");
		jaylink_free_devices(devs, true);
		jaylink_exit(ctx);
		return EXIT_FAILURE;
	}

	device_found = false;

	for (i = 0; devs[i]; i++) {
		devh = NULL;
		ret = jaylink_device_get_serial_number(devs[i], &tmp);

		if (ret != JAYLINK_OK) {
			g_warning("jaylink_device_get_serial_number() failed: "
				"%s.", jaylink_strerror_name(ret));
			continue;
		}

		if (use_serial_number && serial_number != tmp)
			continue;

		ret = jaylink_open(devs[i], &devh);

		if (ret == JAYLINK_OK) {
			serial_number = tmp;
			device_found = true;
			break;
		}

		g_critical("jaylink_open() failed: %s.",
			jaylink_strerror_name(ret));
	}

	jaylink_free_devices(devs, true);

	if (!device_found) {
		g_critical("No J-Link device found.");
		jaylink_exit(ctx);
		return EXIT_FAILURE;
	}

	ret = jaylink_device_get_serial_number(jaylink_get_device(devh),
		&tmp);

	if (ret == JAYLINK_OK)
		g_debug("S/N: %012u", tmp);

	ret = jaylink_get_firmware_version(devh, &firmware_version, &length);

	if (ret != JAYLINK_OK) {
                g_critical("jaylink_get_firmware_version() failed: %s.",
                        jaylink_strerror_name(ret));
                jaylink_close(devh);
                jaylink_exit(ctx);
                return EXIT_FAILURE;
	} else if (length > 0) {
		g_debug("Firmware: %s", firmware_version);
		free(firmware_version);
	}

	/* Ensure that chip select (CS) is not asserted. */
	if (!assert_cs(devh, false)) {
		g_critical("Failed to de-assert CS signal.");
		jaylink_close(devh);
		jaylink_exit(ctx);
		return EXIT_FAILURE;
	}

	memset(caps, 0, JAYLINK_DEV_EXT_CAPS_SIZE);

	ret = jaylink_get_caps(devh, caps);

	if (ret != JAYLINK_OK) {
		g_critical("jaylink_get_caps() failed: %s.",
			jaylink_strerror_name(ret));
		jaylink_close(devh);
		jaylink_exit(ctx);
		return EXIT_FAILURE;
	}

	if (jaylink_has_cap(caps, JAYLINK_DEV_CAP_GET_EXT_CAPS)) {
		ret = jaylink_get_extended_caps(devh, caps);

		if (ret != JAYLINK_OK) {
			g_critical("jaylink_get_extended_caps() failed: %s.",
				jaylink_strerror_name(ret));
			jaylink_close(devh);
			jaylink_exit(ctx);
			return EXIT_FAILURE;
		}
	}

	if (jaylink_has_cap(caps, JAYLINK_DEV_CAP_SELECT_TIF)) {
		ret = jaylink_get_available_interfaces(devh, &interfaces);

		if (ret != JAYLINK_OK) {
			g_critical("jaylink_get_available_interfaces() "
				"failed: %s.", jaylink_strerror(ret));
			jaylink_close(devh);
			jaylink_exit(ctx);
			return EXIT_FAILURE;
		}

		if (!(interfaces & (1 << JAYLINK_TIF_JTAG))) {
			g_critical("Device does not support JTAG.");
			jaylink_close(devh);
			jaylink_exit(ctx);
			return EXIT_FAILURE;
		}

		ret = jaylink_select_interface(devh, JAYLINK_TIF_JTAG, NULL);

		if (ret != JAYLINK_OK) {
			g_critical("jaylink_select_interface() failed: %s.",
				jaylink_strerror(ret));
			return 1;
		}
	}

	length = fread(mosi, 1, sizeof(mosi), stdin);

	if (length > JTAG_MAX_TRANSFER_SIZE) {
		g_critical("Too much input data, maximum transfer size is "
			"%zu bytes.", (size_t)JTAG_MAX_TRANSFER_SIZE);
		jaylink_close(devh);
		jaylink_exit(ctx);
		return EXIT_FAILURE;
	}


	if (!send_data(devh, mosi, miso, length)) {
		g_critical("Failed to send data.");
		jaylink_close(devh);
		jaylink_exit(ctx);
		return EXIT_FAILURE;
	}

	for (i = 0; i < length; i++)
		printf("%02x ", miso[i]);

	printf("\n");

	jaylink_close(devh);
	jaylink_exit(ctx);

	return EXIT_SUCCESS;
}
