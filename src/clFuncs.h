#ifndef CLFUNCS_H
#define CLFUNCS_H

#include <CL/cl.h>
#include "transform.h"

cl_int clTransform(VSTransformData* const td, const VSTransform t);

#endif