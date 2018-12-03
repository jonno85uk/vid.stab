cmake_minimum_required (VERSION 2.8.5)
project (vid.stab)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/CMakeModules/")

include (FindSSE)
include (GNUInstallDirs)

set(MAJOR_VERSION 1)
set(MINOR_VERSION 1)
set(PATCH_VERSION 0)
set(VIDSTAB_VERSION ${MAJOR_VERSION}.${MINOR_VERSION}${PATCH_VERSION})

# Default to release builds if no explicit build type specified.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "Release")
endif()

option(BUILD_SHARED_LIBS "build shared libraries instead of static libraries" ON) # no need for -fPIC

set(CMAKE_C_FLAGS_DEBUG "-Wall -Wno-pointer-sign -g3 -O0")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -march=native -Ofast")
add_definitions(-std=gnu11)

# here we should check for SSE2
# our  -DUSE_SSE2_ASM code does not work with fpic
# explicitly using sse2 and fast-math makes 0% perf difference
if(SSE2_FOUND)
  add_definitions( -DUSE_SSE2 )
endif()

set(SOURCES src/frameinfo.c src/transformtype.c src/libvidstab.c
  src/transform.c src/transformfixedpoint.c src/motiondetect.c
  src/motiondetect_opt.c src/serialize.c src/localmotion2transform.c
  src/boxblur.c src/vsvector.c src/clFuncs.c)

set(HEADERS src/frameinfo.h src/transformtype.h src/libvidstab.h
  src/transform.h src/motiondetect.h src/serialize.h
  src/localmotion2transform.h src/boxblur.h src/vsvector.h src/clFuncs.h)


# Create the vidstab library
add_library (vidstab ${SOURCES})
#set version of lib
set_target_properties(vidstab PROPERTIES SOVERSION ${MAJOR_VERSION}.${MINOR_VERSION})
target_link_libraries(vidstab m)
set(PKG_EXTRA_LIBS -lm)

option(USE_OPENCL "use opencl use OPENCL" OFF)
option(USE_OMP "use parallelization use OMP" OFF)
option(USE_ACC "use openacc use OPENACC" OFF)
if(USE_OPENCL)
  include_directories(AFTER SYSTEM /opt/amdgpu-pro/include/)
  find_library(OPENCL OpenCL HINTS /opt/amdgpu-pro/lib/x86_64-linux-gnu/)
  target_link_libraries(vidstab PUBLIC ${OPENCL})
  set(SOURCES ${SOURCES} src/clFuncs.c)
  set(HEADERS ${HEADERS} src/clFuncs.h)
  unset(USE_OMP)
elseif(USE_ACC)
  add_definitions(-fopenacc -USE_ACC)
elseif(USE_OMP)
  add_definitions(-fopenmp -DUSE_OMP)
  target_link_libraries(vidstab gomp)
  set(PKG_EXTRA_LIBS "${PKG_EXTRA_LIBS} -lgomp -lpthread")
endif()

# json file for use in sourcetrail
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

#if(!NOHEADERS)
FILE(GLOB HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")
INSTALL(FILES ${HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/vid.stab)
#endif()

INSTALL(TARGETS vidstab
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

include(create_pkgconfig_file)
create_pkgconfig_file(vidstab "Vid.Stab, a library for stabilizing video clips")
