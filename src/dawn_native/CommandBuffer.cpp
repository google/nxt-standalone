// Copyright 2017 The Dawn Authors
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

#include "dawn_native/CommandBuffer.h"

#include "dawn_native/BindGroup.h"
#include "dawn_native/Buffer.h"
#include "dawn_native/CommandBufferStateTracker.h"
#include "dawn_native/Commands.h"
#include "dawn_native/ComputePipeline.h"
#include "dawn_native/Device.h"
#include "dawn_native/InputState.h"
#include "dawn_native/PipelineLayout.h"
#include "dawn_native/RenderPipeline.h"
#include "dawn_native/Texture.h"

#include <cstring>
#include <map>

namespace dawn_native {

    namespace {

        MaybeError ValidateCopyLocationFitsInTexture(const TextureCopyLocation& location) {
            const TextureBase* texture = location.texture.Get();
            if (location.level >= texture->GetNumMipLevels()) {
                return DAWN_VALIDATION_ERROR("Copy mip-level out of range");
            }

            if (location.slice >= texture->GetArrayLayers()) {
                return DAWN_VALIDATION_ERROR("Copy array-layer out of range");
            }

            // All texture dimensions are in uint32_t so by doing checks in uint64_t we avoid
            // overflows.
            uint64_t level = location.level;
            if (uint64_t(location.x) + uint64_t(location.width) >
                    (static_cast<uint64_t>(texture->GetWidth()) >> level) ||
                uint64_t(location.y) + uint64_t(location.height) >
                    (static_cast<uint64_t>(texture->GetHeight()) >> level)) {
                return DAWN_VALIDATION_ERROR("Copy would touch outside of the texture");
            }

            // TODO(cwallez@chromium.org): Check the depth bound differently for 2D arrays and 3D
            // textures
            if (location.z != 0 || location.depth != 1) {
                return DAWN_VALIDATION_ERROR("No support for z != 0 and depth != 1 for now");
            }

            return {};
        }

        bool FitsInBuffer(const BufferBase* buffer, uint32_t offset, uint32_t size) {
            uint32_t bufferSize = buffer->GetSize();
            return offset <= bufferSize && (size <= (bufferSize - offset));
        }

        MaybeError ValidateCopySizeFitsInBuffer(const BufferCopyLocation& location,
                                                uint32_t dataSize) {
            if (!FitsInBuffer(location.buffer.Get(), location.offset, dataSize)) {
                return DAWN_VALIDATION_ERROR("Copy would overflow the buffer");
            }

            return {};
        }

        MaybeError ValidateTexelBufferOffset(TextureBase* texture,
                                             const BufferCopyLocation& location) {
            uint32_t texelSize =
                static_cast<uint32_t>(TextureFormatPixelSize(texture->GetFormat()));
            if (location.offset % texelSize != 0) {
                return DAWN_VALIDATION_ERROR("Buffer offset must be a multiple of the texel size");
            }

            return {};
        }

        MaybeError ComputeTextureCopyBufferSize(const TextureCopyLocation& location,
                                                uint32_t rowPitch,
                                                uint32_t* bufferSize) {
            // TODO(cwallez@chromium.org): check for overflows
            *bufferSize = (rowPitch * (location.height - 1) + location.width) * location.depth;

            return {};
        }

        uint32_t ComputeDefaultRowPitch(TextureBase* texture, uint32_t width) {
            uint32_t texelSize = TextureFormatPixelSize(texture->GetFormat());
            return texelSize * width;
        }

        MaybeError ValidateRowPitch(const TextureCopyLocation& location, uint32_t rowPitch) {
            if (rowPitch % kTextureRowPitchAlignment != 0) {
                return DAWN_VALIDATION_ERROR("Row pitch must be a multiple of 256");
            }

            uint32_t texelSize = TextureFormatPixelSize(location.texture.Get()->GetFormat());
            if (rowPitch < location.width * texelSize) {
                return DAWN_VALIDATION_ERROR(
                    "Row pitch must not be less than the number of bytes per row");
            }

            return {};
        }

        MaybeError ValidateCanUseAs(BufferBase* buffer, dawn::BufferUsageBit usage) {
            ASSERT(HasZeroOrOneBits(usage));
            if (!(buffer->GetUsage() & usage)) {
                return DAWN_VALIDATION_ERROR("buffer doesn't have the required usage.");
            }

            return {};
        }

        MaybeError ValidateCanUseAs(TextureBase* texture, dawn::TextureUsageBit usage) {
            ASSERT(HasZeroOrOneBits(usage));
            if (!(texture->GetUsage() & usage)) {
                return DAWN_VALIDATION_ERROR("texture doesn't have the required usage.");
            }

            return {};
        }

        enum class PassType {
            Render,
            Compute,
        };

        // Helper class to encapsulate the logic of tracking per-resource usage during the
        // validation of command buffer passes. It is used both to know if there are validation
        // errors, and to get a list of resources used per pass for backends that need the
        // information.
        class PassResourceUsageTracker {
          public:
            void BufferUsedAs(BufferBase* buffer, dawn::BufferUsageBit usage) {
                // std::map's operator[] will create the key and return 0 if the key didn't exist
                // before.
                dawn::BufferUsageBit& storedUsage = mBufferUsages[buffer];

                if (usage == dawn::BufferUsageBit::Storage &&
                    storedUsage & dawn::BufferUsageBit::Storage) {
                    mStorageUsedMultipleTimes = true;
                }

                storedUsage |= usage;
            }

            void TextureUsedAs(TextureBase* texture, dawn::TextureUsageBit usage) {
                // std::map's operator[] will create the key and return 0 if the key didn't exist
                // before.
                dawn::TextureUsageBit& storedUsage = mTextureUsages[texture];

                if (usage == dawn::TextureUsageBit::Storage &&
                    storedUsage & dawn::TextureUsageBit::Storage) {
                    mStorageUsedMultipleTimes = true;
                }

                storedUsage |= usage;
            }

            // Performs the per-pass usage validation checks
            MaybeError ValidateUsages(PassType pass) const {
                // Storage resources cannot be used twice in the same compute pass
                if (pass == PassType::Compute && mStorageUsedMultipleTimes) {
                    return DAWN_VALIDATION_ERROR(
                        "Storage resource used multiple times in compute pass");
                }

                // Buffers can only be used as single-write or multiple read.
                for (auto& it : mBufferUsages) {
                    BufferBase* buffer = it.first;
                    dawn::BufferUsageBit usage = it.second;

                    if (usage & ~buffer->GetUsage()) {
                        return DAWN_VALIDATION_ERROR("Buffer missing usage for the pass");
                    }

                    bool readOnly = (usage & kReadOnlyBufferUsages) == usage;
                    bool singleUse = dawn::HasZeroOrOneBits(usage);

                    if (!readOnly && !singleUse) {
                        return DAWN_VALIDATION_ERROR(
                            "Buffer used as writeable usage and another usage in pass");
                    }
                }

                // Textures can only be used as single-write or multiple read.
                // TODO(cwallez@chromium.org): implement per-subresource tracking
                for (auto& it : mTextureUsages) {
                    TextureBase* texture = it.first;
                    dawn::TextureUsageBit usage = it.second;

                    if (usage & ~texture->GetUsage()) {
                        return DAWN_VALIDATION_ERROR("Texture missing usage for the pass");
                    }

                    // For textures the only read-only usage in a pass is Sampled, so checking the
                    // usage constraint simplifies to checking a single usage bit is set.
                    if (!dawn::HasZeroOrOneBits(it.second)) {
                        return DAWN_VALIDATION_ERROR(
                            "Texture used with more than one usage in pass");
                    }
                }

                return {};
            }

            // Returns the per-pass usage for use by backends for APIs with explicit barriers.
            PassResourceUsage AcquireResourceUsage() {
                PassResourceUsage result;
                result.buffers.reserve(mBufferUsages.size());
                result.bufferUsages.reserve(mBufferUsages.size());
                result.textures.reserve(mTextureUsages.size());
                result.textureUsages.reserve(mTextureUsages.size());

                for (auto& it : mBufferUsages) {
                    result.buffers.push_back(it.first);
                    result.bufferUsages.push_back(it.second);
                }

                for (auto& it : mTextureUsages) {
                    result.textures.push_back(it.first);
                    result.textureUsages.push_back(it.second);
                }

                return result;
            }

          private:
            std::map<BufferBase*, dawn::BufferUsageBit> mBufferUsages;
            std::map<TextureBase*, dawn::TextureUsageBit> mTextureUsages;
            bool mStorageUsedMultipleTimes = false;
        };

        void TrackBindGroupResourceUsage(BindGroupBase* group, PassResourceUsageTracker* tracker) {
            const auto& layoutInfo = group->GetLayout()->GetBindingInfo();

            for (uint32_t i : IterateBitSet(layoutInfo.mask)) {
                dawn::BindingType type = layoutInfo.types[i];

                switch (type) {
                    case dawn::BindingType::UniformBuffer: {
                        BufferBase* buffer = group->GetBindingAsBufferView(i)->GetBuffer();
                        tracker->BufferUsedAs(buffer, dawn::BufferUsageBit::Uniform);
                    } break;

                    case dawn::BindingType::StorageBuffer: {
                        BufferBase* buffer = group->GetBindingAsBufferView(i)->GetBuffer();
                        tracker->BufferUsedAs(buffer, dawn::BufferUsageBit::Storage);
                    } break;

                    case dawn::BindingType::SampledTexture: {
                        TextureBase* texture = group->GetBindingAsTextureView(i)->GetTexture();
                        tracker->TextureUsedAs(texture, dawn::TextureUsageBit::Sampled);
                    } break;

                    case dawn::BindingType::Sampler:
                        break;
                }
            }
        }

    }  // namespace

    // CommandBuffer

    CommandBufferBase::CommandBufferBase(CommandBufferBuilder* builder)
        : mDevice(builder->mDevice) {
    }

    DeviceBase* CommandBufferBase::GetDevice() {
        return mDevice;
    }

    // CommandBufferBuilder

    CommandBufferBuilder::CommandBufferBuilder(DeviceBase* device) : Builder(device) {
    }

    CommandBufferBuilder::~CommandBufferBuilder() {
        if (!mWereCommandsAcquired) {
            MoveToIterator();
            FreeCommands(&mIterator);
        }
    }

    CommandIterator CommandBufferBuilder::AcquireCommands() {
        ASSERT(!mWereCommandsAcquired);
        mWereCommandsAcquired = true;
        return std::move(mIterator);
    }

    std::vector<PassResourceUsage> CommandBufferBuilder::AcquirePassResourceUsage() {
        ASSERT(!mWerePassUsagesAcquired);
        mWerePassUsagesAcquired = true;
        return std::move(mPassResourceUsages);
    }

    CommandBufferBase* CommandBufferBuilder::GetResultImpl() {
        MoveToIterator();
        return mDevice->CreateCommandBuffer(this);
    }

    void CommandBufferBuilder::MoveToIterator() {
        if (!mWasMovedToIterator) {
            mIterator = std::move(mAllocator);
            mWasMovedToIterator = true;
        }
    }

    // Implementation of the command buffer validation that can be precomputed before submit

    MaybeError CommandBufferBuilder::ValidateGetResult() {
        MoveToIterator();
        mIterator.Reset();

        Command type;
        while (mIterator.NextCommandId(&type)) {
            switch (type) {
                case Command::BeginComputePass: {
                    mIterator.NextCommand<BeginComputePassCmd>();
                    DAWN_TRY(ValidateComputePass());
                } break;

                case Command::BeginRenderPass: {
                    BeginRenderPassCmd* cmd = mIterator.NextCommand<BeginRenderPassCmd>();
                    DAWN_TRY(ValidateRenderPass(cmd->info.Get()));
                } break;

                case Command::CopyBufferToBuffer: {
                    CopyBufferToBufferCmd* copy = mIterator.NextCommand<CopyBufferToBufferCmd>();

                    DAWN_TRY(ValidateCopySizeFitsInBuffer(copy->source, copy->size));
                    DAWN_TRY(ValidateCopySizeFitsInBuffer(copy->destination, copy->size));

                    DAWN_TRY(ValidateCanUseAs(copy->source.buffer.Get(),
                                              dawn::BufferUsageBit::TransferSrc));
                    DAWN_TRY(ValidateCanUseAs(copy->destination.buffer.Get(),
                                              dawn::BufferUsageBit::TransferDst));
                } break;

                case Command::CopyBufferToTexture: {
                    CopyBufferToTextureCmd* copy = mIterator.NextCommand<CopyBufferToTextureCmd>();

                    uint32_t bufferCopySize = 0;
                    DAWN_TRY(ValidateRowPitch(copy->destination, copy->rowPitch));
                    DAWN_TRY(ComputeTextureCopyBufferSize(copy->destination, copy->rowPitch,
                                                          &bufferCopySize));

                    DAWN_TRY(ValidateCopyLocationFitsInTexture(copy->destination));
                    DAWN_TRY(ValidateCopySizeFitsInBuffer(copy->source, bufferCopySize));
                    DAWN_TRY(
                        ValidateTexelBufferOffset(copy->destination.texture.Get(), copy->source));

                    DAWN_TRY(ValidateCanUseAs(copy->source.buffer.Get(),
                                              dawn::BufferUsageBit::TransferSrc));
                    DAWN_TRY(ValidateCanUseAs(copy->destination.texture.Get(),
                                              dawn::TextureUsageBit::TransferDst));
                } break;

                case Command::CopyTextureToBuffer: {
                    CopyTextureToBufferCmd* copy = mIterator.NextCommand<CopyTextureToBufferCmd>();

                    uint32_t bufferCopySize = 0;
                    DAWN_TRY(ValidateRowPitch(copy->source, copy->rowPitch));
                    DAWN_TRY(ComputeTextureCopyBufferSize(copy->source, copy->rowPitch,
                                                          &bufferCopySize));

                    DAWN_TRY(ValidateCopyLocationFitsInTexture(copy->source));
                    DAWN_TRY(ValidateCopySizeFitsInBuffer(copy->destination, bufferCopySize));
                    DAWN_TRY(
                        ValidateTexelBufferOffset(copy->source.texture.Get(), copy->destination));

                    DAWN_TRY(ValidateCanUseAs(copy->source.texture.Get(),
                                              dawn::TextureUsageBit::TransferSrc));
                    DAWN_TRY(ValidateCanUseAs(copy->destination.buffer.Get(),
                                              dawn::BufferUsageBit::TransferDst));
                } break;

                default:
                    return DAWN_VALIDATION_ERROR("Command disallowed outside of a pass");
            }
        }

        return {};
    }

    MaybeError CommandBufferBuilder::ValidateComputePass() {
        PassResourceUsageTracker usageTracker;
        CommandBufferStateTracker persistentState;

        Command type;
        while (mIterator.NextCommandId(&type)) {
            switch (type) {
                case Command::EndComputePass: {
                    mIterator.NextCommand<EndComputePassCmd>();

                    DAWN_TRY(usageTracker.ValidateUsages(PassType::Compute));
                    mPassResourceUsages.push_back(usageTracker.AcquireResourceUsage());
                    return {};
                } break;

                case Command::Dispatch: {
                    mIterator.NextCommand<DispatchCmd>();
                    DAWN_TRY(persistentState.ValidateCanDispatch());
                } break;

                case Command::SetComputePipeline: {
                    SetComputePipelineCmd* cmd = mIterator.NextCommand<SetComputePipelineCmd>();
                    ComputePipelineBase* pipeline = cmd->pipeline.Get();
                    persistentState.SetComputePipeline(pipeline);
                } break;

                case Command::SetPushConstants: {
                    SetPushConstantsCmd* cmd = mIterator.NextCommand<SetPushConstantsCmd>();
                    mIterator.NextData<uint32_t>(cmd->count);
                    // Validation of count and offset has already been done when the command was
                    // recorded because it impacts the size of an allocation in the
                    // CommandAllocator.
                    if (cmd->stages & ~dawn::ShaderStageBit::Compute) {
                        return DAWN_VALIDATION_ERROR(
                            "SetPushConstants stage must be compute or 0 in compute passes");
                    }
                } break;

                case Command::SetBindGroup: {
                    SetBindGroupCmd* cmd = mIterator.NextCommand<SetBindGroupCmd>();

                    TrackBindGroupResourceUsage(cmd->group.Get(), &usageTracker);
                    persistentState.SetBindGroup(cmd->index, cmd->group.Get());
                } break;

                default:
                    return DAWN_VALIDATION_ERROR("Command disallowed inside a compute pass");
            }
        }

        return DAWN_VALIDATION_ERROR("Unfinished compute pass");
    }

    MaybeError CommandBufferBuilder::ValidateRenderPass(RenderPassDescriptorBase* renderPass) {
        PassResourceUsageTracker usageTracker;
        CommandBufferStateTracker persistentState;

        // Track usage of the render pass attachments
        for (uint32_t i : IterateBitSet(renderPass->GetColorAttachmentMask())) {
            TextureBase* texture = renderPass->GetColorAttachment(i).view->GetTexture();
            usageTracker.TextureUsedAs(texture, dawn::TextureUsageBit::OutputAttachment);
        }

        if (renderPass->HasDepthStencilAttachment()) {
            TextureBase* texture = renderPass->GetDepthStencilAttachment().view->GetTexture();
            usageTracker.TextureUsedAs(texture, dawn::TextureUsageBit::OutputAttachment);
        }

        Command type;
        while (mIterator.NextCommandId(&type)) {
            switch (type) {
                case Command::EndRenderPass: {
                    mIterator.NextCommand<EndRenderPassCmd>();

                    DAWN_TRY(usageTracker.ValidateUsages(PassType::Render));
                    mPassResourceUsages.push_back(usageTracker.AcquireResourceUsage());
                    return {};
                } break;

                case Command::DrawArrays: {
                    mIterator.NextCommand<DrawArraysCmd>();
                    DAWN_TRY(persistentState.ValidateCanDrawArrays());
                } break;

                case Command::DrawElements: {
                    mIterator.NextCommand<DrawElementsCmd>();
                    DAWN_TRY(persistentState.ValidateCanDrawElements());
                } break;

                case Command::SetRenderPipeline: {
                    SetRenderPipelineCmd* cmd = mIterator.NextCommand<SetRenderPipelineCmd>();
                    RenderPipelineBase* pipeline = cmd->pipeline.Get();

                    if (!pipeline->IsCompatibleWith(renderPass)) {
                        return DAWN_VALIDATION_ERROR(
                            "Pipeline is incompatible with this render pass");
                    }

                    persistentState.SetRenderPipeline(pipeline);
                } break;

                case Command::SetPushConstants: {
                    SetPushConstantsCmd* cmd = mIterator.NextCommand<SetPushConstantsCmd>();
                    mIterator.NextData<uint32_t>(cmd->count);
                    // Validation of count and offset has already been done when the command was
                    // recorded because it impacts the size of an allocation in the
                    // CommandAllocator.
                    if (cmd->stages &
                        ~(dawn::ShaderStageBit::Vertex | dawn::ShaderStageBit::Fragment)) {
                        return DAWN_VALIDATION_ERROR(
                            "SetPushConstants stage must be a subset of (vertex|fragment) in "
                            "render passes");
                    }
                } break;

                case Command::SetStencilReference: {
                    mIterator.NextCommand<SetStencilReferenceCmd>();
                } break;

                case Command::SetBlendColor: {
                    mIterator.NextCommand<SetBlendColorCmd>();
                } break;

                case Command::SetScissorRect: {
                    mIterator.NextCommand<SetScissorRectCmd>();
                } break;

                case Command::SetBindGroup: {
                    SetBindGroupCmd* cmd = mIterator.NextCommand<SetBindGroupCmd>();

                    TrackBindGroupResourceUsage(cmd->group.Get(), &usageTracker);
                    persistentState.SetBindGroup(cmd->index, cmd->group.Get());
                } break;

                case Command::SetIndexBuffer: {
                    SetIndexBufferCmd* cmd = mIterator.NextCommand<SetIndexBufferCmd>();

                    usageTracker.BufferUsedAs(cmd->buffer.Get(), dawn::BufferUsageBit::Index);
                    persistentState.SetIndexBuffer();
                } break;

                case Command::SetVertexBuffers: {
                    SetVertexBuffersCmd* cmd = mIterator.NextCommand<SetVertexBuffersCmd>();
                    auto buffers = mIterator.NextData<Ref<BufferBase>>(cmd->count);
                    mIterator.NextData<uint32_t>(cmd->count);

                    for (uint32_t i = 0; i < cmd->count; ++i) {
                        usageTracker.BufferUsedAs(buffers[i].Get(), dawn::BufferUsageBit::Vertex);
                    }
                    persistentState.SetVertexBuffer(cmd->startSlot, cmd->count);
                } break;

                default:
                    return DAWN_VALIDATION_ERROR("Command disallowed inside a render pass");
            }
        }

        return DAWN_VALIDATION_ERROR("Unfinished render pass");
    }

    // Implementation of the API's command recording methods

    void CommandBufferBuilder::BeginComputePass() {
        mAllocator.Allocate<BeginComputePassCmd>(Command::BeginComputePass);
    }

    void CommandBufferBuilder::BeginRenderPass(RenderPassDescriptorBase* info) {
        BeginRenderPassCmd* cmd = mAllocator.Allocate<BeginRenderPassCmd>(Command::BeginRenderPass);
        new (cmd) BeginRenderPassCmd;
        cmd->info = info;
    }

    void CommandBufferBuilder::CopyBufferToBuffer(BufferBase* source,
                                                  uint32_t sourceOffset,
                                                  BufferBase* destination,
                                                  uint32_t destinationOffset,
                                                  uint32_t size) {
        CopyBufferToBufferCmd* copy =
            mAllocator.Allocate<CopyBufferToBufferCmd>(Command::CopyBufferToBuffer);
        new (copy) CopyBufferToBufferCmd;
        copy->source.buffer = source;
        copy->source.offset = sourceOffset;
        copy->destination.buffer = destination;
        copy->destination.offset = destinationOffset;
        copy->size = size;
    }

    void CommandBufferBuilder::CopyBufferToTexture(BufferBase* buffer,
                                                   uint32_t bufferOffset,
                                                   uint32_t rowPitch,
                                                   TextureBase* texture,
                                                   uint32_t x,
                                                   uint32_t y,
                                                   uint32_t z,
                                                   uint32_t width,
                                                   uint32_t height,
                                                   uint32_t depth,
                                                   uint32_t level,
                                                   uint32_t slice) {
        if (rowPitch == 0) {
            rowPitch = ComputeDefaultRowPitch(texture, width);
        }
        CopyBufferToTextureCmd* copy =
            mAllocator.Allocate<CopyBufferToTextureCmd>(Command::CopyBufferToTexture);
        new (copy) CopyBufferToTextureCmd;
        copy->source.buffer = buffer;
        copy->source.offset = bufferOffset;
        copy->destination.texture = texture;
        copy->destination.x = x;
        copy->destination.y = y;
        copy->destination.z = z;
        copy->destination.width = width;
        copy->destination.height = height;
        copy->destination.depth = depth;
        copy->destination.level = level;
        copy->destination.slice = slice;
        copy->rowPitch = rowPitch;
    }

    void CommandBufferBuilder::CopyTextureToBuffer(TextureBase* texture,
                                                   uint32_t x,
                                                   uint32_t y,
                                                   uint32_t z,
                                                   uint32_t width,
                                                   uint32_t height,
                                                   uint32_t depth,
                                                   uint32_t level,
                                                   uint32_t slice,
                                                   BufferBase* buffer,
                                                   uint32_t bufferOffset,
                                                   uint32_t rowPitch) {
        if (rowPitch == 0) {
            rowPitch = ComputeDefaultRowPitch(texture, width);
        }
        CopyTextureToBufferCmd* copy =
            mAllocator.Allocate<CopyTextureToBufferCmd>(Command::CopyTextureToBuffer);
        new (copy) CopyTextureToBufferCmd;
        copy->source.texture = texture;
        copy->source.x = x;
        copy->source.y = y;
        copy->source.z = z;
        copy->source.width = width;
        copy->source.height = height;
        copy->source.depth = depth;
        copy->source.level = level;
        copy->source.slice = slice;
        copy->destination.buffer = buffer;
        copy->destination.offset = bufferOffset;
        copy->rowPitch = rowPitch;
    }

    void CommandBufferBuilder::Dispatch(uint32_t x, uint32_t y, uint32_t z) {
        DispatchCmd* dispatch = mAllocator.Allocate<DispatchCmd>(Command::Dispatch);
        new (dispatch) DispatchCmd;
        dispatch->x = x;
        dispatch->y = y;
        dispatch->z = z;
    }

    void CommandBufferBuilder::DrawArrays(uint32_t vertexCount,
                                          uint32_t instanceCount,
                                          uint32_t firstVertex,
                                          uint32_t firstInstance) {
        DrawArraysCmd* draw = mAllocator.Allocate<DrawArraysCmd>(Command::DrawArrays);
        new (draw) DrawArraysCmd;
        draw->vertexCount = vertexCount;
        draw->instanceCount = instanceCount;
        draw->firstVertex = firstVertex;
        draw->firstInstance = firstInstance;
    }

    void CommandBufferBuilder::DrawElements(uint32_t indexCount,
                                            uint32_t instanceCount,
                                            uint32_t firstIndex,
                                            uint32_t firstInstance) {
        DrawElementsCmd* draw = mAllocator.Allocate<DrawElementsCmd>(Command::DrawElements);
        new (draw) DrawElementsCmd;
        draw->indexCount = indexCount;
        draw->instanceCount = instanceCount;
        draw->firstIndex = firstIndex;
        draw->firstInstance = firstInstance;
    }

    void CommandBufferBuilder::EndComputePass() {
        mAllocator.Allocate<EndComputePassCmd>(Command::EndComputePass);
    }

    void CommandBufferBuilder::EndRenderPass() {
        mAllocator.Allocate<EndRenderPassCmd>(Command::EndRenderPass);
    }

    void CommandBufferBuilder::SetComputePipeline(ComputePipelineBase* pipeline) {
        SetComputePipelineCmd* cmd =
            mAllocator.Allocate<SetComputePipelineCmd>(Command::SetComputePipeline);
        new (cmd) SetComputePipelineCmd;
        cmd->pipeline = pipeline;
    }

    void CommandBufferBuilder::SetRenderPipeline(RenderPipelineBase* pipeline) {
        SetRenderPipelineCmd* cmd =
            mAllocator.Allocate<SetRenderPipelineCmd>(Command::SetRenderPipeline);
        new (cmd) SetRenderPipelineCmd;
        cmd->pipeline = pipeline;
    }

    void CommandBufferBuilder::SetPushConstants(dawn::ShaderStageBit stages,
                                                uint32_t offset,
                                                uint32_t count,
                                                const void* data) {
        // TODO(cwallez@chromium.org): check for overflows
        if (offset + count > kMaxPushConstants) {
            HandleError("Setting too many push constants");
            return;
        }

        SetPushConstantsCmd* cmd =
            mAllocator.Allocate<SetPushConstantsCmd>(Command::SetPushConstants);
        new (cmd) SetPushConstantsCmd;
        cmd->stages = stages;
        cmd->offset = offset;
        cmd->count = count;

        uint32_t* values = mAllocator.AllocateData<uint32_t>(count);
        memcpy(values, data, count * sizeof(uint32_t));
    }

    void CommandBufferBuilder::SetStencilReference(uint32_t reference) {
        SetStencilReferenceCmd* cmd =
            mAllocator.Allocate<SetStencilReferenceCmd>(Command::SetStencilReference);
        new (cmd) SetStencilReferenceCmd;
        cmd->reference = reference;
    }

    void CommandBufferBuilder::SetBlendColor(float r, float g, float b, float a) {
        SetBlendColorCmd* cmd = mAllocator.Allocate<SetBlendColorCmd>(Command::SetBlendColor);
        new (cmd) SetBlendColorCmd;
        cmd->r = r;
        cmd->g = g;
        cmd->b = b;
        cmd->a = a;
    }

    void CommandBufferBuilder::SetScissorRect(uint32_t x,
                                              uint32_t y,
                                              uint32_t width,
                                              uint32_t height) {
        SetScissorRectCmd* cmd = mAllocator.Allocate<SetScissorRectCmd>(Command::SetScissorRect);
        new (cmd) SetScissorRectCmd;
        cmd->x = x;
        cmd->y = y;
        cmd->width = width;
        cmd->height = height;
    }

    void CommandBufferBuilder::SetBindGroup(uint32_t groupIndex, BindGroupBase* group) {
        if (groupIndex >= kMaxBindGroups) {
            HandleError("Setting bind group over the max");
            return;
        }

        SetBindGroupCmd* cmd = mAllocator.Allocate<SetBindGroupCmd>(Command::SetBindGroup);
        new (cmd) SetBindGroupCmd;
        cmd->index = groupIndex;
        cmd->group = group;
    }

    void CommandBufferBuilder::SetIndexBuffer(BufferBase* buffer, uint32_t offset) {
        // TODO(kainino@chromium.org): validation

        SetIndexBufferCmd* cmd = mAllocator.Allocate<SetIndexBufferCmd>(Command::SetIndexBuffer);
        new (cmd) SetIndexBufferCmd;
        cmd->buffer = buffer;
        cmd->offset = offset;
    }

    void CommandBufferBuilder::SetVertexBuffers(uint32_t startSlot,
                                                uint32_t count,
                                                BufferBase* const* buffers,
                                                uint32_t const* offsets) {
        // TODO(kainino@chromium.org): validation

        SetVertexBuffersCmd* cmd =
            mAllocator.Allocate<SetVertexBuffersCmd>(Command::SetVertexBuffers);
        new (cmd) SetVertexBuffersCmd;
        cmd->startSlot = startSlot;
        cmd->count = count;

        Ref<BufferBase>* cmdBuffers = mAllocator.AllocateData<Ref<BufferBase>>(count);
        for (size_t i = 0; i < count; ++i) {
            new (&cmdBuffers[i]) Ref<BufferBase>(buffers[i]);
        }

        uint32_t* cmdOffsets = mAllocator.AllocateData<uint32_t>(count);
        memcpy(cmdOffsets, offsets, count * sizeof(uint32_t));
    }

}  // namespace dawn_native
