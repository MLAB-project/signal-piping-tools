#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <complex.h>

#include <volk/volk.h>

char const *prg_name;

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
		fprintf(stderr, "%s: read_taps: read error\n");
		free(taps);
		fclose(fhandle);
		return NULL;
	}

	fclose(fhandle);

	/* FIXME or not */
	return realloc(taps, sizeof(float) * *ntaps);

fail:
	fprintf(stderr, "%s: read_taps: %s\n", prg_name, strerror(errno));

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

int main(int argc, char const *argv[])
{
	double samp_rate, center_freq;
	int decimation, ntaps;
	float *taps;

	prg_name = argv[0];

	/* avoid volk's noise on stdout */
	int fdout = dup(1);
	close(1);
	open("/dev/null", O_WRONLY);

	if (argc != 5) {
		fprintf(stderr, "%s: usage: %s SAMP_RATE CENTER_FREQ DECIMATION TAPS_FILE\n",
				argv[0], argv[0]);
		return 1;
	}

	samp_rate = atof(argv[1]);
	center_freq = atof(argv[2]);
	decimation = atoi(argv[3]);

	if (samp_rate <= 0 /* || center_freq <= 0 */ || decimation <= 0) {
		fprintf(stderr, "%s: bad SAMP_RATE, CETNER_FREQ or DECIMATION\n", argv[0]);
		return 1;
	}

	taps = read_taps(&ntaps, argv[4]);

	if (!taps)
		return 1;

	if (ntaps < decimation)
		fprintf(stderr, "%s: number of taps (%d) must be greater "
						"than decimation rate (%d)\n", argv[0], ntaps, decimation);

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

	size_t outbuflen = 4096;
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
	fprintf(stderr, "%s: %s\n", prg_name, strerror(errno));

	goto cleanup;
}
