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

#ifndef BACKEND_D3D12_DESCRIPTORHEAPALLOCATOR_H_
#define BACKEND_D3D12_DESCRIPTORHEAPALLOCATOR_H_

#include "d3d12_platform.h"

#include "common/SerialQueue.h"
#include <array>
#include <vector>

namespace backend {
namespace d3d12 {

    class Device;

    class DescriptorHeapHandle {

        public:
            DescriptorHeapHandle();
            DescriptorHeapHandle(ComPtr<ID3D12DescriptorHeap> descriptorHeap, uint32_t sizeIncrement, uint32_t offset);

            ID3D12DescriptorHeap* Get() const;
            D3D12_CPU_DESCRIPTOR_HANDLE GetCPUHandle(uint32_t index) const;
            D3D12_GPU_DESCRIPTOR_HANDLE GetGPUHandle(uint32_t index) const;

        private:
            Device* device;
            ComPtr<ID3D12DescriptorHeap> descriptorHeap;
            uint32_t sizeIncrement;
            uint32_t offset;
    };

    class DescriptorHeapAllocator {
        public:
            DescriptorHeapAllocator(Device* device);

            DescriptorHeapHandle AllocateGPUHeap(D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t count);
            DescriptorHeapHandle AllocateCPUHeap(D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t count);
            void Tick(uint64_t lastCompletedSerial);

        private:
            static constexpr unsigned int kMaxCbvUavSrvHeapSize = 1000000;
            static constexpr unsigned int kMaxSamplerHeapSize = 2048;
            static constexpr unsigned int kDescriptorHeapTypes = D3D12_DESCRIPTOR_HEAP_TYPE::D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;

            struct AllocationInfo {
                uint32_t size = 0;
                uint32_t remaining = 0;
            };

            using DescriptorHeapInfo = std::pair<ComPtr<ID3D12DescriptorHeap>, AllocationInfo>;

            DescriptorHeapHandle Allocate(D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t count, uint32_t allocationSize, DescriptorHeapInfo* heapInfo, D3D12_DESCRIPTOR_HEAP_FLAGS flags);
            void Release(DescriptorHeapHandle handle);

            Device* device;

            std::array<uint32_t, kDescriptorHeapTypes> sizeIncrements;
            std::array<DescriptorHeapInfo, kDescriptorHeapTypes> cpuDescriptorHeapInfos;
            std::array<DescriptorHeapInfo, kDescriptorHeapTypes> gpuDescriptorHeapInfos;
            SerialQueue<DescriptorHeapHandle> releasedHandles;
    };

}
}

#endif // BACKEND_D3D12_DESCRIPTORHEAPALLOCATOR_H_
