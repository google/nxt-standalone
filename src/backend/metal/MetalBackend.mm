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

#include "backend/metal/MetalBackend.h"

#include "backend/metal/BlendStateMTL.h"
#include "backend/metal/BufferMTL.h"
#include "backend/metal/CommandBufferMTL.h"
#include "backend/metal/ComputePipelineMTL.h"
#include "backend/metal/DepthStencilStateMTL.h"
#include "backend/metal/InputStateMTL.h"
#include "backend/metal/RenderPipelineMTL.h"
#include "backend/metal/PipelineLayoutMTL.h"
#include "backend/metal/ResourceUploader.h"
#include "backend/metal/SamplerMTL.h"
#include "backend/metal/ShaderModuleMTL.h"
#include "backend/metal/SwapChainMTL.h"
#include "backend/metal/TextureMTL.h"

#include <unistd.h>

namespace backend {
namespace metal {
    nxtProcTable GetNonValidatingProcs();
    nxtProcTable GetValidatingProcs();

    void Init(id<MTLDevice> metalDevice, nxtProcTable* procs, nxtDevice* device) {
        *device = nullptr;

        *procs = GetValidatingProcs();
        *device = reinterpret_cast<nxtDevice>(new Device(metalDevice));
    }

    // Device

    Device::Device(id<MTLDevice> mtlDevice)
        : mtlDevice(mtlDevice), mapReadTracker(new MapReadRequestTracker(this)),
            resourceUploader(new ResourceUploader(this)) {
        [mtlDevice retain];
        commandQueue = [mtlDevice newCommandQueue];
    }

    Device::~Device() {
        // Wait for all commands to be finished so we can free resources
        // SubmitPendingCommandBuffer may not increment the pendingCommandSerial if there
        // are no pending commands, so we can't store the pendingSerial before
        // SubmitPendingCommandBuffer then wait for it to be passed. Instead we submit and
        // wait for the serial before the next pendingCommandSerial.
        SubmitPendingCommandBuffer();
        while (finishedCommandSerial != pendingCommandSerial - 1) {
            usleep(100);
        }
        Tick();

        [pendingCommands release];
        pendingCommands = nil;

        delete mapReadTracker;
        mapReadTracker = nullptr;

        delete resourceUploader;
        resourceUploader = nullptr;

        [mtlDevice release];
        mtlDevice = nil;

        [commandQueue release];
        commandQueue = nil;
    }

    BindGroupBase* Device::CreateBindGroup(BindGroupBuilder* builder) {
        return new BindGroup(builder);
    }
    BindGroupLayoutBase* Device::CreateBindGroupLayout(BindGroupLayoutBuilder* builder) {
        return new BindGroupLayout(builder);
    }
    BlendStateBase* Device::CreateBlendState(BlendStateBuilder* builder) {
        return new BlendState(builder);
    }
    BufferBase* Device::CreateBuffer(BufferBuilder* builder) {
        return new Buffer(builder);
    }
    BufferViewBase* Device::CreateBufferView(BufferViewBuilder* builder) {
        return new BufferView(builder);
    }
    CommandBufferBase* Device::CreateCommandBuffer(CommandBufferBuilder* builder) {
        return new CommandBuffer(builder);
    }
    ComputePipelineBase* Device::CreateComputePipeline(ComputePipelineBuilder* builder) {
        return new ComputePipeline(builder);
    }
    DepthStencilStateBase* Device::CreateDepthStencilState(DepthStencilStateBuilder* builder) {
        return new DepthStencilState(builder);
    }
    FramebufferBase* Device::CreateFramebuffer(FramebufferBuilder* builder) {
        return new Framebuffer(builder);
    }
    InputStateBase* Device::CreateInputState(InputStateBuilder* builder) {
        return new InputState(builder);
    }
    PipelineLayoutBase* Device::CreatePipelineLayout(PipelineLayoutBuilder* builder) {
        return new PipelineLayout(builder);
    }
    QueueBase* Device::CreateQueue(QueueBuilder* builder) {
        return new Queue(builder);
    }
    RenderPassBase* Device::CreateRenderPass(RenderPassBuilder* builder) {
        return new RenderPass(builder);
    }
    RenderPipelineBase* Device::CreateRenderPipeline(RenderPipelineBuilder* builder) {
        return new RenderPipeline(builder);
    }
    SamplerBase* Device::CreateSampler(SamplerBuilder* builder) {
        return new Sampler(builder);
    }
    ShaderModuleBase* Device::CreateShaderModule(ShaderModuleBuilder* builder) {
        return new ShaderModule(builder);
    }
    SwapChainBase* Device::CreateSwapChain(SwapChainBuilder* builder) {
        return new SwapChain(builder);
    }
    TextureBase* Device::CreateTexture(TextureBuilder* builder) {
        return new Texture(builder);
    }
    TextureViewBase* Device::CreateTextureView(TextureViewBuilder* builder) {
        return new TextureView(builder);
    }

    void Device::TickImpl() {
        resourceUploader->Tick(finishedCommandSerial);
        mapReadTracker->Tick(finishedCommandSerial);

        // Code above might have added GPU work, submit it. This also makes sure
        // that even when no GPU work is happening, the serial number keeps incrementing.
        SubmitPendingCommandBuffer();
    }

    id<MTLDevice> Device::GetMTLDevice() {
        return mtlDevice;
    }

    id<MTLCommandBuffer> Device::GetPendingCommandBuffer() {
        if (pendingCommands == nil) {
            pendingCommands = [commandQueue commandBuffer];
            [pendingCommands retain];
        }
        return pendingCommands;
    }

    void Device::SubmitPendingCommandBuffer() {
        if (pendingCommands == nil) {
            return;
        }

        // Ok, ObjC blocks are weird. My understanding is that local variables are captured by value
        // so this-> works as expected. However it is unclear how members are captured, (are they
        // captured using this-> or by value?) so we make a copy of the pendingCommandSerial on the stack.
        Serial pendingSerial = pendingCommandSerial;
        [pendingCommands addCompletedHandler:^(id<MTLCommandBuffer>) {
            this->finishedCommandSerial = pendingSerial;
        }];

        [pendingCommands commit];
        [pendingCommands release];
        pendingCommands = nil;
        pendingCommandSerial ++;
    }

    uint64_t Device::GetPendingCommandSerial() {
        // If this is called, then it means some piece of code somewhere will wait for this serial to
        // complete. Make sure the pending command buffer is created so that it is on the worst case
        // enqueued on the next Tick() and eventually increments the serial. Otherwise if no GPU work
        // happens we could be waiting for this serial forever.
        GetPendingCommandBuffer();
        return pendingCommandSerial;
    }

    MapReadRequestTracker* Device::GetMapReadTracker() const {
        return mapReadTracker;
    }

    ResourceUploader* Device::GetResourceUploader() const {
        return resourceUploader;
    }

    // Bind Group

    BindGroup::BindGroup(BindGroupBuilder* builder)
        : BindGroupBase(builder) {
    }

    // Bind Group Layout

    BindGroupLayout::BindGroupLayout(BindGroupLayoutBuilder* builder)
        : BindGroupLayoutBase(builder) {
    }

    // Framebuffer

    Framebuffer::Framebuffer(FramebufferBuilder* builder)
        : FramebufferBase(builder) {
    }

    Framebuffer::~Framebuffer() {
    }

    // Queue

    Queue::Queue(QueueBuilder* builder)
        : QueueBase(builder) {
        Device* device = ToBackend(builder->GetDevice());
        commandQueue = [device->GetMTLDevice() newCommandQueue];
    }

    Queue::~Queue() {
        [commandQueue release];
        commandQueue = nil;
    }

    id<MTLCommandQueue> Queue::GetMTLCommandQueue() {
        return commandQueue;
    }

    void Queue::Submit(uint32_t numCommands, CommandBuffer* const * commands) {
        Device* device = ToBackend(GetDevice());
        id<MTLCommandBuffer> commandBuffer = device->GetPendingCommandBuffer();

        for (uint32_t i = 0; i < numCommands; ++i) {
            commands[i]->FillCommands(commandBuffer);
        }

        device->SubmitPendingCommandBuffer();
    }

    // RenderPass

    RenderPass::RenderPass(RenderPassBuilder* builder)
        : RenderPassBase(builder) {
    }

    RenderPass::~RenderPass() {
    }

}
}
