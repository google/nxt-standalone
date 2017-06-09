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

#include "QueueD3D12.h"

#include "D3D12Backend.h"
#include "CommandBufferD3D12.h"

namespace backend {
namespace d3d12 {

    Queue::Queue(Device* device, QueueBuilder* builder)
        : QueueBase(builder), device(device) {

        ASSERT_SUCCESS(device->GetD3D12Device()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator)));
        ASSERT_SUCCESS(device->GetD3D12Device()->CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            commandAllocator.Get(),
            nullptr,
            IID_PPV_ARGS(&commandList)
        ));
        ASSERT_SUCCESS(commandList->Close());

        ASSERT_SUCCESS(device->GetD3D12Device()->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
        fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        ASSERT(fenceEvent != nullptr);
    }

    void Queue::Submit(uint32_t numCommands, CommandBuffer* const * commands) {
        // TODO(enga@google.com): This will stall on the previous submit because
        // the commands must finish exeuting before the ID3D12CommandAllocator is reset.
        // This should be fixed / optimized by using multiple command allocators.
        const uint64_t currentFence = fenceValue++;
        ASSERT_SUCCESS(device->GetCommandQueue()->Signal(fence.Get(), fenceValue));

        if (fence->GetCompletedValue() < currentFence) {
            ASSERT_SUCCESS(fence->SetEventOnCompletion(currentFence, fenceEvent));
            WaitForSingleObject(fenceEvent, INFINITE);
        }

        ASSERT_SUCCESS(commandAllocator->Reset());
        ASSERT_SUCCESS(commandList->Reset(commandAllocator.Get(), NULL));

        for (uint32_t i = 0; i < numCommands; ++i) {
            commands[i]->FillCommands(commandList);
        }

        ASSERT_SUCCESS(commandList->Close());

        ID3D12CommandList* commandLists[] = { commandList.Get() };
        device->GetCommandQueue()->ExecuteCommandLists(_countof(commandLists), commandLists);
    }

}
}
