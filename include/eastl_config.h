#pragma once

#include <common.h>
#include <spm/system.h>

// Set platform as linux powerpc
#define __powerpc__
#define EA_PLATFORM_LINUX

// Use spm SPM_ASSERT
// TODO: stub when EA SPM_ASSERT flags are off
#define EASTL_ASSERT(expression) SPM_ASSERT(expression, "EASTL_ASSERT")
#define EASTL_ASSERT_MSG(expression, message) SPM_ASSERT(expression, "EASTL_ASSERT_MSG: %s", message)
#define EASTL_FAIL_MSG(message) SPM_ASSERT(0, "EASTL_FAIL_MSG: %s", message)