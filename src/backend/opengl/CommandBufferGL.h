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

#ifndef BACKEND_OPENGL_COMMANDBUFFERGL_H_
#define BACKEND_OPENGL_COMMANDBUFFERGL_H_

#include "backend/CommandAllocator.h"
#include "backend/CommandBuffer.h"

namespace backend {
    class RenderPassDescriptorBase;
}  // namespace backend

namespace backend { namespace opengl {

    class Device;

    class CommandBuffer : public CommandBufferBase {
      public:
        CommandBuffer(CommandBufferBuilder* builder);
        ~CommandBuffer();

        void Execute();

      private:
        void ExecuteComputePass();
        void ExecuteRenderPass(RenderPassDescriptorBase* renderPass);

        CommandIterator mCommands;
    };

}}  // namespace backend::opengl

#endif  // BACKEND_OPENGL_COMMANDBUFFERGL_H_
