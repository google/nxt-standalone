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

#include "backend/RenderPass.h"

#include "backend/Buffer.h"
#include "backend/Device.h"
#include "backend/Texture.h"
#include "common/Assert.h"
#include "common/BitSetIterator.h"

namespace backend {

    // RenderPass

    RenderPassBase::RenderPassBase(RenderPassBuilder* builder)
        : attachments(std::move(builder->attachments)), subpasses(std::move(builder->subpasses)) {
        for (uint32_t s = 0; s < GetSubpassCount(); ++s) {
            const auto& subpass = GetSubpassInfo(s);
            for (auto location : IterateBitSet(subpass.colorAttachmentsSet)) {
                auto attachmentSlot = subpass.colorAttachments[location];
                auto& firstSubpass = attachments[attachmentSlot].firstSubpass;
                if (firstSubpass == UINT32_MAX) {
                    firstSubpass = s;
                }
            }
            if (subpass.depthStencilAttachmentSet) {
                auto attachmentSlot = subpass.depthStencilAttachment;
                auto& firstSubpass = attachments[attachmentSlot].firstSubpass;
                if (firstSubpass == UINT32_MAX) {
                    firstSubpass = s;
                }
            }
        }
    }

    uint32_t RenderPassBase::GetAttachmentCount() const {
        return static_cast<uint32_t>(attachments.size());
    }

    const RenderPassBase::AttachmentInfo& RenderPassBase::GetAttachmentInfo(uint32_t attachment) const {
        ASSERT(attachment < attachments.size());
        return attachments[attachment];
    }

    uint32_t RenderPassBase::GetSubpassCount() const {
        return static_cast<uint32_t>(subpasses.size());
    }

    const RenderPassBase::SubpassInfo& RenderPassBase::GetSubpassInfo(uint32_t subpass) const {
        ASSERT(subpass < subpasses.size());
        return subpasses[subpass];
    }

    bool RenderPassBase::IsCompatibleWith(const RenderPassBase* other) const {
        // TODO(kainino@chromium.org): This check is overly strict; need actual
        // compatibility checking (different load and store ops, etc.)
        return other == this;
    }

    // RenderPassBuilder

    enum RenderPassSetProperties {
        RENDERPASS_PROPERTY_ATTACHMENT_COUNT = 0x1,
        RENDERPASS_PROPERTY_SUBPASS_COUNT = 0x2,
    };

    RenderPassBuilder::RenderPassBuilder(DeviceBase* device)
        : Builder(device), subpasses(1) {
    }

    RenderPassBase* RenderPassBuilder::GetResultImpl() {
        constexpr int requiredProperties = RENDERPASS_PROPERTY_ATTACHMENT_COUNT | RENDERPASS_PROPERTY_SUBPASS_COUNT;
        if ((propertiesSet & requiredProperties) != requiredProperties) {
            HandleError("Render pass missing properties");
            return nullptr;
        }

        for (const auto& prop : attachmentProperties) {
            if (!prop.all()) {
                HandleError("A render pass attachment is missing some property");
                return nullptr;
            }
        }

        for (const auto& subpass : subpasses) {
            for (unsigned int location : IterateBitSet(subpass.colorAttachmentsSet)) {
                uint32_t slot = subpass.colorAttachments[location];
                if (TextureFormatHasDepthOrStencil(attachments[slot].format)) {
                    HandleError("Render pass color attachment is not of a color format");
                    return nullptr;
                }
            }
            if (subpass.depthStencilAttachmentSet) {
                uint32_t slot = subpass.depthStencilAttachment;
                if (!TextureFormatHasDepthOrStencil(attachments[slot].format)) {
                    HandleError("Render pass depth/stencil attachment is not of a depth/stencil format");
                    return nullptr;
                }
            }
        }

        return device->CreateRenderPass(this);
    }

    void RenderPassBuilder::SetAttachmentCount(uint32_t attachmentCount) {
        if ((propertiesSet & RENDERPASS_PROPERTY_ATTACHMENT_COUNT) != 0) {
            HandleError("Render pass attachment count property set multiple times");
            return;
        }

        attachmentProperties.resize(attachmentCount);
        attachments.resize(attachmentCount);
        propertiesSet |= RENDERPASS_PROPERTY_ATTACHMENT_COUNT;
    }

    void RenderPassBuilder::AttachmentSetFormat(uint32_t attachmentSlot, nxt::TextureFormat format) {
        if ((propertiesSet & RENDERPASS_PROPERTY_ATTACHMENT_COUNT) == 0) {
            HandleError("Render pass attachment count not set yet");
            return;
        }
        if (attachmentSlot >= attachments.size()) {
            HandleError("Render pass attachment slot out of bounds");
            return;
        }
        if (attachmentProperties[attachmentSlot][ATTACHMENT_PROPERTY_FORMAT]) {
            HandleError("Render pass attachment format already set");
            return;
        }

        attachments[attachmentSlot].format = format;
        attachmentProperties[attachmentSlot].set(ATTACHMENT_PROPERTY_FORMAT);
    }

    void RenderPassBuilder::AttachmentSetColorLoadOp(uint32_t attachmentSlot, nxt::LoadOp op) {
        if ((propertiesSet & RENDERPASS_PROPERTY_ATTACHMENT_COUNT) == 0) {
            HandleError("Render pass attachment count not set yet");
            return;
        }
        if (attachmentSlot >= attachments.size()) {
            HandleError("Render pass attachment slot out of bounds");
            return;
        }

        attachments[attachmentSlot].colorLoadOp = op;
    }

    void RenderPassBuilder::AttachmentSetDepthStencilLoadOps(uint32_t attachmentSlot, nxt::LoadOp depthOp, nxt::LoadOp stencilOp) {
        if ((propertiesSet & RENDERPASS_PROPERTY_ATTACHMENT_COUNT) == 0) {
            HandleError("Render pass attachment count not set yet");
            return;
        }
        if (attachmentSlot >= attachments.size()) {
            HandleError("Render pass attachment slot out of bounds");
            return;
        }

        attachments[attachmentSlot].depthLoadOp = depthOp;
        attachments[attachmentSlot].stencilLoadOp = stencilOp;
    }


    void RenderPassBuilder::SetSubpassCount(uint32_t subpassCount) {
        if ((propertiesSet & RENDERPASS_PROPERTY_SUBPASS_COUNT) != 0) {
            HandleError("Render pass subpass count property set multiple times");
            return;
        }
        if (subpassCount < 1) {
            HandleError("Render pass cannot have fewer than one subpass");
            return;
        }

        subpasses.resize(subpassCount);
        propertiesSet |= RENDERPASS_PROPERTY_SUBPASS_COUNT;
    }

    void RenderPassBuilder::SubpassSetColorAttachment(uint32_t subpass, uint32_t outputAttachmentLocation, uint32_t attachmentSlot) {
        if ((propertiesSet & RENDERPASS_PROPERTY_SUBPASS_COUNT) == 0) {
            HandleError("Render pass subpass count not set yet");
            return;
        }
        if ((propertiesSet & RENDERPASS_PROPERTY_ATTACHMENT_COUNT) == 0) {
            HandleError("Render pass attachment count not set yet");
            return;
        }
        if (subpass >= subpasses.size()) {
            HandleError("Subpass index out of bounds");
            return;
        }
        if (outputAttachmentLocation >= kMaxColorAttachments) {
            HandleError("Subpass output attachment location out of bounds");
            return;
        }
        if (attachmentSlot >= attachments.size()) {
            HandleError("Subpass attachment slot out of bounds");
            return;
        }
        if (subpasses[subpass].colorAttachmentsSet[outputAttachmentLocation]) {
            HandleError("Subpass color attachment already set");
            return;
        }

        subpasses[subpass].colorAttachmentsSet.set(outputAttachmentLocation);
        subpasses[subpass].colorAttachments[outputAttachmentLocation] = attachmentSlot;
    }

    void RenderPassBuilder::SubpassSetDepthStencilAttachment(uint32_t subpass, uint32_t attachmentSlot) {
        if ((propertiesSet & RENDERPASS_PROPERTY_SUBPASS_COUNT) == 0) {
            HandleError("Render pass subpass count not set yet");
            return;
        }
        if ((propertiesSet & RENDERPASS_PROPERTY_ATTACHMENT_COUNT) == 0) {
            HandleError("Render pass attachment count not set yet");
            return;
        }
        if (subpass >= subpasses.size()) {
            HandleError("Subpass index out of bounds");
            return;
        }
        if (attachmentSlot >= attachments.size()) {
            HandleError("Subpass attachment slot out of bounds");
            return;
        }
        if (subpasses[subpass].depthStencilAttachmentSet) {
            HandleError("Subpass depth-stencil attachment already set");
            return;
        }

        subpasses[subpass].depthStencilAttachmentSet = true;
        subpasses[subpass].depthStencilAttachment = attachmentSlot;
    }

}
