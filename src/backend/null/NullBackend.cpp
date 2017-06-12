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

#include "NullBackend.h"

#include <spirv-cross/spirv_cross.hpp>

namespace backend {
namespace null {

    nxtProcTable GetNonValidatingProcs();
    nxtProcTable GetValidatingProcs();

    void Init(nxtProcTable* procs, nxtDevice* device) {
        *procs = GetValidatingProcs();
        *device = reinterpret_cast<nxtDevice>(new Device);
    }

    // Device

    Device::Device() {
    }

    Device::~Device() {
    }

    BindGroupBase* Device::CreateBindGroup(BindGroupBuilder* builder) {
        return new BindGroupBase(builder);
    }
    BindGroupLayoutBase* Device::CreateBindGroupLayout(BindGroupLayoutBuilder* builder) {
        return new BindGroupLayoutBase(builder);
    }
    BufferBase* Device::CreateBuffer(BufferBuilder* builder) {
        return new Buffer(builder);
    }
    BufferViewBase* Device::CreateBufferView(BufferViewBuilder* builder) {
        return new BufferViewBase(builder);
    }
    CommandBufferBase* Device::CreateCommandBuffer(CommandBufferBuilder* builder) {
        return new CommandBufferBase(builder);
    }
    DepthStencilStateBase* Device::CreateDepthStencilState(DepthStencilStateBuilder* builder) {
        return new DepthStencilStateBase(builder);
    }
    InputStateBase* Device::CreateInputState(InputStateBuilder* builder) {
        return new InputStateBase(builder);
    }
    FramebufferBase* Device::CreateFramebuffer(FramebufferBuilder* builder) {
        return new FramebufferBase(builder);
    }
    PipelineBase* Device::CreatePipeline(PipelineBuilder* builder) {
        return new PipelineBase(builder);
    }
    PipelineLayoutBase* Device::CreatePipelineLayout(PipelineLayoutBuilder* builder) {
        return new PipelineLayoutBase(builder);
    }
    QueueBase* Device::CreateQueue(QueueBuilder* builder) {
        return new Queue(builder);
    }
    RenderPassBase* Device::CreateRenderPass(RenderPassBuilder* builder) {
        return new RenderPassBase(builder);
    }
    SamplerBase* Device::CreateSampler(SamplerBuilder* builder) {
        return new SamplerBase(builder);
    }
    ShaderModuleBase* Device::CreateShaderModule(ShaderModuleBuilder* builder) {
        auto module = new ShaderModuleBase(builder);

        spirv_cross::Compiler compiler(builder->AcquireSpirv());
        module->ExtractSpirvInfo(compiler);

        return module;
    }
    TextureBase* Device::CreateTexture(TextureBuilder* builder) {
        return new TextureBase(builder);
    }
    TextureViewBase* Device::CreateTextureView(TextureViewBuilder* builder) {
        return new TextureViewBase(builder);
    }

    void Device::AddPendingOperation(std::unique_ptr<PendingOperation> operation) {
        pendingOperations.emplace_back(std::move(operation));
    }
    std::vector<std::unique_ptr<PendingOperation>> Device::AcquirePendingOperations() {
        return std::move(pendingOperations);
    }

    void Device::Reference() {
    }

    void Device::Release() {
    }

    // Buffer

    struct BufferMapReadOperation : PendingOperation {
        virtual void Execute() {
            buffer->MapReadOperationCompleted(serial, ptr);
        }

        Ref<Buffer> buffer;
        const void* ptr;
        uint32_t serial;
    };

    Buffer::Buffer(BufferBuilder* builder)
        : BufferBase(builder) {
        if (GetAllowedUsage() & (nxt::BufferUsageBit::TransferDst | nxt::BufferUsageBit::MapRead | nxt::BufferUsageBit::MapWrite)) {
            backingData = std::unique_ptr<char[]>(new char[GetSize()]);
        }
    }

    Buffer::~Buffer() {
    }

    void Buffer::MapReadOperationCompleted(uint32_t serial, const void* ptr) {
        CallMapReadCallback(serial, NXT_BUFFER_MAP_READ_STATUS_SUCCESS, ptr);
    }

    void Buffer::SetSubDataImpl(uint32_t start, uint32_t count, const uint32_t* data) {
        ASSERT(start + count <= GetSize());
        ASSERT(backingData);
        memcpy(backingData.get() + start, data, count);
    }

    void Buffer::MapReadAsyncImpl(uint32_t serial, uint32_t start, uint32_t count) {
        ASSERT(start + count <= GetSize());
        ASSERT(backingData);

        auto operation = new BufferMapReadOperation;
        operation->buffer = this;
        operation->ptr = backingData.get() + start;
        operation->serial = serial;

        ToBackend(GetDevice())->AddPendingOperation(std::unique_ptr<PendingOperation>(operation));
    }

    void Buffer::UnmapImpl() {
    }

    // Queue

    Queue::Queue(QueueBuilder* builder)
        : QueueBase(builder) {
    }

    Queue::~Queue() {
    }

    void Queue::Submit(uint32_t numCommands, CommandBuffer* const * commands) {
        auto operations = ToBackend(GetDevice())->AcquirePendingOperations();

        for (auto& operation : operations) {
            operation->Execute();
        }

        operations.clear();
    }

}
}
