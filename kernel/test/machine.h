// Host-build stub: replaces kernel/machine.h for test builds.
// http.cc includes machine.h but calls no hardware functions in the test path.
#pragma once
