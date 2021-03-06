/*
 *  transformfixedpoint.c
 *
 *  Fixed point implementation of image transformations (see also transformfloat.c/h)
 *
 *  Copyright (C) Georg Martius - June 2011
 *   georg dot martius at web dot de
 *
 *  This file is part of vid.stab video stabilization library
 *
 *  vid.stab is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License,
 *  as published by the Free Software Foundation; either version 2, or
 *  (at your option) any later version.
 *
 *  vid.stab is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GNU Make; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 */
#ifdef USE_OMP
#include <omp.h>
#endif

#include <stdbool.h>
#include "transformfixedpoint.h"
#include "transform.h"
#include "transformtype_operations.h"

#define iToFp8(v)  ((v)<<8)
#define fToFp8(v)  ((int32_t)((v)*((float)0xFF)))
#define iToFp16(v) ((v)<<16)
#define fToFp16(v) ((int32_t)((v)*((double)0xFFFF)))
#define fp16To8(v) ((v)>>8)
//#define fp16To8(v) ( (v) && 0x80 == 1 ? ((v)>>8 + 1) : ((v)>>8) )
#define fp24To8(v) ((v)>>16)

#define fp8ToI(v)  ((v)>>8)
#define fp16ToI(v) ((v)>>16)
#define fp8ToF(v)  ((v)/((double)(1<<8)))
#define fp16ToF(v) ((v)/((double)(1<<16)))
//#define fp16ToF(v) (((v & 0x8000)<<16) | (((v&0x7c00)+0x1c000)<<13) | ((v&0x03FF)<<13))

// #define fp8ToIRound(v) ( (((v)>>7) & 0x1) == 0 ? ((v)>>8) : ((v)>>8)+1 )
#define fp8_0_5 (1<<7)
#define fp8ToIRound(v) (((v) + fp8_0_5) >> 7)
//#define fp16ToIRound(v) ( (((v)>>15) & 0x1) == 0 ? ((v)>>16) : ((v)>>16)+1 )
#define fp16_0_5 (1<<15)
#define fp16ToIRound(v) (((v) + fp16_0_5) >> 16)

#define LUMA_CHANNEL 0

#define MAX_PIX_VAL 255

inline void lim_pix_val(uint8_t * const rv, const int res) 
{
  if (res>=0) {
    if (res < MAX_PIX_VAL) {
      *rv = (uint8_t)res;
    } else {
      *rv = MAX_PIX_VAL;
    }
  } else {
    *rv = 0;
  }
}

inline bool isEqual(const double a, const double b, const double epsilon)
{
  return fabs(a - b) < epsilon;
}

/** interpolateBiLinBorder: bi-linear interpolation function that also works at the border.
    This is used by many other interpolation methods at and outsize the border, see interpolate */
inline void interpolateBiLinBorder(uint8_t * const rv, const fp16 x, const fp16 y,
                                 const uint8_t * const img, const int linesize,
                                 const int width, const int height, const uint8_t def)
{
  int32_t ix = fp16ToI(x);
  int32_t iy = fp16ToI(y);
  int32_t ixx = ix + 1;
  int32_t iyy = iy + 1;
  if (ix < 0 || ixx >= width || iy < 0 || iyy >= height) {
    int32_t w  = 10; // number of pixels to blur out the border pixel outwards
    int32_t xl = - w - ix;
    int32_t yl = - w - iy;
    int32_t xh = ixx - w - width;
    int32_t yh = iyy - w - height;
    int32_t c = VS_MAX(VS_MIN(VS_MAX(xl, VS_MAX(yl, VS_MAX(xh, yh))),w),0);
    // pixel at border of source image
    short val_border = PIX(img, linesize, VS_MAX(VS_MIN(ix, width-1),0),
                           VS_MAX(VS_MIN(iy, height-1),0));
    int32_t res = (def * c + val_border * (w - c)) / w;
    lim_pix_val(rv, res);
    // *rv = (res >= 0) ? ((res < 255) ? res : 255) : 0;
  }else{
    short v1 = PIXEL(img, linesize, ixx, iyy, width, height, def);
    short v2 = PIXEL(img, linesize, ixx, iy, width, height, def);
    short v3 = PIXEL(img, linesize, ix, iyy, width, height, def);
    short v4 = PIXEL(img, linesize, ix, iy, width, height, def);
    fp16 x_f = iToFp16(ix);
    fp16 x_c = iToFp16(ixx);
    fp16 y_f = iToFp16(iy);
    fp16 y_c = iToFp16(iyy);
    fp16 s   = fp16To8(v1*(x - x_f)+v3*(x_c - x))*fp16To8(y - y_f) +
      fp16To8(v2*(x - x_f) + v4*(x_c - x))*fp16To8(y_c - y) + 1;
    const int32_t res = fp16ToIRound(s);
    lim_pix_val(rv, res);
    // *rv = (res >= 0) ? ((res < 255) ? res : 255) : 0;
  }
}

/** taken from http://en.wikipedia.org/wiki/Bicubic_interpolation for alpha=-0.5
    in matrix notation:
    a0-a3 are the neigthboring points where the target point is between a1 and a2
    t is the point of interpolation (position between a1 and a2) value between 0 and 1
                  | 0, 2, 0, 0 |  |a0|
                  |-1, 0, 1, 0 |  |a1|
    (1,t,t^2,t^3) | 2,-5, 4,-1 |  |a2|
                  |-1, 3,-3, 1 |  |a3|
*/
#ifdef TESTING
 inline static short bicub_kernel(fp16 t, short a0, short a1, short a2, short a3){ 
  //  (2*a1 + t*((-a0+a2) + t*((2*a0-5*a1+4*a2-a3) + t*(-a0+3*a1-3*a2+a3) )) ) / 2; 
   return ((iToFp16(2*a1) + t*(-a0+a2 
             + fp16ToI(t*((2*a0-5*a1+4*a2-a3) 
              + fp16ToI(t*(-a0+3*a1-3*a2+a3)) )) ) 
      ) ) >> 17; 
 } 
#else
inline short bicub_kernel(const fp16 t, const short a0, const short a1, const short a2, const short a3){
  // (2*a1 + t*((-a0+a2) + t*((2*a0-5*a1+4*a2-a3) + t*(-a0+3*a1-3*a2+a3) )) ) / 2;
  // we add 1/2 because of truncation errors
  return fp16ToIRound((iToFp16(2*a1) + t*(-a0+a2 + fp16ToIRound(t*((2*a0-5*a1+4*a2-a3) + fp16ToIRound(t*(-a0+3*a1-3*a2+a3)) )) )) >> 1);
}
#endif
// perf: ~0% perf improvement with _inline_ hint
/**
 * bi-cubic interpolation function using 4x4 pixel, see interpolate
 */
inline void interpolateBiCub(uint8_t * const rv, const fp16 x, const fp16 y,
                                 const uint8_t * const img, const int linesize,
                                 const int width, const int height, const uint8_t def)                    
{
  // do a simple linear interpolation at the border
  const int32_t ix_f = fp16ToI(x);
  const int32_t iy_f = fp16ToI(y);
  if (unlikely((ix_f < 1) || (ix_f > (width - 3)) || (iy_f < 1) || (iy_f > (height - 3)) )) { //see ~1% perf improvement with _unlikely
    interpolateBiLinBorder(rv, x, y, img, linesize, width, height, def);
  } else {
    const fp16 x_f = iToFp16(ix_f);
    const fp16 y_f = iToFp16(iy_f);
    const fp16 tx  = x-x_f;

    short vals[4];
    for (int32_t iy = 0; iy < 4; iy++) {
      int32_t pixs[4];
      for (int32_t ix = 0; ix < 4; ix++) {
          const int32_t ixx = ix_f + ix - 1;
          const int32_t iyy = iy_f + iy - 1;
          pixs[ix] = PIX(img, linesize, ixx, iyy);
      }//for
      vals[iy] = bicub_kernel(tx, pixs[0], pixs[1], pixs[2], pixs[3]);
    }//for

    short res = bicub_kernel(y-y_f, vals[0], vals[1], vals[2], vals[3]);
    lim_pix_val(rv, res);
  }
}

/** interpolateBiLin: bi-linear interpolation function, see interpolate */
void interpolateBiLin(uint8_t * const rv, const fp16 x, const fp16 y,
                                 const uint8_t * const img, const int linesize,
                                 const int width, const int height, const uint8_t def)
{
  //rounds down
  const int32_t ix = fp16ToI(x);
  const int32_t iy = fp16ToI(y);
  
  if (unlikely( (ix < 0) || (ix > (width - 2)) || (iy < 0) || (iy > (height - 2)) )) {
    interpolateBiLinBorder(rv, x, y, img, linesize, width, height, def);
  } else {
    const int32_t ixx = ix + 1;
    const int32_t iyy = iy + 1;

    /*
     * https://en.wikipedia.org/wiki/Bilinear_interpolation#Application_in_image_processing
     * 
     *         
     * Cj   [c] ----- [d]      
     * Cf    | [z]     |
     *       |         |
     *       |         |    col /\
     * Ci   [a] ----- [b]   row -->
     *       Rk Rf     Rl
     * 
     *  I[Ci,Rf] = (Rl-Rf).a        +  (Rf-Rk).b
     *  I[Cj,Rf] = (Rl-Rf).c        +  (Rl-Rf).d
     *  I[Rf,Cf] = (Cj-Cf).I[Ci,Rf] +  (Cf-Ci).I[Cj,Rf]   <----result (i.e. value at z)
     */
// #define WIKI_EXP
#ifdef WIKI_EXP
    // Easier to read but 10% slower
    const short a = PIX(img, linesize, ix, iy);     // a
    const short b = PIX(img, linesize, ixx, iy);    // b
    const short c = PIX(img, linesize, ix, iyy);    // c
    const short d = PIX(img, linesize, ixx, iyy);   // d

    const float xF = fp16ToF(x);
    const float yF = fp16ToF(y);
    float parts[3];
    parts[0] = ((xF-ix) * b) + ((ixx - xF) * a);
    parts[1] = ((xF-ix) * d) + ((ixx - xF) * c);
    parts[2] = ((yF-iy) * parts[0]) + ((iyy - yF) * parts[1]);
    const short res = parts[2];
#else    

    const fp16 x_f = iToFp16(ix);                  
    const fp16 x_c = iToFp16(ixx);                 
    const fp16 y_f = iToFp16(iy);                  
    const fp16 y_c = iToFp16(iyy);                 
    const short v1 = PIX(img, linesize, ixx, iyy); 
    const short v2 = PIX(img, linesize, ixx, iy);  
    const short v3 = PIX(img, linesize, ix, iyy);  
    const short v4 = PIX(img, linesize, ix, iy);   

    const fp16 s  = fp16To8(v1*(x - x_f) + v3*(x_c - x)) * fp16To8(y - y_f) + fp16To8(v2*(x - x_f) + v4*(x_c - x))*fp16To8(y_c - y);
    // it is underestimated due to truncation, so we add one
    const short res = fp16ToI(s);
#endif
    lim_pix_val(rv, res);
  }
}

/** interpolateLin: linear (only x) interpolation function, see interpolate */
inline void interpolateLin(uint8_t * const rv, const fp16 x, const fp16 y,
                                 const uint8_t * const img, const int linesize,
                                 const int width, const int height, const uint8_t def)
{
  int32_t ix = fp16ToI(x);
  int32_t ixx = ix + 1;
  fp16    x_c  = iToFp16(ixx);
  fp16    x_f  = iToFp16(ix);
  int     y_n  = fp16ToIRound(y);

  short v1 = PIXEL(img, linesize, ixx, y_n, width, height, def);
  short v2 = PIXEL(img, linesize, ix, y_n, width, height, def);
  fp16 s   = v1*(x - x_f) + v2*(x_c - x);
  short res = fp16ToI(s);
  lim_pix_val(rv, res);
}

/** interpolateZero: nearest neighbor interpolation function, see interpolate */
inline void interpolateZero(uint8_t * const rv, const int32_t x, const int32_t y,
                                 const uint8_t * const img, const int linesize,
                                 const int width, const int height, const uint8_t def)
{
  int32_t ix_n = fp16ToIRound(x);
  int32_t iy_n = fp16ToIRound(y);
  int32_t res = PIXEL(img, linesize, ix_n, iy_n, width, height, def);
  lim_pix_val(rv, res);
}


/**
 * interpolateN: Bi-linear interpolation function for N channel image.
 *
 * Parameters:
 *             rv: destination pixel (call by reference)
 *            x,y: the source coordinates in the image img. Note this
 *                 are real-value coordinates, that's why we interpolate
 *            img: source image
 *   width,height: dimension of image
 *              N: number of channels
 *        channel: channel number (0..N-1)
 *            def: default value if coordinates are out of range
 * Return value:  None
 */
inline void interpolateN(uint8_t *rv, fp16 x, fp16 y,
                         const uint8_t *img, int img_linesize,
                         int width, int height,
                         uint8_t N, uint8_t channel, uint8_t def)
{
  int32_t ix = fp16ToI(x);
  int32_t iy = fp16ToI(y);
  if (ix < 0 || ix > width-1 || iy < 0 || iy > height - 1) {
    *rv = def;
  } else {
    int32_t ixx = ix + 1;
    int32_t iyy = iy + 1;
    short v1 = PIXN(img, img_linesize, ixx, iyy, N, channel);
    short v2 = PIXN(img, img_linesize, ixx, iy, N, channel);
    short v3 = PIXN(img, img_linesize, ix, iyy, N, channel);
    short v4 = PIXN(img, img_linesize, ix, iy, N, channel);
    fp16 x_f = iToFp16(ix);
    fp16 x_c = iToFp16(ixx);
    fp16 y_f = iToFp16(iy);
    fp16 y_c = iToFp16(iyy);
    fp16 s  = fp16To8(v1*(x - x_f)+v3*(x_c - x))*fp16To8(y - y_f) +
      fp16To8(v2*(x - x_f) + v4*(x_c - x))*fp16To8(y_c - y);
    int32_t res = fp16ToIRound(s);
    lim_pix_val(rv, res);
  }
}


/**
 * transformPacked: applies current transformation to frame
 * Parameters:
 *         td: private data structure of this filter
 * Return value:
 *         0 for failture, 1 for success
 * Preconditions:
 *  The frame must be in Packed format
 */
int transformPacked(VSTransformData* td, VSTransform t)
{
  int x = 0, y = 0, k = 0;
  uint8_t *D_1, *D_2;

  D_1  = td->src.data[0];
  D_2  = td->destbuf.data[0];
  fp16 c_s_x = iToFp16(td->fiSrc.width/2);
  fp16 c_s_y = iToFp16(td->fiSrc.height/2);
  int32_t c_d_x = td->fiDest.width/2;
  int32_t c_d_y = td->fiDest.height/2;

  /* for each pixel in the destination image we calc the source
   * coordinate and make an interpolation:
   *      p_d = c_d + M(p_s - c_s) + t
   * where p are the points, c the center coordinate,
   *  _s source and _d destination,
   *  t the translation, and M the rotation matrix
   *      p_s = M^{-1}(p_d - c_d - t) + c_s
   */
  double zoomVal     = 1.0 - (t.zoom/100.0);
  fp16 zcos_a = fToFp16(zoomVal*cos(-t.alpha)); // scaled cos
  fp16 zsin_a = fToFp16(zoomVal*sin(-t.alpha)); // scaled sin
  fp16  c_tx    = c_s_x - fToFp16(t.x);
  fp16  c_ty    = c_s_y - fToFp16(t.y);
  int channels = td->fiSrc.bytesPerPixel;
  /* All channels */
  for (y = 0; y < td->fiDest.height; y++) {
    int32_t y_d1 = (y - c_d_y);
    for (x = 0; x < td->fiDest.width; x++) {
      int32_t x_d1 = (x - c_d_x);
      fp16 x_s  =  zcos_a * x_d1 + zsin_a * y_d1 + c_tx;
      fp16 y_s  = -zsin_a * x_d1 + zcos_a * y_d1 + c_ty;

      for (k = 0; k < channels; k++) { // iterate over colors
        uint8_t *dest = &D_2[x + y * td->destbuf.linesize[0]+k];
        interpolateN(dest, x_s, y_s, D_1, td->src.linesize[0],
                     td->fiSrc.width, td->fiSrc.height,
                     (uint8_t)channels, (uint8_t)k, td->conf.crop ? 16 : *dest);
      }
    }
  }
  return VS_OK;
}

/**
 * transformPlanar: applies current transformation to frame
 *
 * Parameters:
 *         td: private data structure of this filter
 * Return value:
 *         0 for failture, 1 for success
 * Preconditions:
 *  The frame must be in Planar format
 *
 * Fixed-point format 32 bit integer:
 *  for image coords we use val<<8
 *  for angle and zoom we use val<<16
 *
 */
int transformPlanar(VSTransformData* td, VSTransform t)
{
#ifdef USE_OMP
  if (td->conf.threadCount < 1) {
    // default to half available threads. 
    td->conf.threadCount = omp_get_num_threads()/2;
  }
  omp_set_num_threads(td->conf.threadCount);
#endif

  if ( isEqual(t.alpha, 0, 1E-6) && isEqual(t.x, 0, 1E-6) && isEqual(t.y, 0, 1E-6) && isEqual(t.zoom, 0, 1E-6) ) {
    if(vsFramesEqual(&td->src, &td->destbuf))
      return VS_OK; // noop
    else {
      vsFrameCopy(&td->destbuf, &td->src, &td->fiSrc);
      return VS_OK;
    }
  }

  const double zoomVal = 1.0 - (t.zoom / 100.0);
  
  for (int plane = 0; plane < td->fiSrc.planes; plane++) {
    const uint8_t * const srcData  = td->src.data[plane];
    uint8_t * const destData       = td->destbuf.data[plane];
    const int linesize = td->destbuf.linesize[plane];
    const VSBorderType border = td->conf.crop;
    const VSInterpolType interpolType = td->conf.interpolType;
    
    const int wsub          = vsGetPlaneWidthSubS(&td->fiSrc, plane);
    const int hsub          = vsGetPlaneHeightSubS(&td->fiSrc, plane);
    const int destWidth     = CHROMA_SIZE(td->fiDest.width, wsub);
    const int destHeight    = CHROMA_SIZE(td->fiDest.height, hsub);
    const int sourceWidth   = CHROMA_SIZE(td->fiSrc.width, wsub);
    const int sourceHeight  = CHROMA_SIZE(td->fiSrc.height, hsub);
    const uint8_t black     = (plane == 0) ? 0 : 0x80;

    const fp16 c_s_x = iToFp16(sourceWidth >> 1);
    const fp16 c_s_y = iToFp16(sourceHeight >> 1);
    const int32_t c_d_x = destWidth >> 1;
    const int32_t c_d_y = destHeight >> 1;

    const fp16 zcos_a = fToFp16(zoomVal * cos(-t.alpha)); // scaled cos
    const fp16 zsin_a = fToFp16(zoomVal * sin(-t.alpha)); // scaled sin
    const fp16  c_tx  = c_s_x - (fToFp16(t.x) >> wsub);
    const fp16  c_ty  = c_s_y - (fToFp16(t.y) >> hsub);

    /* for each pixel in the destination image we calc the source
     * coordinate and make an interpolation:
     *      p_d = c_d + M(p_s - c_s) + t
     * where p are the points, c the center coordinate,
     *  _s source and _d destination,
     *  t the translation, and M the rotation and scaling matrix
     *      p_s = M^{-1}(p_d - c_d - t) + c_s
     */
#ifdef USE_OMP
    #pragma omp parallel for
#endif
    for (int32_t y = 0; y < destHeight; y++) {
      // swapping of the loops brought 15% performance gain
      const int32_t y_d1 = y - c_d_y;
      const fp16 sinPart = (zsin_a * y_d1) + c_tx;
      const fp16 cosPart = (zcos_a * y_d1) + c_ty;
      const fp16 indexPart =  (y * linesize);
      for (int32_t x = 0; x < destWidth; x++) {
        const int32_t x_d1 = x - c_d_x;
        const fp16 x_s = (zcos_a * x_d1) + sinPart;
        const fp16 y_s = (-zsin_a * x_d1) + cosPart;
        const uint32_t index = x + indexPart;
        uint8_t * const dest = &destData[index];
        const uint8_t def = border ? black : *dest;
        // inlining the interpolation function brings no performance change
        if (likely(interpolType != VS_BiCubLin)) {
          td->interpolate(dest, x_s, y_s, srcData, linesize, sourceWidth, sourceHeight, def);
        } else {
          vsInterpolateFun interpolate;
          if (unlikely(plane == LUMA_CHANNEL)) {
             interpolate = &interpolateBiCub;
          } else {
            interpolate = &interpolateBiLin;
          }
          interpolate(dest, x_s, y_s, srcData, linesize, sourceWidth, sourceHeight, def);
        }

      }//for
    }//for
  }

  return VS_OK;
}

/*
 * Local variables:
 *   c-file-style: "stroustrup"
 *   c-file-offsets: ((case-label . *) (statement-case-intro . *))
 *   indent-tabs-mode: nil
 *   c-basic-offset: 2 t
 *
 * End:
 *
 * vim: expandtab shiftwidth=2:
 */
