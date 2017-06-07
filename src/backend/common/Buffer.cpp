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

#include "Buffer.h"

#include "Device.h"

#include <utility>
#include <cstdio>

namespace backend {

    // Buffer

    BufferBase::BufferBase(BufferBuilder* builder)
        : UsageTracker(builder->allowedUsage, builder->currentUsage),
          device(builder->device), size(builder->size) {
    }

    BufferViewBuilder* BufferBase::CreateBufferViewBuilder() {
        return new BufferViewBuilder(device, this);
    }

    DeviceBase* BufferBase::GetDevice() {
        return device;
    }

    uint32_t BufferBase::GetSize() const {
        return size;
    }

    void BufferBase::SetSubData(uint32_t start, uint32_t count, const uint32_t* data) {
        if ((start + count) * sizeof(uint32_t) > GetSize()) {
            device->HandleError("Buffer subdata out of range");
            return;
        }

        if (!HasUsage(nxt::BufferUsageBit::Mapped)) {
            device->HandleError("Buffer needs the mapped usage bit");
            return;
        }

        SetSubDataImpl(start, count, data);
    }

    bool BufferBase::IsUsagePossible(nxt::BufferUsageBit allowedUsage, nxt::BufferUsageBit usage) {
        const nxt::BufferUsageBit allReadBits =
            nxt::BufferUsageBit::TransferSrc |
            nxt::BufferUsageBit::Index |
            nxt::BufferUsageBit::Vertex |
            nxt::BufferUsageBit::Uniform;
        bool allowed = (usage & allowedUsage) == usage;
        bool readOnly = (usage & allReadBits) == usage;
        bool singleUse = nxt::HasZeroOrOneBits(usage);
        return allowed && (readOnly || singleUse);
    }

    void BufferBase::TransitionUsage(nxt::BufferUsageBit usage) {
        if (!IsTransitionPossible(usage)) {
            device->HandleError("Buffer frozen or usage not allowed");
            return;
        }
        TransitionUsageImpl(usage);
    }

    void BufferBase::FreezeUsage(nxt::BufferUsageBit usage) {
        if (!FreezeUsageImpl(usage)) {
            device->HandleError("Buffer already frozen or usage not allowed");
        }
    }

    // BufferBuilder

    enum BufferSetProperties {
        BUFFER_PROPERTY_ALLOWED_USAGE = 0x1,
        BUFFER_PROPERTY_INITIAL_USAGE = 0x2,
        BUFFER_PROPERTY_SIZE = 0x4,
    };

    BufferBuilder::BufferBuilder(DeviceBase* device) : Builder(device) {
    }

    BufferBase* BufferBuilder::GetResultImpl() {
        constexpr int allProperties = BUFFER_PROPERTY_ALLOWED_USAGE | BUFFER_PROPERTY_SIZE;
        if ((propertiesSet & allProperties) != allProperties) {
            HandleError("Buffer missing properties");
            return nullptr;
        }

        if (!BufferBase::IsUsagePossible(allowedUsage, currentUsage)) {
            HandleError("Initial buffer usage is not allowed");
            return nullptr;
        }

        return device->CreateBuffer(this);
    }

    void BufferBuilder::SetAllowedUsage(nxt::BufferUsageBit usage) {
        if ((propertiesSet & BUFFER_PROPERTY_ALLOWED_USAGE) != 0) {
            HandleError("Buffer allowedUsage property set multiple times");
            return;
        }

        this->allowedUsage = usage;
        propertiesSet |= BUFFER_PROPERTY_ALLOWED_USAGE;
    }

    void BufferBuilder::SetInitialUsage(nxt::BufferUsageBit usage) {
        if ((propertiesSet & BUFFER_PROPERTY_INITIAL_USAGE) != 0) {
            HandleError("Buffer initialUsage property set multiple times");
            return;
        }

        this->currentUsage = usage;
        propertiesSet |= BUFFER_PROPERTY_INITIAL_USAGE;
    }

    void BufferBuilder::SetSize(uint32_t size) {
        if ((propertiesSet & BUFFER_PROPERTY_SIZE) != 0) {
            HandleError("Buffer size property set multiple times");
            return;
        }

        this->size = size;
        propertiesSet |= BUFFER_PROPERTY_SIZE;
    }

    // BufferViewBase

    BufferViewBase::BufferViewBase(BufferViewBuilder* builder)
        : buffer(std::move(builder->buffer)), size(builder->size), offset(builder->offset) {
    }

    BufferBase* BufferViewBase::GetBuffer() {
        return buffer.Get();
    }

    uint32_t BufferViewBase::GetSize() const {
        return size;
    }

    uint32_t BufferViewBase::GetOffset() const {
        return offset;
    }

    // BufferViewBuilder

    enum BufferViewSetProperties {
        BUFFER_VIEW_PROPERTY_EXTENT = 0x1,
    };

    BufferViewBuilder::BufferViewBuilder(DeviceBase* device, BufferBase* buffer)
        : Builder(device), buffer(buffer) {
    }

    BufferViewBase* BufferViewBuilder::GetResultImpl() {
        constexpr int allProperties = BUFFER_VIEW_PROPERTY_EXTENT;
        if ((propertiesSet & allProperties) != allProperties) {
            HandleError("Buffer view missing properties");
            return nullptr;
        }

        return device->CreateBufferView(this);
    }

    void BufferViewBuilder::SetExtent(uint32_t offset, uint32_t size) {
        if ((propertiesSet & BUFFER_VIEW_PROPERTY_EXTENT) != 0) {
            HandleError("Buffer view extent property set multiple times");
            return;
        }

        uint64_t viewEnd = static_cast<uint64_t>(offset) + static_cast<uint64_t>(size);
        if (viewEnd > static_cast<uint64_t>(buffer->GetSize())) {
            HandleError("Buffer view end is OOB");
            return;
        }

        this->offset = offset;
        this->size = size;
        propertiesSet |= BUFFER_VIEW_PROPERTY_EXTENT;
    }

}
