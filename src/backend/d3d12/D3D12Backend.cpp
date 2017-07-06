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

#include "D3D12Backend.h"

#include "BindGroupD3D12.h"
#include "BindGroupLayoutD3D12.h"
#include "BufferD3D12.h"
#include "CommandBufferD3D12.h"
#include "InputStateD3D12.h"
#include "PipelineD3D12.h"
#include "PipelineLayoutD3D12.h"
#include "QueueD3D12.h"
#include "SamplerD3D12.h"
#include "ShaderModuleD3D12.h"
#include "TextureD3D12.h"

#include "CommandAllocatorManager.h"
#include "DescriptorHeapAllocator.h"
#include "ResourceAllocator.h"
#include "ResourceUploader.h"

namespace backend {
namespace d3d12 {

    nxtProcTable GetNonValidatingProcs();
    nxtProcTable GetValidatingProcs();

    void Init(ComPtr<ID3D12Device> d3d12Device, nxtProcTable* procs, nxtDevice* device) {
        *device = nullptr;
        *procs = GetValidatingProcs();
        *device = reinterpret_cast<nxtDevice>(new Device(d3d12Device));
    }

    ComPtr<ID3D12CommandQueue> GetCommandQueue(nxtDevice device) {
        Device* backendDevice = reinterpret_cast<Device*>(device);
        return backendDevice->GetCommandQueue();
    }

    void SetNextRenderTargetDescriptor(nxtDevice device, D3D12_CPU_DESCRIPTOR_HANDLE renderTargetDescriptor) {
        Device* backendDevice = reinterpret_cast<Device*>(device);
        backendDevice->SetNextRenderTargetDescriptor(renderTargetDescriptor);
    }

    uint64_t GetSerial(const nxtDevice device) {
        const Device* backendDevice = reinterpret_cast<const Device*>(device);
        return backendDevice->GetSerial();
    }

    void NextSerial(nxtDevice device) {
        Device* backendDevice = reinterpret_cast<Device*>(device);
        backendDevice->NextSerial();
    }

    void ExecuteCommandLists(nxtDevice device, std::initializer_list<ID3D12CommandList*> commandLists) {
        Device* backendDevice = reinterpret_cast<Device*>(device);
        backendDevice->ExecuteCommandLists(commandLists);
    }

    void WaitForSerial(nxtDevice device, uint64_t serial) {
        Device* backendDevice = reinterpret_cast<Device*>(device);
        backendDevice->WaitForSerial(serial);
    }

    void OpenCommandList(nxtDevice device, ComPtr<ID3D12GraphicsCommandList>* commandList) {
        Device* backendDevice = reinterpret_cast<Device*>(device);
        return backendDevice->OpenCommandList(commandList);
    }

    void ASSERT_SUCCESS(HRESULT hr) {
        ASSERT(SUCCEEDED(hr));
    }

    Device::Device(ComPtr<ID3D12Device> d3d12Device)
        : d3d12Device(d3d12Device),
          commandAllocatorManager(new CommandAllocatorManager(this)),
          descriptorHeapAllocator(new DescriptorHeapAllocator(this)),
          mapReadRequestTracker(new MapReadRequestTracker(this)),
          resourceAllocator(new ResourceAllocator(this)),
          resourceUploader(new ResourceUploader(this)) {

        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        ASSERT_SUCCESS(d3d12Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue)));

        ASSERT_SUCCESS(d3d12Device->CreateFence(serial, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
        fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        ASSERT(fenceEvent != nullptr);
    }

    Device::~Device() {
    }

    ComPtr<ID3D12Device> Device::GetD3D12Device() {
        return d3d12Device;
    }

    ComPtr<ID3D12CommandQueue> Device::GetCommandQueue() {
        return commandQueue;
    }

    DescriptorHeapAllocator* Device::GetDescriptorHeapAllocator() {
        return descriptorHeapAllocator;
    }

    MapReadRequestTracker* Device::GetMapReadRequestTracker() const {
        return mapReadRequestTracker;
    }

    ResourceAllocator* Device::GetResourceAllocator() {
        return resourceAllocator;
    }

    ResourceUploader* Device::GetResourceUploader() {
        return resourceUploader;
    }

    void Device::OpenCommandList(ComPtr<ID3D12GraphicsCommandList>* commandList) {
        ComPtr<ID3D12GraphicsCommandList> &cmdList = *commandList;
        if (!cmdList) {
            ASSERT_SUCCESS(d3d12Device->CreateCommandList(
                0,
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                commandAllocatorManager->ReserveCommandAllocator().Get(),
                nullptr,
                IID_PPV_ARGS(&cmdList)
            ));
        } else {
            ASSERT_SUCCESS(cmdList->Reset(commandAllocatorManager->ReserveCommandAllocator().Get(), nullptr));
        }
    }

    ComPtr<ID3D12GraphicsCommandList> Device::GetPendingCommandList() {
        // Callers of GetPendingCommandList do so to record commands. Only reserve a command allocator when it is needed so we don't submit empty command lists
        if (!pendingCommands.open) {
            OpenCommandList(&pendingCommands.commandList);
            pendingCommands.open = true;
        }
        return pendingCommands.commandList;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE Device::GetCurrentRenderTargetDescriptor() {
        return renderTargetDescriptor;
    }

    void Device::SetNextRenderTargetDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE renderTargetDescriptor) {
        this->renderTargetDescriptor = renderTargetDescriptor;
        static const float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
        GetPendingCommandList()->ClearRenderTargetView(renderTargetDescriptor, clearColor, 0, nullptr);
    }

    void Device::TickImpl() {
        // Perform cleanup operations to free unused objects
        const uint64_t lastCompletedSerial = fence->GetCompletedValue();
        resourceAllocator->Tick(lastCompletedSerial);
        commandAllocatorManager->Tick(lastCompletedSerial);
        descriptorHeapAllocator->Tick(lastCompletedSerial);
        mapReadRequestTracker->Tick(lastCompletedSerial);
        ExecuteCommandLists({});
        NextSerial();
    }

    uint64_t Device::GetSerial() const {
        return serial;
    }

    void Device::NextSerial() {
        ASSERT_SUCCESS(commandQueue->Signal(fence.Get(), serial++));
    }

    void Device::WaitForSerial(uint64_t serial) {
        const uint64_t lastCompletedSerial = fence->GetCompletedValue();
        if (lastCompletedSerial < serial) {
            ASSERT_SUCCESS(fence->SetEventOnCompletion(serial, fenceEvent));
            WaitForSingleObject(fenceEvent, INFINITE);
        }
    }

    void Device::ExecuteCommandLists(std::initializer_list<ID3D12CommandList*> commandLists) {
        // If there are pending commands, prepend them to ExecuteCommandLists
        if (pendingCommands.open) {
            std::vector<ID3D12CommandList*> lists(commandLists.size() + 1);
            pendingCommands.commandList->Close();
            pendingCommands.open = false;
            lists[0] = pendingCommands.commandList.Get();
            std::copy(commandLists.begin(), commandLists.end(), lists.begin() + 1);
            commandQueue->ExecuteCommandLists(commandLists.size() + 1, lists.data());
        } else {
            std::vector<ID3D12CommandList*> lists(commandLists);
            commandQueue->ExecuteCommandLists(commandLists.size(), lists.data());
        }
    }

    BindGroupBase* Device::CreateBindGroup(BindGroupBuilder* builder) {
        return new BindGroup(this, builder);
    }
    BindGroupLayoutBase* Device::CreateBindGroupLayout(BindGroupLayoutBuilder* builder) {
        return new BindGroupLayout(this, builder);
    }
    BufferBase* Device::CreateBuffer(BufferBuilder* builder) {
        return new Buffer(this, builder);
    }
    BufferViewBase* Device::CreateBufferView(BufferViewBuilder* builder) {
        return new BufferView(builder);
    }
    CommandBufferBase* Device::CreateCommandBuffer(CommandBufferBuilder* builder) {
        return new CommandBuffer(this, builder);
    }
    DepthStencilStateBase* Device::CreateDepthStencilState(DepthStencilStateBuilder* builder) {
        return new DepthStencilState(this, builder);
    }
    InputStateBase* Device::CreateInputState(InputStateBuilder* builder) {
        return new InputState(this, builder);
    }
    FramebufferBase* Device::CreateFramebuffer(FramebufferBuilder* builder) {
        return new Framebuffer(this, builder);
    }
    PipelineBase* Device::CreatePipeline(PipelineBuilder* builder) {
        return new Pipeline(this, builder);
    }
    PipelineLayoutBase* Device::CreatePipelineLayout(PipelineLayoutBuilder* builder) {
        return new PipelineLayout(this, builder);
    }
    QueueBase* Device::CreateQueue(QueueBuilder* builder) {
        return new Queue(this, builder);
    }
    RenderPassBase* Device::CreateRenderPass(RenderPassBuilder* builder) {
        return new RenderPass(this, builder);
    }
    SamplerBase* Device::CreateSampler(SamplerBuilder* builder) {
        return new Sampler(builder);
    }
    ShaderModuleBase* Device::CreateShaderModule(ShaderModuleBuilder* builder) {
        return new ShaderModule(this, builder);
    }
    TextureBase* Device::CreateTexture(TextureBuilder* builder) {
        return new Texture(this, builder);
    }
    TextureViewBase* Device::CreateTextureView(TextureViewBuilder* builder) {
        return new TextureView(builder);
    }

    void Device::Reference() {
    }

    void Device::Release() {
    }

    // DepthStencilState

    DepthStencilState::DepthStencilState(Device* device, DepthStencilStateBuilder* builder)
        : DepthStencilStateBase(builder), device(device) {
    }

    // Framebuffer

    Framebuffer::Framebuffer(Device* device, FramebufferBuilder* builder)
        : FramebufferBase(builder), device(device) {
    }

    // RenderPass

    RenderPass::RenderPass(Device* device, RenderPassBuilder* builder)
        : RenderPassBase(builder), device(device) {
    }

}
}
