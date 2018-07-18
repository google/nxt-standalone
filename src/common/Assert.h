// Copyright 2017 The Dawn Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef COMMON_ASSERT_H_
#define COMMON_ASSERT_H_

#include "common/Compiler.h"

// NXT asserts to be used instead of the regular C stdlib assert function (if you don't use assert
// yet, you should start now!). In debug ASSERT(condition) will trigger an error, otherwise in
// release it does nothing at runtime.
//
// In case of name clashes (with for example a testing library), you can define the
// NXT_SKIP_ASSERT_SHORTHANDS to only define the NXT_ prefixed macros.
//
// These asserts feature:
//     - Logging of the error with file, line and function information.
//     - Breaking in the debugger when an assert is triggered and a debugger is attached.
//     - Use the assert information to help the compiler optimizer in release builds.

// MSVC triggers a warning in /W4 for do {} while(0). SDL worked around this by using (0,0) and
// points out that it looks like an owl face.
#if defined(NXT_COMPILER_MSVC)
#    define NXT_ASSERT_LOOP_CONDITION (0, 0)
#else
#    define NXT_ASSERT_LOOP_CONDITION (0)
#endif

// NXT_ASSERT_CALLSITE_HELPER generates the actual assert code. In Debug it does what you would
// expect of an assert and in release it tries to give hints to make the compiler generate better
// code.
#if defined(NXT_ENABLE_ASSERTS)
#    define NXT_ASSERT_CALLSITE_HELPER(file, func, line, condition)   \
        do {                                                          \
            if (!(condition)) {                                       \
                HandleAssertionFailure(file, func, line, #condition); \
            }                                                         \
        } while (NXT_ASSERT_LOOP_CONDITION)
#else
#    if defined(NXT_COMPILER_MSVC)
#        define NXT_ASSERT_CALLSITE_HELPER(file, func, line, condition) __assume(condition)
#    elif defined(NXT_COMPILER_CLANG) && defined(__builtin_assume)
#        define NXT_ASSERT_CALLSITE_HELPER(file, func, line, condition) __builtin_assume(condition)
#    else
#        define NXT_ASSERT_CALLSITE_HELPER(file, func, line, condition) \
            do {                                                        \
                NXT_UNUSED(sizeof(condition));                          \
            } while (NXT_ASSERT_LOOP_CONDITION)
#    endif
#endif

#define NXT_ASSERT(condition) NXT_ASSERT_CALLSITE_HELPER(__FILE__, __func__, __LINE__, condition)
#define NXT_UNREACHABLE()                                                \
    do {                                                                 \
        NXT_ASSERT(NXT_ASSERT_LOOP_CONDITION && "Unreachable code hit"); \
        NXT_BUILTIN_UNREACHABLE();                                       \
    } while (NXT_ASSERT_LOOP_CONDITION)

#if !defined(NXT_SKIP_ASSERT_SHORTHANDS)
#    define ASSERT NXT_ASSERT
#    define UNREACHABLE NXT_UNREACHABLE
#endif

void HandleAssertionFailure(const char* file,
                            const char* function,
                            int line,
                            const char* condition);

#endif  // COMMON_ASSERT_H_
