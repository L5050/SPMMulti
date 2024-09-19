#include "errno.h"

s32* __errno() {
    return &spm_errno;
}
