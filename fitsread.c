#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fitsio.h>

int main(int argc, char const *argv[])
{
	if (argc != 2) {
		fprintf(stderr, "%s: usage: %s FITS_FILENAME\n", argv[0], argv[0]);
		return 1;
	}

	fitsfile *fptr;
	int status = 0;

	fits_open_image(&fptr, argv[1], READONLY, &status);

	int bitpix, naxis;
	long naxes[2];
	fits_get_img_param(fptr, sizeof(naxes) / sizeof(long), &bitpix, &naxis, naxes, &status);

	if (status)
		goto fail;

	if ((bitpix != FLOAT_IMG) || (naxis != 2) || (naxes[0] != 2)) {
		fprintf(stderr, "%s: %s: unexpected image parameters\n", argv[0], argv[1]);
		goto fail;
	}

	float buff[8192];

	long frmpos = 0;
	long frmlen = naxes[1];
	const long frmbuflen = sizeof(buff) / (sizeof(float) * 2);

	while (frmpos < frmlen) {
		long fpixel[2] = {1, frmpos + 1};

		long nframes = frmlen - frmpos;
		if (nframes > frmbuflen)
			nframes = frmbuflen;

		fits_read_pix(fptr, TFLOAT, fpixel, nframes * 2, NULL, buff, NULL, &status);

		if (status)
			goto fail;

		if (fwrite(buff, sizeof(float) * 2, nframes, stdout) != nframes) {
			fprintf(stderr, "%s: fwrite: %s\n", argv[0], strerror(errno));
			return errno;
		}

		frmpos += nframes;
	}

fail:
	fits_close_file(fptr, &status);

	if (status)
		fits_report_error(stderr, status);
	
	return status;
}
