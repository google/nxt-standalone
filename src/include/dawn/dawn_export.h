// Copyright 2018 The Dawn Authors
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

#ifndef DAWN_EXPORT_H_
#define DAWN_EXPORT_H_

#if defined(_WIN32)
#    if defined(DAWN_IMPLEMENTATION)
#        define DAWN_EXPORT __declspec(dllexport)
#    else
#        define DAWN_EXPORT __declspec(dllimport)
#    endif
#else
#    if defined(DAWN_IMPLEMENTATION)
#        define DAWN_EXPORT __attribute__((visibility("default")))
#    else
#        define DAWN_EXPORT
#    endif
#endif

#endif  // DAWN_EXPORT_H_
