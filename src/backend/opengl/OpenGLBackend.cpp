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

#include "backend/opengl/OpenGLBackend.h"

#include "backend/opengl/CommandBufferGL.h"
#include "backend/opengl/DepthStencilStateGL.h"
#include "backend/opengl/PipelineGL.h"
#include "backend/opengl/PipelineLayoutGL.h"
#include "backend/opengl/ShaderModuleGL.h"
#include "backend/opengl/SamplerGL.h"
#include "backend/opengl/TextureGL.h"

namespace backend {
namespace opengl {
    nxtProcTable GetNonValidatingProcs();
    nxtProcTable GetValidatingProcs();

    void HACKCLEAR(nxtDevice device) {
        Device* backendDevice = reinterpret_cast<Device*>(device);
        backendDevice->HACKCLEAR();
    }

    void Init(void* (*getProc)(const char*), nxtProcTable* procs, nxtDevice* device) {
        *device = nullptr;

        gladLoadGLLoader(reinterpret_cast<GLADloadproc>(getProc));

        *procs = GetValidatingProcs();
        *device = reinterpret_cast<nxtDevice>(new Device);

        glEnable(GL_DEPTH_TEST);
        HACKCLEAR(*device);
    }

    void InitBackbuffer(nxtDevice device) {
        Device* backendDevice = reinterpret_cast<Device*>(device);
        backendDevice->InitBackbuffer();
    }

    void CommitBackbuffer(nxtDevice device) {
        Device* backendDevice = reinterpret_cast<Device*>(device);
        backendDevice->CommitBackbuffer();
    }

    // Device

    BindGroupBase* Device::CreateBindGroup(BindGroupBuilder* builder) {
        return new BindGroup(builder);
    }
    BindGroupLayoutBase* Device::CreateBindGroupLayout(BindGroupLayoutBuilder* builder) {
        return new BindGroupLayout(builder);
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
    DepthStencilStateBase* Device::CreateDepthStencilState(DepthStencilStateBuilder* builder) {
        return new DepthStencilState(builder);
    }
    InputStateBase* Device::CreateInputState(InputStateBuilder* builder) {
        return new InputState(builder);
    }
    FramebufferBase* Device::CreateFramebuffer(FramebufferBuilder* builder) {
        return new Framebuffer(builder);
    }
    PipelineBase* Device::CreatePipeline(PipelineBuilder* builder) {
        return new Pipeline(builder);
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
    SamplerBase* Device::CreateSampler(SamplerBuilder* builder) {
        return new Sampler(builder);
    }
    ShaderModuleBase* Device::CreateShaderModule(ShaderModuleBuilder* builder) {
        return new ShaderModule(builder);
    }
    TextureBase* Device::CreateTexture(TextureBuilder* builder) {
        return new Texture(builder);
    }
    TextureViewBase* Device::CreateTextureView(TextureViewBuilder* builder) {
        return new TextureView(builder);
    }

    void Device::TickImpl() {
    }

    void Device::HACKCLEAR() {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, backFBO);
        glClearColor(0, 0, 0, 1);
        glStencilMask(0xff);
        glClearStencil(0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    }

    void Device::InitBackbuffer() {
        glGenTextures(1, &backTexture);
        glBindTexture(GL_TEXTURE_2D, backTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 640, 480, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);

        glGenTextures(1, &backDepthTexture);
        glBindTexture(GL_TEXTURE_2D, backDepthTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, 640, 480, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);

        glGenFramebuffers(1, &backFBO);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, backFBO);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_2D, backTexture, 0);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                GL_TEXTURE_2D, backDepthTexture, 0);
    }

    void Device::CommitBackbuffer() {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, backFBO);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, 640, 480, 0, 0, 640, 480,
                GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }

    GLuint Device::GetCurrentTexture() {
        return backTexture;
    }

    GLuint Device::GetCurrentDepthTexture() {
        return backDepthTexture;
    }

    // Bind Group

    BindGroup::BindGroup(BindGroupBuilder* builder)
        : BindGroupBase(builder) {
    }

    // Bind Group Layout

    BindGroupLayout::BindGroupLayout(BindGroupLayoutBuilder* builder)
        : BindGroupLayoutBase(builder) {
    }

    // Buffer

    Buffer::Buffer(BufferBuilder* builder)
        : BufferBase(builder) {
        glGenBuffers(1, &buffer);
        glBindBuffer(GL_ARRAY_BUFFER, buffer);
        glBufferData(GL_ARRAY_BUFFER, GetSize(), nullptr, GL_STATIC_DRAW);
    }

    GLuint Buffer::GetHandle() const {
        return buffer;
    }

    void Buffer::SetSubDataImpl(uint32_t start, uint32_t count, const uint32_t* data) {
        glBindBuffer(GL_ARRAY_BUFFER, buffer);
        glBufferSubData(GL_ARRAY_BUFFER, start * sizeof(uint32_t), count * sizeof(uint32_t), data);
    }

    void Buffer::MapReadAsyncImpl(uint32_t serial, uint32_t start, uint32_t count) {
        // TODO(cwallez@chromium.org): Implement Map Read for the GL backend
    }

    void Buffer::UnmapImpl() {
        // TODO(cwallez@chromium.org): Implement Map Read for the GL backend
    }

    void Buffer::TransitionUsageImpl(nxt::BufferUsageBit currentUsage, nxt::BufferUsageBit targetUsage) {
    }

    // BufferView

    BufferView::BufferView(BufferViewBuilder* builder)
        : BufferViewBase(builder) {
    }

    // InputState

    InputState::InputState(InputStateBuilder* builder)
        : InputStateBase(builder) {
        glGenVertexArrays(1, &vertexArrayObject);
        glBindVertexArray(vertexArrayObject);
        auto& attributesSetMask = GetAttributesSetMask();
        for (uint32_t location = 0; location < attributesSetMask.size(); ++location) {
            if (!attributesSetMask[location]) {
                continue;
            }
            auto attribute = GetAttribute(location);
            glEnableVertexAttribArray(location);

            auto input = GetInput(attribute.bindingSlot);
            if (input.stride == 0) {
                // Emulate a stride of zero (constant vertex attribute) by
                // setting the attribute instance divisor to a huge number.
                glVertexAttribDivisor(location, 0xffffffff);
            } else {
                switch (input.stepMode) {
                    case nxt::InputStepMode::Vertex:
                        break;
                    case nxt::InputStepMode::Instance:
                        glVertexAttribDivisor(location, 1);
                        break;
                    default:
                        ASSERT(false);
                        break;
                }
            }
        }
    }

    GLuint InputState::GetVAO() {
        return vertexArrayObject;
    }

    // Framebuffer

    Framebuffer::Framebuffer(FramebufferBuilder* builder)
        : FramebufferBase(builder) {
    }

    // Queue

    Queue::Queue(QueueBuilder* builder)
        : QueueBase(builder) {
    }

    void Queue::Submit(uint32_t numCommands, CommandBuffer* const * commands) {
        for (uint32_t i = 0; i < numCommands; ++i) {
            commands[i]->Execute();
        }
    }

    // RenderPass

    RenderPass::RenderPass(RenderPassBuilder* builder)
        : RenderPassBase(builder) {
    }

}
}
