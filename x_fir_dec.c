#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <complex.h>

#include <volk/volk.h>

char const *prgname;

float hann(int i, int N) {
	float k = ((float) i + 1) / ((float) N + 1);
	return pow(sin(M_PI*k), 2);
}

float *lowpass_coeffs(int *ntaps, int samp_rate, char const *desc) {
	float f_c, wn_c;
	float *taps;

	if (sscanf(desc, "lp:%d:%f", ntaps, &f_c) != 2) {
		fprintf(stderr, "%s: failed to understand '%s'\n",
				prgname, desc);
		exit(1);
	}

	wn_c = 2.f*M_PI*f_c/samp_rate;
	taps = (float *) calloc(*ntaps, sizeof(float));
	for (int i = 0; i < *ntaps; i++) {
		int a_ = i*2 - (*ntaps - 1);
		float a = ((float) a_) / 2;
		taps[i] = hann(i,*ntaps)*(a_!=0 ? sin(a*wn_c)/a/M_PI : wn_c/M_PI);
	}

	return taps;
}

float *read_taps(int *ntaps, char const *filename) {
	size_t tapsmax = 1024;
	float *taps = NULL;
	FILE *fhandle = NULL;
	*ntaps = 0;

	taps = (float *) calloc(tapsmax, sizeof(float));

	if (!taps)
		goto fail;

	fhandle = fopen(filename, "r");

	if (!fhandle)
		goto fail;

	char line[128];
	while (fgets(line, sizeof(line), fhandle)) {
		*ntaps += 1;

		if (*ntaps > tapsmax) {
			tapsmax *= 2;
			float *newtaps = realloc(taps, sizeof(float) * tapsmax);
			if (!newtaps)
				goto fail;
			taps = newtaps;
		}

		taps[*ntaps - 1] = atof(line);
	}

	if (!feof(fhandle)) {
		fprintf(stderr, "%s: read_taps: read error\n", prgname);
		free(taps);
		fclose(fhandle);
		return NULL;
	}

	fclose(fhandle);

	/* FIXME or not */
	return realloc(taps, sizeof(float) * *ntaps);

fail:
	fprintf(stderr, "%s: read_taps: %s\n", prgname, strerror(errno));

	free(taps);
	if (fhandle)
		fclose(fhandle);
	return NULL;
}

/* plagiarized from plan9port */
long readn(int f, void *av, long n)
{
	char *a;
	long m, t;

	a = av;
	t = 0;
	while(t < n){
		m = read(f, a+t, n-t);
		if(m <= 0){
			if(t == 0)
				return m;
			break;
		}
		t += m;
	}
	return t;
}

void usage()
{
	fprintf(stderr, "%s: usage: %s [-b OUT_BUF_LEN] SAMP_RATE"
			" CENTER_FREQ DECIMATION TAPS_FILE\n", prgname, prgname);
	exit(1);
}

int main(int argc, char * const argv[])
{
	double samp_rate, center_freq;
	int decimation, ntaps;
	float *taps;

	prgname = argv[0];

	int opt;
	size_t outbuflen = 4096;
	while ((opt = getopt(argc, argv, "b:")) != -1) {
		switch (opt) {
			case 'b':
				outbuflen = atoi(optarg);
				break;
			default:
				usage();
		}
	}

	/* avoid volk's noise on stdout */
	int fdout = dup(1);
	close(1);
	open("/dev/null", O_WRONLY);

	if (argc - optind != 4)
		usage();

	samp_rate = atof(argv[optind++]);
	center_freq = atof(argv[optind++]);
	decimation = atoi(argv[optind++]);

	if (samp_rate <= 0 /* || center_freq <= 0 */ || decimation <= 0) {
		fprintf(stderr, "%s: bad SAMP_RATE, CETNER_FREQ or DECIMATION\n", prgname);
		return 1;
	}

	char *taps_fn = argv[optind++];
	if (strncmp(taps_fn, "lp:", 3) != 0)
		taps = read_taps(&ntaps, taps_fn);
	else
		taps = lowpass_coeffs(&ntaps, samp_rate, taps_fn);

	if (!taps)
		return 1;

	if (ntaps < decimation)
		fprintf(stderr, "%s: number of taps (%d) must be greater "
						"than decimation rate (%d)\n", prgname, ntaps, decimation);

	/* FIXME or not */
	complex float *ctaps = (complex float *) calloc(ntaps, sizeof(float complex));

	double complex w = cexp(-I * 2 * M_PI * (((float) center_freq) / samp_rate));

	int x;
	double complex rot = 1;
	for (x = 0; x < ntaps; x++) {
		ctaps[x] = rot * taps[x];
		rot *= w;
	}

	float complex phase = 1;
	float complex wdec = cpowf(w, decimation);

	float complex *viabuf = (float complex *) calloc(outbuflen, sizeof(float complex));
	float complex *outbuf = (float complex *) calloc(outbuflen, sizeof(float complex));

	size_t inbuflen = decimation * (outbuflen - 1) + ntaps;
	float complex *inbuf = (float complex *) calloc(inbuflen, sizeof(float complex));

	if (!(viabuf && outbuf && inbuf)) {
		errno = ENOMEM;
		goto fail;
	}

	long ret = 0;

	while (1) {
		if (!ret) { /* is this the first read? */
			ret = readn(0, inbuf, inbuflen * sizeof(float complex));

			if (ret < 0)
				goto fail;
		} else {
			long overlap = ntaps - decimation;

			memcpy(inbuf, inbuf + inbuflen - overlap, overlap * sizeof(float complex));
			ret = readn(0, inbuf + overlap, (inbuflen - overlap) * sizeof(float complex));

			if (ret < 0)
				goto fail;

			ret += overlap * sizeof(float complex);
		}

		ret /= sizeof(float complex);

		long i, o;
		for (i = o = 0; i <= ret - ntaps; i += decimation, o++)
			volk_32fc_x2_dot_prod_32fc(viabuf + o, inbuf + i, ctaps, ntaps);

		volk_32fc_s32fc_x2_rotator_32fc(outbuf, viabuf, wdec, &phase, o);
		phase /= cabs(phase);

		if (write(fdout, outbuf, o * sizeof(float complex)) != o * sizeof(float complex))
			goto fail;

		if (ret != inbuflen)
			break;
	}

cleanup:
	free(inbuf);
	free(outbuf);
	free(taps);
	free(ctaps);

	return 0;

fail:
	fprintf(stderr, "%s: %s\n", prgname, strerror(errno));

	goto cleanup;
}

