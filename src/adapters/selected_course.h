#pragma once

#if defined(VEGA_TEST_COURSE) && VEGA_TEST_COURSE == 1
#include "tamagawagakuen_course_data.h"
#elif defined(VEGA_TEST_COURSE) && VEGA_TEST_COURSE == 2
#include "tobitakyu_course_data.h"
#else
#include "course_data.h"
#endif
