#include <hwcutil.h>
#include <cutils/log.h>
#include <cutils/properties.h>
#include <stdlib.h>

int hwc_get_int_property(const char* pcProperty,const char* default_value)
{
    char value[PROPERTY_VALUE_MAX];
    int new_value = 0;

    if(pcProperty == NULL || default_value == NULL)
    {
        ALOGE("hwc_get_int_property: invalid param");
        return -1;
    }

    property_get(pcProperty, value, default_value);
    new_value = atoi(value);

    return new_value;
}
