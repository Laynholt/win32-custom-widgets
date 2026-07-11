#pragma once

#include <iostream>

inline int testFailures = 0;

#define CHECK(expression)                                                        \
    do {                                                                         \
        if (!(expression)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK(" #expression \
                      << ") failed\n";                                            \
            ++testFailures;                                                      \
        }                                                                        \
    } while (false)
