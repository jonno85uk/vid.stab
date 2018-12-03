
#ifdef USE_OPENCL //FIXME:TEMPORARY: do in cmake
#include "clFuncs.h"
#include <stdio.h>
#include <string.h>

#define MAX_SOURCE_SIZE (0x100000)

// void vsFrameCopy(VSFrame* const dest, const VSFrame* src, const VSFrameInfo* fi) {
//   assert( (fi->planes > 0) && (fi->planes <= 4) );
//   for (int plane = 0; plane < fi->planes; plane++){
//     vsFrameCopyPlane(dest, src, fi, plane);
//   }
// }

                    //   result[i] = vals[i] * 0.25; 
const char* kernel = "__kernel void mult(__global const uint8 *vals, __global uint8 *result) { \
                      int i = get_global_id(0);\
                      if (i/2==0) { \
                          result[i] = vals[i]; \
                      } else { \
                        result[i] = 0; \
                      } \
                    }";

int initalised = 0;
cl_device_id device_id = 0;   

cl_int clInit()
{
    if (initalised != 0) {
        return CL_SUCCESS;
    }
    // Get platform and device information
    cl_platform_id platform_id = NULL;
    cl_uint ret_num_devices;
    cl_uint ret_num_platforms;
    cl_int ret = clGetPlatformIDs(1, &platform_id, &ret_num_platforms);
    if (ret == CL_SUCCESS) {
        ret = clGetDeviceIDs( platform_id, CL_DEVICE_TYPE_DEFAULT, 1, &device_id, &ret_num_devices);
        if (ret == CL_SUCCESS) {
            initalised = 1;
            // printf("%d\n", ret_num_devices);
        }
    }
    return ret;
}

cl_int clTransform(VSTransformData* const td, const VSTransform t)
{   
    // FIXME: LEAKY!
    cl_int ret = clInit();
    if (likely(ret == CL_SUCCESS)) {
        // Create an OpenCL context
        cl_context context = clCreateContext( NULL, 1, &device_id, NULL, NULL, &ret);
        // Create a command queue
        cl_command_queue command_queue = clCreateCommandQueue(context, device_id, 0, &ret);
        
        int maxSize = 0;
        for (int i = 0; i < 4; i++) {
            if (td->src.linesize[i] > maxSize) {
                maxSize = td->src.linesize[i];
            }
        }
        // Create memory buffer on the device for data
        cl_mem mem_obj = clCreateBuffer(context, CL_MEM_READ_ONLY, 
            maxSize * sizeof(uint8_t), NULL, &ret);
        cl_mem ret_mem_obj = clCreateBuffer(context, CL_MEM_READ_ONLY, 
            maxSize * sizeof(uint8_t), NULL, &ret);

        // Copy the list to the memory buffers
        ret = clEnqueueWriteBuffer(command_queue, mem_obj, CL_TRUE, 0,
            maxSize * sizeof(uint8_t), td->src.data[0], 0, NULL, NULL);
        if (ret != CL_SUCCESS) {
            printf("clEnqueueWriteBuffer\n");
            return ret;
        }
        // Create a program from the kernel source
        const size_t kernelSize = strlen(kernel);
        cl_program program = clCreateProgramWithSource(context, 1, &kernel, &kernelSize, &ret);            
        // Build the program
        ret = clBuildProgram(program, 1, &device_id, NULL, NULL, NULL);
        if (ret != CL_SUCCESS) {
            printf("clBuildProgram %d\n", ret);
            if (ret == CL_BUILD_PROGRAM_FAILURE) {
                // Determine the size of the log
                size_t log_size;
                clGetProgramBuildInfo(program, device_id, CL_PROGRAM_BUILD_LOG, 0, NULL, &log_size);

                // Allocate memory for the log
                char *log = (char *) malloc(log_size);

                // Get the log
                clGetProgramBuildInfo(program, device_id, CL_PROGRAM_BUILD_LOG, log_size, log, NULL);

                // Print the log
                printf("%s\n", log);
                free(log);
            }
            return ret;
        }

        // Create the OpenCL kernel
        cl_kernel kernel = clCreateKernel(program, "mult", &ret);
        // Set the arguments of the kernel
        ret = clSetKernelArg(kernel, 0, sizeof(cl_mem), (void *)&mem_obj);
        if (ret != CL_SUCCESS) {
            printf("clSetKernelArg1 %d\n", ret);
            return ret;
        }
        ret = clSetKernelArg(kernel, 1, sizeof(cl_mem), (void *)&ret_mem_obj);
        if (ret != CL_SUCCESS) {
            printf("clSetKernelArg2\n");
            return ret;
        }
        // Execute the OpenCL kernel on the list
        size_t global_item_size = maxSize; // Process the entire lists
        size_t local_item_size = 64; // Divide work items into groups of 64
        ret = clEnqueueNDRangeKernel(command_queue, kernel, 1, NULL,
                                                                &global_item_size, &local_item_size, 0, NULL, NULL);

        if (ret != CL_SUCCESS) {
            printf("clEnqueueNDRangeKernel\n");
            return ret;
        }

        // Read the memory buffer C on the device to the local variable C
        // int *C = (int*)malloc(sizeof(int)*LIST_SIZE);
        ret = clEnqueueReadBuffer(command_queue, ret_mem_obj, CL_TRUE, 0, 
                        maxSize * sizeof(uint8_t), td->destbuf.data[0], 0, NULL, NULL);    
                                
        if (ret != CL_SUCCESS) {
            printf("clEnqueueReadBuffer\n");
            return ret;
        }


        // Clean up
        ret = clFlush(command_queue);
        if (ret != CL_SUCCESS) {
            printf("clFlush\n");
            return ret;
        }
        ret = clFinish(command_queue);
        if (ret != CL_SUCCESS) {
            printf("clFinish\n");
            return ret;
        }
        ret = clReleaseKernel(kernel);
        if (ret != CL_SUCCESS) {
            printf("clReleaseKernel\n");
            return ret;
        }
        ret = clReleaseProgram(program);
        if (ret != CL_SUCCESS) {
            printf("clReleaseProgram\n");
            return ret;
        }
        ret = clReleaseMemObject(mem_obj);
        if (ret != CL_SUCCESS) {
            printf("clReleaseMemObject1\n");
            return ret;
        }
        ret = clReleaseMemObject(ret_mem_obj);
        if (ret != CL_SUCCESS) {
            printf("clReleaseMemObject2\n");
            return ret;
        }
        ret = clReleaseCommandQueue(command_queue);
        if (ret != CL_SUCCESS) {
            printf("clReleaseCommandQueue\n");
            return ret;
        }
        ret = clReleaseContext(context);
        if (ret != CL_SUCCESS) {
            printf("clReleaseContext\n");
            return ret;
        }
    }
    return ret;
}

#endif
