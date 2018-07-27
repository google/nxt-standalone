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

#ifndef DAWNNATIVE_METAL_DEVICEMTL_H_
#define DAWNNATIVE_METAL_DEVICEMTL_H_

#include "dawn_native/dawn_platform.h"

#include "common/Serial.h"
#include "dawn_native/Device.h"
#include "dawn_native/metal/Forward.h"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <type_traits>

namespace dawn_native { namespace metal {

    class MapRequestTracker;
    class ResourceUploader;

    class Device : public DeviceBase {
      public:
        Device(id<MTLDevice> mtlDevice);
        ~Device();

        BindGroupBase* CreateBindGroup(BindGroupBuilder* builder) override;
        BlendStateBase* CreateBlendState(BlendStateBuilder* builder) override;
        BufferBase* CreateBuffer(BufferBuilder* builder) override;
        BufferViewBase* CreateBufferView(BufferViewBuilder* builder) override;
        CommandBufferBase* CreateCommandBuffer(CommandBufferBuilder* builder) override;
        ComputePipelineBase* CreateComputePipeline(ComputePipelineBuilder* builder) override;
        DepthStencilStateBase* CreateDepthStencilState(DepthStencilStateBuilder* builder) override;
        InputStateBase* CreateInputState(InputStateBuilder* builder) override;
        RenderPassDescriptorBase* CreateRenderPassDescriptor(
            RenderPassDescriptorBuilder* builder) override;
        RenderPipelineBase* CreateRenderPipeline(RenderPipelineBuilder* builder) override;
        ShaderModuleBase* CreateShaderModule(ShaderModuleBuilder* builder) override;
        SwapChainBase* CreateSwapChain(SwapChainBuilder* builder) override;
        TextureBase* CreateTexture(TextureBuilder* builder) override;
        TextureViewBase* CreateTextureView(TextureViewBuilder* builder) override;

        void TickImpl() override;

        id<MTLDevice> GetMTLDevice();

        id<MTLCommandBuffer> GetPendingCommandBuffer();
        void SubmitPendingCommandBuffer();
        Serial GetPendingCommandSerial();

        MapRequestTracker* GetMapTracker() const;
        ResourceUploader* GetResourceUploader() const;

      private:
        ResultOrError<BindGroupLayoutBase*> CreateBindGroupLayoutImpl(
            const BindGroupLayoutDescriptor* descriptor) override;
        ResultOrError<PipelineLayoutBase*> CreatePipelineLayoutImpl(
            const PipelineLayoutDescriptor* descriptor) override;
        ResultOrError<QueueBase*> CreateQueueImpl() override;
        ResultOrError<SamplerBase*> CreateSamplerImpl(const SamplerDescriptor* descriptor) override;

        void OnCompletedHandler();

        id<MTLDevice> mMtlDevice = nil;
        id<MTLCommandQueue> mCommandQueue = nil;
        MapRequestTracker* mMapTracker;
        ResourceUploader* mResourceUploader;

        Serial mFinishedCommandSerial = 0;
        Serial mPendingCommandSerial = 1;
        id<MTLCommandBuffer> mPendingCommands = nil;
    };

}}  // namespace dawn_native::metal

#endif  // DAWNNATIVE_METAL_DEVICEMTL_H_
