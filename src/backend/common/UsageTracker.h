// Copyright 2017 The NXT Authors
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

#ifndef BACKEND_COMMON_USAGETRACKER_H
#define BACKEND_COMMON_USAGETRACKER_H

template<typename T, typename B>
class UsageTracker {
    public:
        UsageTracker(B allowedUsage, B usage)
            : allowedUsage(allowedUsage), currentUsage(usage) {
        }

        B GetAllowedUsage() const {
            return allowedUsage;
        }

        B GetUsage() const {
            return currentUsage;
        }

        bool IsFrozen() const {
            return frozen;
        }

        bool HasUsage(B usage) const {
            return usage & currentUsage;
        }

        bool HasFrozenUsage(B usage) const {
            return IsFrozen() && HasUsage(usage);
        }

        bool IsTransitionPossible(B usage) const {
            if (IsFrozen()) {
                return false;
            }
            return T::IsUsagePossible(allowedUsage, usage);
        }

        void TransitionUsageImpl(B usage) {
            assert(IsTransitionPossible(usage));
            currentUsage = usage;
        }

        bool ClearUsage() {
            if (IsFrozen()) {
                return false;
            }
            currentUsage = nxt::TextureUsageBit::None;
            return true;
        }

        bool FreezeUsageImpl(B usage) {
            if (!IsTransitionPossible(usage)) {
                return false;
            }
            allowedUsage = usage;
            currentUsage = usage;
            frozen = true;
            return true;
        }

    private:
        B allowedUsage = B::None;
        B currentUsage = B::None;
        bool frozen = false;
};

#endif // BACKEND_COMMON_USAGETRACKER_H