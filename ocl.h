#ifndef __OCL_H__
#define __OCL_H__
#include <CL/cl.h>

typedef struct {
	cl_context context;
	cl_kernel kernel;
	cl_command_queue commandQueue;
	cl_program program;
	cl_mem inputBuffer;
	cl_mem outputBuffer;
} _clState;

extern char *file_contents(const char *filename, int *length);
extern int clDevicesNum();
extern _clState *initCl(int gpu, char *name, size_t nameSize);

#endif /* __OCL_H__ */
