#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <errno.h>

#ifdef __linux__
#include <sched.h>
#endif

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
								  (unsigned char *) buffer, sizeof(buffer), IO_TIMEOUT);
	if (ret < 0) {
		fprintf(stderr,"%s: set_sample_rate failed: %d\n", prgname, ret);
		return ret;
	}

	return 0;
}

#define OZY_BUFFER_SIZE 512

#define PACKET_NSAMPLES	126
#define RB_NPACKETS		(16*2048)	// ~16.5 MB

float ringbuf[RB_NPACKETS][PACKET_NSAMPLES];
int rb_wpos = 0;
int rb_rpos = 0;
pthread_mutex_t rb_pos_m;
pthread_cond_t rb_wpos_c;

void *ringbuf_to_stdout_loop(void *unused)
{
	while (1) {
		/* wait for work */
		pthread_mutex_lock(&rb_pos_m);
		while (rb_rpos == rb_wpos)
			pthread_cond_wait(&rb_wpos_c, &rb_pos_m);
		pthread_mutex_unlock(&rb_pos_m);

		const int len = sizeof(ringbuf[0]);
		if (write(1, (char *) ringbuf[rb_rpos], len) != len) {
			fprintf(stderr, "%s: write didn't work out as expected\n", prgname);
			return NULL;
		}

		pthread_mutex_lock(&rb_pos_m);
		rb_rpos = (rb_rpos + 1) % RB_NPACKETS;
		pthread_mutex_unlock(&rb_pos_m);
	}
}

float *get_rb_write_ptr()
{
	int local_rpos;
	pthread_mutex_lock(&rb_pos_m);
	local_rpos = rb_rpos;
	pthread_mutex_unlock(&rb_pos_m);

	if ((rb_wpos + 1) % RB_NPACKETS == local_rpos)
		return NULL;
	else
		return (float *) ringbuf[rb_wpos];
}

void rb_commit_pkt_samples()
{
	pthread_mutex_lock(&rb_pos_m);
	rb_wpos = (rb_wpos + 1) % RB_NPACKETS;
	pthread_cond_signal(&rb_wpos_c);
	pthread_mutex_unlock(&rb_pos_m);
}

// lpthread_cond_broadcast

float smp_conv(char *p)
{
	return (float) ((int) ((signed char) p[0] << 16
						   | (unsigned char) p[1] << 8
						   | (unsigned char) p[2])) / 8388607.0;
}


void ozy_packet(char *packet)
{
	assert(PACKET_NSAMPLES*4 + 8 == OZY_BUFFER_SIZE);

	char *p = packet;
	int i;

#define SYNC 0x7F
	if (*p++ != SYNC || *p++ != SYNC || *p++ != SYNC) {
		fprintf(stderr, "%s: no sync, dropping packet\n", prgname);
		return;
	}

	if ((*p) & 0x80)
		fprintf(stderr, "%s: widget reports overflow\n", prgname);
	p += 5;

	float *samples = get_rb_write_ptr();

	static int overflow_counter = 0;
	if (!samples) {
		if (overflow_counter == 0)
			fprintf(stderr, "%s: overflow occurred, dropping packet\n", prgname);
		else if (overflow_counter % 1000 == 0)
			fprintf(stderr, "%s: overflow occurred, dropping packet (x%d)\n",
					prgname, overflow_counter);
		overflow_counter++;
		return;
	} else {
		overflow_counter = 0;
	}

	for (i = 0; i < PACKET_NSAMPLES/2; i++) {
		samples[i * 2] = smp_conv(&p[0]);
		samples[i * 2 + 1] = smp_conv(&p[3]);

		p += 8;
	}

	rb_commit_pkt_samples();
}

void usage()
{
	fprintf(stderr, "%s: usage: %s [-r SAMP_RATE]\n", prgname, prgname);
	exit(1);
}

int main(int argc, char * const argv[])
{
	libusb_device_handle *handle;
	pthread_t writer_thread;
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

#ifdef __linux__
	struct sched_param sched_param;
	sched_param.sched_priority = 20;
	if (sched_setscheduler(0, SCHED_FIFO, &sched_param) < 0)
		fprintf(stderr, "%s: failed to set real-time scheduling policy: %s\n",
				prgname, strerror(errno));
#endif

	pthread_mutex_init(&rb_pos_m, NULL);
	pthread_cond_init(&rb_wpos_c, NULL);

	ret = pthread_create(&writer_thread, NULL, ringbuf_to_stdout_loop, NULL);
	if (ret) {
		fprintf(stderr, "%s: failed to create thread\n", prgname);
		return 1;
	}

	{
		int bytes;
		unsigned char buffer[OZY_BUFFER_SIZE * 16];
		ret = libusb_bulk_transfer(handle, 0x86, buffer, sizeof(buffer),
								   &bytes, IO_TIMEOUT);
		if (ret < 0) {
			fprintf(stderr, "%s: bulk transfer failed: %d\n", prgname, ret);
			return 1;
		}
	}

	while (1) {
		int bytes;
		char buffer[OZY_BUFFER_SIZE * 16];

		ret = libusb_bulk_transfer(handle, 0x86, (unsigned char *) buffer,
								   sizeof(buffer), &bytes, IO_TIMEOUT);

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
