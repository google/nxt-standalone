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

#include "SampleUtils.h"

#include "utils/NXTHelpers.h"
#include "utils/SystemUtils.h"

#include <cstdlib>
#include <cstdio>
#include <vector>

nxt::Device device;
nxt::Queue queue;
nxt::SwapChain swapchain;
nxt::TextureView depthStencilView;
nxt::RenderPipeline pipeline;
nxt::RenderPass renderpass;

float RandomFloat(float min, float max) {
    float zeroOne = rand() / float(RAND_MAX);
    return zeroOne * (max - min) + min;
}

struct ShaderData {
    float scale;
    float time;
    float offsetX;
    float offsetY;
    float scalar;
    float scalarOffset;
};

static std::vector<ShaderData> shaderData;

void init() {
    device = CreateCppNXTDevice();

    queue = device.CreateQueueBuilder().GetResult();
    swapchain = GetSwapChain(device);
    swapchain.Configure(nxt::TextureFormat::R8G8B8A8Unorm, nxt::TextureUsageBit::OutputAttachment, nxt::TextureUsageBit::OutputAttachment, 640, 480);

    nxt::ShaderModule vsModule = utils::CreateShaderModule(device, nxt::ShaderStage::Vertex, R"(
        #version 450

        layout(push_constant) uniform ConstantsBlock {
            float scale;
            float time;
            float offsetX;
            float offsetY;
            float scalar;
            float scalarOffset;
        } c;

        layout(location = 0) out vec4 v_color;

        const vec4 positions[3] = vec4[3](
            vec4( 0.0f,  0.1f, 0.0f, 1.0f),
            vec4(-0.1f, -0.1f, 0.0f, 1.0f),
            vec4( 0.1f, -0.1f, 0.0f, 1.0f)
        );

        const vec4 colors[3] = vec4[3](
            vec4(1.0f, 0.0f, 0.0f, 1.0f),
            vec4(0.0f, 1.0f, 0.0f, 1.0f),
            vec4(0.0f, 0.0f, 1.0f, 1.0f)
        );

        void main() {
            vec4 position = positions[gl_VertexIndex];
            vec4 color = colors[gl_VertexIndex];

            float fade = mod(c.scalarOffset + c.time * c.scalar / 10.0, 1.0);
            if (fade < 0.5) {
                fade = fade * 2.0;
            } else {
                fade = (1.0 - fade) * 2.0;
            }
            float xpos = position.x * c.scale;
            float ypos = position.y * c.scale;
            float angle = 3.14159 * 2.0 * fade;
            float xrot = xpos * cos(angle) - ypos * sin(angle);
            float yrot = xpos * sin(angle) + ypos * cos(angle);
            xpos = xrot + c.offsetX;
            ypos = yrot + c.offsetY;
            v_color = vec4(fade, 1.0 - fade, 0.0, 1.0) + color;
            gl_Position = vec4(xpos, ypos, 0.0, 1.0);
        })"
    );

    nxt::ShaderModule fsModule = utils::CreateShaderModule(device, nxt::ShaderStage::Fragment, R"(
        #version 450
        out vec4 fragColor;
        layout(location = 0) in vec4 v_color;
        void main() {
            fragColor = v_color;
        })"
    );

    renderpass = CreateDefaultRenderPass(device);
    depthStencilView = CreateDefaultDepthStencilView(device);

    pipeline = device.CreateRenderPipelineBuilder()
        .SetSubpass(renderpass, 0)
        .SetStage(nxt::ShaderStage::Vertex, vsModule, "main")
        .SetStage(nxt::ShaderStage::Fragment, fsModule, "main")
        .GetResult();

    shaderData.resize(10000);
    for (auto& data : shaderData) {
        data.scale = RandomFloat(0.2f, 0.4f);
        data.time = 0.0;
        data.offsetX = RandomFloat(-0.9f, 0.9f);
        data.offsetY = RandomFloat(-0.9f, 0.9f);
        data.scalar = RandomFloat(0.5f, 2.0f);
        data.scalarOffset = RandomFloat(0.0f, 10.0f);
    }
}

void frame() {
    nxt::Texture backbuffer;
    nxt::Framebuffer framebuffer;
    GetNextFramebuffer(device, renderpass, swapchain, depthStencilView, &backbuffer, &framebuffer);

    static int f = 0;
    f++;

    size_t i = 0;

    nxt::CommandBuffer commands;
    {
        nxt::CommandBufferBuilder builder = device.CreateCommandBufferBuilder()
            .BeginRenderPass(renderpass, framebuffer)
            .BeginRenderSubpass()
            .SetRenderPipeline(pipeline)
            .Clone();

        for (int k = 0; k < 10000; k++) {
            shaderData[i].time = f / 60.0f;
            builder.SetPushConstants(nxt::ShaderStageBit::Vertex, 0, 6, reinterpret_cast<uint32_t*>(&shaderData[i]))
                   .DrawArrays(3, 1, 0, 0);
            i++;
        }

        builder.EndRenderSubpass();
        builder.EndRenderPass();
        commands = builder.GetResult();
    }

    queue.Submit(1, &commands);
    backbuffer.TransitionUsage(nxt::TextureUsageBit::Present);
    swapchain.Present(backbuffer);
    DoFlush();
    fprintf(stderr, "frame %i\n", f);
}

int main(int argc, const char* argv[]) {
    if (!InitSample(argc, argv)) {
        return 1;
    }
    init();

    while (!ShouldQuit()) {
        frame();
        utils::USleep(16000);
    }

    // TODO release stuff
}
