#ifndef __MINER_H__
#define __MINER_H__
#include "ocl.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

extern int opt_protocol;

extern json_t *json_rpc_call(const char *url, const char *userpass,
                 const char *rpc_req);
extern char *bin2hex(unsigned char *p, size_t len);
extern int hex2bin(unsigned char *p, const char *hexstr, size_t len);

extern int timeval_subtract (struct timeval *result, struct timeval *x, struct timeval *y);

#endif /* __MINER_H__ */
