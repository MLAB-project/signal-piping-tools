#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

char const *prgname;

libusb_device_handle *open_sdr_widget()
{
	libusb_device_handle *handle;
	int ret;

	handle = libusb_open_device_with_vid_pid(NULL, 0xfffe, 0x0007);
	if (handle == NULL) {
		fprintf(stderr, "%s: libusb: cannot find device "
						"with the right vid and pid\n", prgname);
		return NULL;
	}

	libusb_detach_kernel_driver(handle, 0);

	ret = libusb_claim_interface(handle, 0);
	if (ret < 0) {
		fprintf(stderr, "%s: libusbio: cannot claim interface: %d\n",
				prgname, ret);
		libusb_close(handle);
		return NULL;
	}

	return handle;
}

#define VRT_VENDOR_IN 0xC0
#define IO_TIMEOUT 500

int set_sample_rate(libusb_device_handle *handle, int rate)
{
	int speed;

	switch (rate) {
		case 48000:
			speed = 0;
			break;
		case 96000:
			speed = 1;
			break;
		case 192000:
			speed = 2;
			break;
		default:
			fprintf(stderr, "%s: invalid sample rate, "
							"48000, 96000, 192000 are valid\n", prgname);
			return -1;
	}

	int ret;
	char buffer[8];
	ret = libusb_control_transfer(handle, VRT_VENDOR_IN, 0x71, 0, speed,
								  buffer, sizeof(buffer), IO_TIMEOUT);
	if (ret < 0) {
		fprintf(stderr,"%s: set_sample_rate failed: %d\n", prgname, ret);
		return ret;
	}

	return 0;
}

#define OZY_BUFFER_SIZE 512

float smp_conv(char *p)
{
	return (float) ((int) ((signed char) p[0] << 16
						   | (unsigned char) p[1] << 8
						   | (unsigned char) p[2])) / 8388607.0;
}

#define SYNC 0x7F

void ozy_packet(char *packet)
{
	char *p = packet;
	int i;
	float samples[128];

	if (*p++ != SYNC || *p++ != SYNC || *p++ != SYNC) {
		fprintf(stderr, "%s: no sync, dropping packet\n", prgname);
		return;
	}

	/* skip uninteresting junk */
	p += 5;

	for (i = 0; i < 63; i++) {
		samples[i * 2] = smp_conv(&p[0]);
		samples[i * 2 + 1] = smp_conv(&p[3]);

		p += 8;
	}

	if (write(1, samples, sizeof(samples)) != sizeof(samples)) {
		fprintf(stderr, "%s: write didn't work out as expected\n", prgname);
		return;
	}

	fprintf(stderr, "packet\n");
}

void usage()
{
	fprintf(stderr, "%s: usage: %s [-r SAMP_RATE]\n", prgname, prgname);
	exit(1);
}

int main(int argc, char * const argv[])
{
	libusb_device_handle *handle;
	int ret;

	prgname = argv[0];

	int opt;
	int samp_rate = 192000;
	while ((opt = getopt(argc, argv, "r:")) != -1) {
		switch (opt) {
			case 'r':
				samp_rate = atoi(optarg);
				break;
			default:
				usage();
		}
	}

	ret = libusb_init(NULL);

	handle = open_sdr_widget();
	if (!handle)
		return 1;

	if (set_sample_rate(handle, samp_rate) < 0)
		return 1;

	while (1) {
		int bytes;
		unsigned char buffer[OZY_BUFFER_SIZE * 16];

		ret = libusb_bulk_transfer(handle, 0x86, buffer, sizeof(buffer),
								   &bytes, IO_TIMEOUT);

		if (ret < 0) {
			fprintf(stderr, "%s: bulk transfer failed: %d\n", prgname, ret);
			return 1;
		}

		if (bytes != sizeof(buffer)) {
			fprintf(stderr, "%s: bulk transfer not complete\n", prgname);
			return 1;
		}

		int i;
		for (i = 0; i < (sizeof(buffer) / OZY_BUFFER_SIZE); i++)
			ozy_packet(&buffer[i * OZY_BUFFER_SIZE]);
	}

	libusb_close(handle);

	return 0;
}
