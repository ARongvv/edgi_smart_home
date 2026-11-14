/* 宿主测试桩：math.h
 * tcc 的 math.h 与 stdio 冲突，这里只声明业务代码用到的几个函数。 */
#ifndef __STUB_MATH_H__
#define __STUB_MATH_H__
double sqrt(double x);
double pow(double x, double y);
double fabs(double x);
double floor(double x);
double ceil(double x);
double round(double x);
double log(double x);
double log10(double x);
double exp(double x);
double sin(double x);
double cos(double x);
double atan2(double y, double x);
double trunc(double x);
#define isfinite(x) ((x) - (x) == 0.0)
#define M_PI 3.14159265358979323846
#endif
