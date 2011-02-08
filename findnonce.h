#define MAXTHREADS 2000000

#ifdef __APPLE_CC__
#include <OpenCL/opencl.h>
#else
#include <CL/cl.h>
#endif

typedef struct {
    cl_uint ctx_a; cl_uint ctx_b; cl_uint ctx_c; cl_uint ctx_d;
    cl_uint ctx_e; cl_uint ctx_f; cl_uint ctx_g; cl_uint ctx_h;
    cl_uint cty_a; cl_uint cty_b; cl_uint cty_c; cl_uint cty_d;
    cl_uint cty_e; cl_uint cty_f; cl_uint cty_g; cl_uint cty_h;
    cl_uint merkle; cl_uint ntime; cl_uint nbits; cl_uint nonce;
	cl_uint fW0; cl_uint fW1; cl_uint fW2; cl_uint fW3; cl_uint fW15;
	cl_uint fW01r; cl_uint fcty_e; cl_uint fcty_e2;
} dev_blk_ctx;

struct work_t {
	unsigned char	data[128];
	unsigned char	hash1[64];
	unsigned char	midstate[32];
	unsigned char	target[32];

	unsigned char	hash[32];
	uint32_t		output[MAXTHREADS];
	uint32_t		res_nonce;
	uint32_t		valid;
	uint32_t		ready;
	dev_blk_ctx		blk;
};

extern void precalc_hash(dev_blk_ctx *blk, uint32_t *state, uint32_t *data);
extern uint32_t postcalc_hash(dev_blk_ctx *blk, struct work_t *work, uint32_t start, uint32_t end, uint32_t *best_nonce, int pool_mode, unsigned int *h0count);
extern void submit_nonce(struct work_t *work, uint32_t nonce);
