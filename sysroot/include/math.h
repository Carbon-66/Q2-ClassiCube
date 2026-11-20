#ifndef _MATH_H
#define _MATH_H
/* libm.so.6; sqrt and sqrtf become the FPU's own instruction */
double sqrt(double);
float sqrtf(float);
double sin(double);
double cos(double);
float sinf(float);
float cosf(float);
double tan(double);
double atan2(double, double);
float atan2f(float, float);
double floor(double);
float floorf(float);
double ceil(double);
float ceilf(float);
double fabs(double);
float fabsf(float);
double log(double);
double exp(double);
double pow(double, double);
float powf(float, float);
double fmod(double, double);
double log2(double);
float log2f(float);
double exp2(double);
float exp2f(float);
#endif
