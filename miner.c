
/*
 * Copyright 2010 Jeff Garzik
 *           2011 Nils Schneider
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option) 
 * any later version.  See COPYING for more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <pthread.h>
#include <getopt.h>
#include <jansson.h>
#include <inttypes.h>
#include "miner.h"

#include "findnonce.h"
#include "ocl.h"

#define VERSION "0.1"

#define PROGRAM_NAME		"oclminer"
#define DEF_RPC_URL		"http://127.0.0.1:8332/"
#define DEF_RPC_USERPASS	"rpcuser:rpcpass"

enum {
	STAT_SLEEP_INTERVAL		= 1,
	STAT_CTR_INTERVAL		= 10000000,
	FAILURE_INTERVAL		= 30,
};

int opt_debug = false;
int opt_protocol = false;
int opt_ndevs = false;
static int opt_retries = 10;
static bool program_running = true;
static const bool opt_time = true;
static int opt_n_threads = 1;
static char *rpc_url = DEF_RPC_URL;
static char *userpass = DEF_RPC_USERPASS;

int block = 0;

double hashrates[16];
_clState *clStates[16];

struct option_help {
	const char	*name;
	const char	*helptext;
};

static struct option_help options_help[] = {
	{ "help",
	  "(-h) Display this help text" },

	{ "ndevs",
	  "(-n) Display number of detected GPUs" },

	{ "debug",
	  "(-D) Enable debug output (default: off)" },

	{ "protocol-dump",
	  "(-P) Verbose dump of protocol-level activities (default: off)" },

	{ "retries N",
	  "(-r N) Number of times to retry, if JSON-RPC call fails\n"
	  "\t(default: 10; use -1 for \"never\")" },

	{ "url URL",
	  "URL for bitcoin JSON-RPC server "
	  "(default: " DEF_RPC_URL ")" },

	{ "userpass USERNAME:PASSWORD",
	  "Username:Password pair for bitcoin JSON-RPC server "
	  "(default: " DEF_RPC_USERPASS ")" },
};

static struct option options[] = {
	{ "help", 0, NULL, 'h' },
	{ "debug", 0, NULL, 'D' },
	{ "protocol-dump", 0, NULL, 'P' },
	{ "retries", 1, NULL, 'r' },
	{ "url", 1, NULL, 1001 },
	{ "userpass", 1, NULL, 1002 },
	{ "ndevs", 0, NULL, 'n' },
	{ }
};

struct work_t {
	unsigned char	data[128];
	unsigned char	hash1[64];
	unsigned char	midstate[32];
	unsigned char	target[32];

	unsigned char	hash[32];
	uint32_t		output[MAXTHREADS];
	uint32_t		res_nonce;
	bool			valid;
	uint32_t		ready;
	dev_blk_ctx		blk;
};

static bool jobj_binary(const json_t *obj, const char *key,
			void *buf, size_t buflen)
{
	const char *hexstr;
	json_t *tmp;

	tmp = json_object_get(obj, key);
	if (!tmp) {
		fprintf(stderr, "JSON key '%s' not found\n", key);
		return false;
	}
	hexstr = json_string_value(tmp);
	if (!hexstr) {
		fprintf(stderr, "JSON key '%s' is not a string\n", key);
		return false;
	}
	if (!hex2bin(buf, hexstr, buflen))
		return false;

	return true;
}

static bool work_decode(const json_t *val, struct work_t *work)
{
	if (!jobj_binary(val, "midstate",
			 work->midstate, sizeof(work->midstate))) {
		fprintf(stderr, "JSON inval midstate\n");
		goto err_out;
	}

	if (!jobj_binary(val, "data", work->data, sizeof(work->data))) {
		fprintf(stderr, "JSON inval data\n");
		goto err_out;
	}

	if (!jobj_binary(val, "hash1", work->hash1, sizeof(work->hash1))) {
		fprintf(stderr, "JSON inval hash1\n");
		goto err_out;
	}

	if (!jobj_binary(val, "target", work->target, sizeof(work->target))) {
		fprintf(stderr, "JSON inval target\n");
		goto err_out;
	}

	memset(work->hash, 0, sizeof(work->hash));

	return true;

err_out:
	return false;
}

static void submit_work(struct work_t *work)
{
	char *hexstr = NULL;
	json_t *val, *res;
	char s[345];

	printf("PROOF OF WORK FOUND?  submitting...\n");

	/* build hex string */
	hexstr = bin2hex(work->data, sizeof(work->data));
	if (!hexstr)
		goto out;

	/* build JSON-RPC request */
	sprintf(s,
	      "{\"method\": \"getwork\", \"params\": [ \"%s\" ], \"id\":1}\r\n",
		hexstr);

	if (opt_debug)
		fprintf(stderr, "DBG: sending RPC call:\n%s", s);

	/* issue JSON-RPC request */
	val = json_rpc_call(rpc_url, userpass, s);
	if (!val) {
		fprintf(stderr, "submit_work json_rpc_call failed\n");
		goto out;
	}

	res = json_object_get(val, "result");

	printf("PROOF OF WORK RESULT: %s\n",
		json_is_true(res) ? "true (yay!!!)" : "false (booooo)");

	json_decref(val);

out:
	free(hexstr);
}

static void hashmeter(int thr_id, struct timeval *tv_start,
		      unsigned long hashes_done)
{
	struct timeval tv_end, diff;
	double khashes, secs;

	gettimeofday(&tv_end, NULL);

	timeval_subtract(&diff, &tv_end, tv_start);

	khashes = hashes_done / 1000.0;
	secs = (double)diff.tv_sec + ((double)diff.tv_usec / 1000000.0);

	hashrates[thr_id] = khashes / secs;
}

static void print_hashmeter(double hashrate, char *rates) {
	printf("\r                                                                            \rHashMeter: %.2f Mhash/sec (%s)", hashrate / 1000, rates);
	fflush(stdout);
}


static bool getwork(struct work_t *work) {
	static const char *rpc_req = "{\"method\": \"getwork\", \"params\": [], \"id\":0}\r\n";
	json_t *val;
	bool rc;

	/* obtain new work from bitcoin */
	val = json_rpc_call(rpc_url, userpass, rpc_req);
	if (!val) {
		fprintf(stderr, "json_rpc_call failed, ");
		return false;
	}

	/* decode result into work state struct */
	rc = work_decode(json_object_get(val, "result"), work);
	if (!rc) {
		fprintf(stderr, "JSON-decode of work failed, ");
		return false;
	}

	json_decref(val);

	return true;
} 

static void submit_nonce(struct work_t *work, uint32_t nonce) {
       work->data[64+12+0] = (nonce>>0) & 0xff;
       work->data[64+12+1] = (nonce>>8) & 0xff;
       work->data[64+12+2] = (nonce>>16) & 0xff;
       work->data[64+12+3] = (nonce>>24) & 0xff;
       submit_work(work);
}

static void *miner_thread(void *thr_id_int)
{
	int thr_id = (unsigned long) thr_id_int;
	int failures = 0;

	uint32_t res[MAXTHREADS];

	size_t globalThreads[1];
	size_t localThreads[1];

	cl_int status;

	_clState *clState = clStates[thr_id];

	status = clSetKernelArg(clState->kernel, 0,  sizeof(cl_mem), (void *)&clState->inputBuffer);
	if(status != CL_SUCCESS) { printf("Error: Setting kernel argument 1.\n"); return false; }

	status = clSetKernelArg(clState->kernel, 1,  sizeof(cl_mem), (void *)&clState->outputBuffer);
	if(status != CL_SUCCESS) { printf("Error: Setting kernel argument 2.\n"); return false; }

	struct work_t *work;
	work = malloc(sizeof(struct work_t)*2);

	work[0].ready = 0;
	work[1].ready = 0;

	int frame = 0;
	int res_frame = 0;
	int my_block = block;
	bool need_work = true;
	unsigned long hashes_done;
	hashes_done = 0;

	while (1) {
		struct timeval tv_start;
		bool rc;

		if (need_work || my_block != block) {
			frame++;
			frame %= 2;

			if (opt_debug)
				fprintf(stderr, "getwork\n");

			rc = getwork(&work[frame]);

			if (!rc) {
				fprintf(stderr, "getwork failed, ");

				if ((opt_retries >= 0) && (++failures > opt_retries)) {
					fprintf(stderr, "terminating thread\n");
					return NULL;	/* exit thread */
				}

				/* pause, then restart work loop */
				fprintf(stderr, "retry after %d seconds\n", FAILURE_INTERVAL);
				sleep(FAILURE_INTERVAL);
				continue;
			}

			precalc_hash(&work[frame].blk, (uint32_t *)(work[frame].midstate), (uint32_t *)(work[frame].data + 64));

			work[frame].blk.nonce = 0;
			work[frame].valid = true;
			work[frame].ready = 0;
			
			my_block = block;
			need_work = false;
		}

		if (hashes_done != 0)
			hashmeter(thr_id, &tv_start, hashes_done);

		gettimeofday(&tv_start, NULL);
	
		int threads = 102400 * 4;
		globalThreads[0] = threads;
		localThreads[0] = 128;

		status = clEnqueueWriteBuffer(clState->commandQueue, clState->inputBuffer, CL_TRUE, 0,
				sizeof(dev_blk_ctx), (void *)&work[frame].blk, 0, NULL, NULL);
		if(status != CL_SUCCESS) { printf("Error: clEnqueueWriteBuffer failed.\n"); return 0; }

        clFinish(clState->commandQueue);

        status = clEnqueueNDRangeKernel(clState->commandQueue, clState->kernel, 1, NULL, 
				globalThreads, localThreads, 0,  NULL, NULL);
		if(status != CL_SUCCESS) { printf("Error: Enqueueing kernel onto command queue. (clEnqueueNDRangeKernel)\n"); return 0; }

        clFlush(clState->commandQueue);

		hashes_done = 1024 * threads;

		if (work[res_frame].ready) {
			rc = false;

			uint32_t bestG = ~0;
			uint32_t nonce;
			int j;
			for(j = 0; j < work[res_frame].ready; j++) {
				if(res[j]) { 
					uint32_t start = (work[res_frame].res_nonce + j)<<10;
					uint32_t my_g, my_nonce;
					my_g = postcalc_hash(&work[res_frame].blk, start, start + 1024, &my_nonce);
					if (opt_debug)
						fprintf(stderr, "DEBUG: H0 within %u .. %u, best G = %08x, nonce = %08x\n", start, start + 1024, my_g, my_nonce);
					if(my_g < bestG) {
						bestG = my_g;
						nonce = my_nonce;
						if (opt_debug)
							fprintf(stderr, "new best\n");
					}       

					rc = true;
				}       
			}       
			
			work[res_frame].ready = false;

			uint32_t *target = (uint32_t *)(work[res_frame].target + 24);

			if(rc && bestG <= *target) {
				printf("Found solution for %08x: %08x %u\n", *target, bestG, nonce);

				submit_nonce(&work[res_frame], nonce);

				block++;

				need_work = true;
			}
		}

        status = clEnqueueReadBuffer(clState->commandQueue, clState->outputBuffer, CL_TRUE, 0, 
				sizeof(uint32_t) * threads, res, 0, NULL, NULL);   
        if(status != CL_SUCCESS) { printf("Error: clEnqueueReadBuffer failed. (clEnqueueReadBuffer)\n"); return 0; }

		res_frame = frame;
		work[res_frame].ready = threads;
		work[res_frame].res_nonce = work[res_frame].blk.nonce;

		work[frame].blk.nonce += threads;

		if (work[frame].blk.nonce > 4000000 - threads)
			need_work = true;

		failures = 0;
	}

	return NULL;
}

static void show_usage(void)
{
	int i;

	printf("oclminer version %s\n\n", VERSION);
	printf("Usage:\tminerd [options]\n\nSupported options:\n");
	for (i = 0; i < ARRAY_SIZE(options_help); i++) {
		struct option_help *h;

		h = &options_help[i];
		printf("--%s\n%s\n\n", h->name, h->helptext);
	}

	exit(1);
}

static void parse_arg (int key, char *arg)
{
	int v, i;

	switch(key) {
	case 'n':
		opt_ndevs = true;
		break;
	case 'D':
		opt_debug = true;
		break;
	case 'P':
		opt_protocol = true;
		break;
	case 'r':
		v = atoi(arg);
		if (v < -1 || v > 9999)	/* sanity check */
			show_usage();

		opt_retries = v;
		break;
	case 1001:			/* --url */
		if (strncmp(arg, "http://", 7) &&
		    strncmp(arg, "https://", 8))
			show_usage();

		rpc_url = arg;
		break;
	case 1002:			/* --userpass */
		if (!strchr(arg, ':'))
			show_usage();

		userpass = arg;
		break;
	default:
		show_usage();
	}
}

static void parse_cmdline(int argc, char *argv[])
{
	int key;

	while (1) {
		key = getopt_long(argc, argv, "DPh?n", options, NULL);
		if (key < 0)
			break;

		parse_arg(key, optarg);
	}
}

int main (int argc, char *argv[])
{
	int i, nDevs;

	/* parse command line */
	parse_cmdline(argc, argv);

	nDevs = clDevicesNum();

	if (opt_ndevs) {
		printf("%i\n", nDevs);
		return nDevs;
	}

	char name[32];

	memset(hashrates, 0, sizeof(hashrates));

	/* start mining threads */
	for (i = 0; i < nDevs; i++) {
		pthread_t t;

		printf("Init GPU %i\n", i);
		clStates[i] = initCl(i, name, sizeof(name));
		printf("initCl() finished. Found %s\n", name);

		if (pthread_create(&t, NULL, miner_thread,
				   (void *)(unsigned long) i)) {
			fprintf(stderr, "thread %d create failed\n", i);
			return 1;
		}

		sleep(1);	/* don't pound RPC server all at once */
	}

	fprintf(stderr, "%d miner threads started\n", i);

	/* main loop */
	while (program_running) {
		sleep(STAT_SLEEP_INTERVAL);
		double hashrate = 0;
		char rates[128];
		char buffer[16];
		rates[0] = 0;

		for(i = 0; i < nDevs; i++) {
			hashrate += hashrates[i];
			sprintf(buffer, "%.02f", hashrates[i] / 1000);
			strcat(rates, buffer);

			if (i != nDevs-1) strcat(rates, " ");
		}

		print_hashmeter(hashrate, rates);

	}

	return 0;
}

