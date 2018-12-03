#ifndef CLFUNCS_H
#define CLFUNCS_H
#ifdef USE_OPENCL //FIXME: TEMPORARY

#include <CL/cl.h>
#include "transform.h"

cl_int clTransform(VSTransformData* const td, const VSTransform t);

#endif

#endif
