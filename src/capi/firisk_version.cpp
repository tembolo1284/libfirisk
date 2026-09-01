#include "firisk.h"

#define FIR_STRINGIFY_(x) #x
#define FIR_STRINGIFY(x)  FIR_STRINGIFY_(x)

#define FIR_VERSION_STRING            \
    FIR_STRINGIFY(FIR_VERSION_MAJOR)  \
    "." FIR_STRINGIFY(FIR_VERSION_MINOR) \
    "." FIR_STRINGIFY(FIR_VERSION_PATCH)

extern "C" {

unsigned int fir_get_version(void)
{
    return FIR_VERSION;
}

int fir_is_compatible_dll(void)
{
    unsigned int major = fir_get_version() >> 16;

    /* Pre-1.0 the minor number carries breaking changes, so it has to
       match too. Drop the minor check at 1.0.0. */
    unsigned int minor = (fir_get_version() >> 8) & 0xffu;

    if (major != FIR_VERSION_MAJOR)
        return 0;
    if (major == 0 && minor != FIR_VERSION_MINOR)
        return 0;

    return 1;
}

const char *fir_get_version_string(void)
{
    return FIR_VERSION_STRING;
}

} /* extern "C" */
