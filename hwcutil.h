#ifndef _HWC_UTIL_
#define _HWC_UTIL_

#define hwcMIN(x, y)			(((x) <= (y)) ?  (x) :  (y))
#define hwcMAX(x, y)			(((x) >= (y)) ?  (x) :  (y))
#define IS_ALIGN(val,align)    (((val)&(align-1))==0)
#define ALIGN( value, base ) (((value) + ((base) - 1)) & ~((base) - 1))

int hwc_get_int_property(const char* pcProperty,const char* default_value);

#endif // _HWC_UTIL_
