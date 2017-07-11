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

#ifndef BACKEND_OPENGL_OPENGLBACKEND_H_
#define BACKEND_OPENGL_OPENGLBACKEND_H_

#include "nxt/nxtcpp.h"

#include "backend/Buffer.h"
#include "backend/BindGroup.h"
#include "backend/BindGroupLayout.h"
#include "backend/Device.h"
#include "backend/DepthStencilState.h"
#include "backend/Framebuffer.h"
#include "backend/InputState.h"
#include "backend/Queue.h"
#include "backend/RenderPass.h"
#include "backend/ToBackend.h"

#include "glad/glad.h"

namespace backend {
namespace opengl {

    class BindGroup;
    class BindGroupLayout;
    class Buffer;
    class BufferView;
    class CommandBuffer;
    class DepthStencilState;
    class Device;
    class InputState;
    class PersistentPipelineState;
    class Pipeline;
    class PipelineLayout;
    class Queue;
    class Sampler;
    class ShaderModule;
    class Texture;
    class TextureView;
    class Framebuffer;
    class RenderPass;

    struct OpenGLBackendTraits {
        using BindGroupType = BindGroup;
        using BindGroupLayoutType = BindGroupLayout;
        using BufferType = Buffer;
        using BufferViewType = BufferView;
        using CommandBufferType = CommandBuffer;
        using DepthStencilStateType = DepthStencilState;
        using DeviceType = Device;
        using InputStateType = InputState;
        using PipelineType = Pipeline;
        using PipelineLayoutType = PipelineLayout;
        using QueueType = Queue;
        using SamplerType = Sampler;
        using ShaderModuleType = ShaderModule;
        using TextureType = Texture;
        using TextureViewType = TextureView;
        using FramebufferType = Framebuffer;
        using RenderPassType = RenderPass;
    };

    template<typename T>
    auto ToBackend(T&& common) -> decltype(ToBackendBase<OpenGLBackendTraits>(common)) {
        return ToBackendBase<OpenGLBackendTraits>(common);
    }

    // Definition of backend types
    class Device : public DeviceBase {
        public:
            BindGroupBase* CreateBindGroup(BindGroupBuilder* builder) override;
            BindGroupLayoutBase* CreateBindGroupLayout(BindGroupLayoutBuilder* builder) override;
            BufferBase* CreateBuffer(BufferBuilder* builder) override;
            BufferViewBase* CreateBufferView(BufferViewBuilder* builder) override;
            CommandBufferBase* CreateCommandBuffer(CommandBufferBuilder* builder) override;
            DepthStencilStateBase* CreateDepthStencilState(DepthStencilStateBuilder* builder) override;
            InputStateBase* CreateInputState(InputStateBuilder* builder) override;
            FramebufferBase* CreateFramebuffer(FramebufferBuilder* builder) override;
            PipelineBase* CreatePipeline(PipelineBuilder* builder) override;
            PipelineLayoutBase* CreatePipelineLayout(PipelineLayoutBuilder* builder) override;
            QueueBase* CreateQueue(QueueBuilder* builder) override;
            RenderPassBase* CreateRenderPass(RenderPassBuilder* builder) override;
            SamplerBase* CreateSampler(SamplerBuilder* builder) override;
            ShaderModuleBase* CreateShaderModule(ShaderModuleBuilder* builder) override;
            TextureBase* CreateTexture(TextureBuilder* builder) override;
            TextureViewBase* CreateTextureView(TextureViewBuilder* builder) override;

            void TickImpl() override;

            void HACKCLEAR();
            void InitBackbuffer();
            void CommitBackbuffer();
            GLuint GetCurrentTexture();
            GLuint GetCurrentDepthTexture();

        private:
            GLuint backFBO = 0;
            GLuint backTexture = 0;
            GLuint backDepthTexture = 0;
    };

    class BindGroup : public BindGroupBase {
        public:
            BindGroup(BindGroupBuilder* builder);
    };

    class BindGroupLayout : public BindGroupLayoutBase {
        public:
            BindGroupLayout(BindGroupLayoutBuilder* builder);
    };

    class Buffer : public BufferBase {
        public:
            Buffer(BufferBuilder* builder);

            GLuint GetHandle() const;

        private:
            void SetSubDataImpl(uint32_t start, uint32_t count, const uint32_t* data) override;
            void MapReadAsyncImpl(uint32_t serial, uint32_t start, uint32_t count) override;
            void UnmapImpl() override;
            void TransitionUsageImpl(nxt::BufferUsageBit currentUsage, nxt::BufferUsageBit targetUsage) override;

            GLuint buffer = 0;
    };

    class BufferView : public BufferViewBase {
        public:
            BufferView(BufferViewBuilder* builder);
    };

    class Framebuffer : public FramebufferBase {
        public:
            Framebuffer(FramebufferBuilder* builder);
    };

    class InputState : public InputStateBase {
        public:
            InputState(InputStateBuilder* builder);
            GLuint GetVAO();

        private:
            GLuint vertexArrayObject;
    };

    class Queue : public QueueBase {
        public:
            Queue(QueueBuilder* builder);

            // NXT API
            void Submit(uint32_t numCommands, CommandBuffer* const * commands);
    };

    class RenderPass : public RenderPassBase {
        public:
            RenderPass(RenderPassBuilder* builder);
    };

}
}

#endif // BACKEND_OPENGL_OPENGLBACKEND_H_
