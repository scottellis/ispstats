/*
 *  
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>

#include "isp_user.h"

int nbins = 256;
int nframes = 4;
int show_bins = 0;
unsigned long gain = 0x20;

static int msleep(int milliseconds)
{
        struct timespec ts;

        if (milliseconds < 1) {
                return -2;
        }

        ts.tv_sec = milliseconds / 1000;
        ts.tv_nsec = 1000000 * (milliseconds % 1000);

        return nanosleep(&ts, NULL);
}

static int xioctl(int fd, int request, void *arg)
{
	int r;

	do {
		r = ioctl(fd, request, arg);
	} while (-1 == r && EINTR == errno);

	return r;
}

static int enable_histogram(int fd)
{
	struct isp_hist_config cfg;
	unsigned int x_start, x_end;
	unsigned int y_start, y_end;
	
	memset(&cfg, 0, sizeof(cfg));

	cfg.hist_source = 0; 		// CCDC is the source
	cfg.input_bit_width = 10;

	cfg.hist_frames = nframes;

	/* parameters if hist_source was MEMORY
	cfg.hist_h_v_info = 0;
	cfg.hist_radd = 0;
	cfg.hist_radd_off = 0;
	*/

	/* BINS_32, BINS_64, BINS_128, BINS_256 */
	switch (nbins) {
	case 32:
		cfg.hist_bins = BINS_32;
		break;
	case 64:
		cfg.hist_bins = BINS_64;
		break;

	case 128:
		cfg.hist_bins = BINS_128;
		break;

	case 256:
		cfg.hist_bins = BINS_256;
		break;

	default:
		printf("Invalid number of bins %d\n", nbins);
		return -1;
	}

	/* 
	fixed-point 8-bit values 3Q5, 
	0x10 = 0.5 
	0x20 = 1.0 gain, 
	0x40 = 2.0
	*/
	cfg.wb_gain_R = gain;
	cfg.wb_gain_RG = gain;
	cfg.wb_gain_B = gain;
	cfg.wb_gain_BG = gain;

	/* 0 = reg0, 1 = reg0 and reg1, ..., 3 = reg0-reg3 */
	cfg.num_regions = 0;

	/* packed start [29:16] and end [13:0] pixel positions */ 
	/* choose a 400x400 pixel region in the center for stats */
	x_start = 1080;
	x_end = 1479;
	y_start = 760;
	y_end = 1159;
	cfg.reg0_hor = (x_start << 16) | x_end;
	cfg.reg0_ver = (y_start << 16) | y_end;

	/*
	x_start = 1024;
	x_end = 1536;
	y_start = 1024;
	y_end = 1536;
	cfg.reg1_hor = (x_start << 16) | x_end;
	cfg.reg1_ver = (y_start << 16) | y_end;

	x_start = 0;
	x_end = 512;
	y_start = 1024;
	y_end = 1536;
	cfg.reg0_hor = (x_start << 16) | x_end;
	cfg.reg0_ver = (y_start << 16) | y_end;

	x_start = 1024;
	x_end = 1536;
	y_start = 0;
	y_end = 512;
	cfg.reg1_hor = (x_start << 16) | x_end;
	cfg.reg1_ver = (y_start << 16) | y_end;
	*/

	if (-1 == xioctl(fd, VIDIOC_PRIVATE_ISP_HIST_CFG, &cfg)) {
		perror("VIDIOC_PRIVATE_ISP_HIST_CFG");
		return -1;
	}
	
	return 0;
}

static void dump_hist_stats_one_component(int index, unsigned int *d, int nbins)
{
	int i, j;
	unsigned int half, median_bin;
	unsigned int sum = 0;

	for (i = 0; i < nbins; i++)
		sum += d[i];

	half = 0;

	for (i = 0; i < nbins; i++) {
		half += d[i];
	
		if (half > sum / 2)
			break;	
	}

	median_bin = i;

	printf("Component[%d]: median-bin %u\n", index, median_bin);

	if (show_bins) {
		for (i = 0; i < nbins; i += 8) {
			printf("\n%3d: ", i % nbins);

			for (j = 0; j < 8; j++)
				printf("%8u", d[i+j]);
		}
	
		printf("\n");
	}
}	

static void dump_hist_summary(unsigned int *d, int nbins)
{
	int i, j;
	unsigned int half;
	unsigned int median_bin[4];
	unsigned int sum;
	unsigned int *p;
	double avg;

	avg = 0.0;
	for (j = 0; j < 4; j++) {
		p = &d[j * nbins];

		sum = 0;
		half = 0;

		for (i = 0; i < nbins; i++) {
			sum += p[i];
		}

		for (i = 0; i < nbins; i++) {
			half += p[i];
		
			if (half > sum / 2)
				break;	
		}

		median_bin[j] = i;
		avg += i;
	}

	avg /= 4.0;

	printf("median-bins: %3u  %3u  %3u  %3u    avg: %3.2lf", median_bin[0],
		median_bin[1], median_bin[2], median_bin[3], avg);
}

static void dump_hist_stats(unsigned int *d, int nbins)
{
	int i;
	
	if (show_bins) {
		for (i = 0; i < 4; i++)
			dump_hist_stats_one_component(i, &d[i * nbins], nbins);
	}
	else {
		dump_hist_summary(d, nbins);
	}

	printf("\n");
}

static int read_histogram(int fd)
{	
	struct isp_hist_data hist;
	int result, i;

	if (enable_histogram(fd) < 0)
		return -1;

	hist.hist_statistics_buf = malloc(4096);

	if (!hist.hist_statistics_buf) {
		printf("memory alloc fail in read_histogram\n");
		return -1;
	}

	memset(hist.hist_statistics_buf, 0, 4096);

	/* just a WAG, the initial sleep after enabling */
	msleep(500 * nframes);

	for (i = 0; i < 10; i++) {
		result = ioctl(fd, VIDIOC_PRIVATE_ISP_HIST_REQ, &hist);

		if (!result)
			break;

		if (errno != EBUSY)
			break;
		
		if (i > 2) {
			if (i == 3)
				printf("EBUSY ...");
			else
				printf(".");
		}

		msleep(500);
	}

	if (i > 0)
		printf("\n");

	if (result)
		perror("VIDIOC_PRIVATE_ISP_HIST_REQ");
	else
		dump_hist_stats(hist.hist_statistics_buf, nbins);

	free(hist.hist_statistics_buf);

	return 0;
}

static int open_device(const char *dev_name)
{
	int fd;
	struct stat st; 

	if (-1 == stat(dev_name, &st)) {
		fprintf(stderr, "Cannot identify '%s': %d, %s\n",
			dev_name, errno, strerror (errno));
		exit(1);
	}

	if (!S_ISCHR(st.st_mode)) {
		fprintf(stderr, "%s is no device\n", dev_name);
		exit(1);
	}

	fd = open(dev_name, O_RDWR | O_NONBLOCK, 0);

	if (-1 == fd) {
		fprintf(stderr, "Cannot open '%s': %d, %s\n",
			dev_name, errno, strerror (errno));
		exit(1);
	}

	return fd;
}

static void usage(FILE *fp, char **argv)
{
	fprintf (fp,
		"Usage: %s [options]\n\n"
		"Options:\n"
		"-b<n>  num histogram bins n=32,64,128 or 256 (default = 128)\n"
		"-f<n>  num frames to collect (default = 1)\n"
		"-g<n>  gain in fixed-point 3Q5 format, (default 0x20 = gain of 1.0)\n"
		"-s     show bin data\n"
		"-h     print this message\n"
		"\n",
		argv[0]);
}

int main(int argc, char **argv)
{
	int fd, opt;
	char *endp;
	char dev_name[] = "/dev/video0";

	while ((opt = getopt(argc, argv, "b:f:g:sh")) != -1) {
		switch (opt) {
		case 'b':
			nbins = atoi(optarg);				
			break;

		case 'f':
			nframes = atoi(optarg);
			break;

		case 'g':
			gain = strtoul(optarg, &endp, 0);

			if (gain < 1 || gain > 255) {
				printf("Invalid gain %lu  Valid range 0-255\n", 
					gain);
				usage(stderr, argv);
				exit(1);
			}

			break;

		case 's':
			show_bins = 1;
			break;

		case 'h':
			usage(stdout, argv);
			exit(0);

		default:
			usage(stderr, argv);
			exit(1);
		}
	}

	fd = open_device(dev_name);

	read_histogram(fd);

	close(fd);

	return 0;
}

