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

#include "utils/BackendBinding.h"

#include "common/Assert.h"
#include "common/Platform.h"
#include "nxt/nxt_wsi.h"
#include "utils/SwapChainImpl.h"

#include <cstdio>
#include "glad/glad.h"
#include "GLFW/glfw3.h"

namespace backend {
    namespace opengl {
        void Init(void* (*getProc)(const char*), nxtProcTable* procs, nxtDevice* device);
    }
}

namespace utils {
    class SwapChainImplGL : SwapChainImpl {
        public:
            static nxtSwapChainImplementation Create(GLFWwindow* window) {
                auto impl = GenerateSwapChainImplementation<SwapChainImplGL, nxtWSIContextGL>();
                impl.userData = new SwapChainImplGL(window);
                return impl;
            }

        private:
            GLFWwindow* window = nullptr;
            uint32_t cfgWidth = 0;
            uint32_t cfgHeight = 0;
            GLuint backFBO = 0;
            GLuint backTexture = 0;

            SwapChainImplGL(GLFWwindow* window)
                : window(window) {
            }

            ~SwapChainImplGL() {
                glDeleteTextures(1, &backTexture);
                glDeleteFramebuffers(1, &backFBO);
            }

            // For GenerateSwapChainImplementation
            friend class SwapChainImpl;

            void Init(nxtWSIContextGL*) {
                glGenTextures(1, &backTexture);
                glBindTexture(GL_TEXTURE_2D, backTexture);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 0, 0, 0,
                        GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

                glGenFramebuffers(1, &backFBO);
                glBindFramebuffer(GL_READ_FRAMEBUFFER, backFBO);
                glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                        GL_TEXTURE_2D, backTexture, 0);
            }

            nxtSwapChainError Configure(nxtTextureFormat format, nxtTextureUsageBit, nxtTextureUsageBit,
                    uint32_t width, uint32_t height) {
                if (format != NXT_TEXTURE_FORMAT_R8_G8_B8_A8_UNORM) {
                    return "unsupported format";
                }
                ASSERT(width > 0);
                ASSERT(height > 0);
                cfgWidth = width;
                cfgHeight = height;

                glBindTexture(GL_TEXTURE_2D, backTexture);
                // Reallocate the texture
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                        GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

                return NXT_SWAP_CHAIN_NO_ERROR;
            }

            nxtSwapChainError GetNextTexture(nxtSwapChainNextTexture* nextTexture) {
                nextTexture->texture = reinterpret_cast<void*>(static_cast<size_t>(backTexture));
                return NXT_SWAP_CHAIN_NO_ERROR;
            }

            nxtSwapChainError Present() {
                glBindFramebuffer(GL_READ_FRAMEBUFFER, backFBO);
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
                glBlitFramebuffer(0, 0, cfgWidth, cfgHeight, 0, 0, cfgWidth, cfgHeight,
                        GL_COLOR_BUFFER_BIT, GL_NEAREST);
                glfwSwapBuffers(window);

                return NXT_SWAP_CHAIN_NO_ERROR;
            }
    };

    class OpenGLBinding : public BackendBinding {
        public:
            void SetupGLFWWindowHints() override {
                #if defined(NXT_PLATFORM_APPLE)
                    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
                    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
                    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
                    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
                #else
                    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
                    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 4);
                    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
                    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
                #endif
            }
            void GetProcAndDevice(nxtProcTable* procs, nxtDevice* device) override {
                glfwMakeContextCurrent(window);
                backend::opengl::Init(reinterpret_cast<void*(*)(const char*)>(glfwGetProcAddress), procs, device);

                backendDevice = *device;
            }

            uint64_t GetSwapChainImplementation() override {
                if (swapchainImpl.userData == nullptr) {
                    swapchainImpl = SwapChainImplGL::Create(window);
                }
                return reinterpret_cast<uint64_t>(&swapchainImpl);
            }

        private:
            nxtDevice backendDevice = nullptr;
            nxtSwapChainImplementation swapchainImpl = {};
    };

    BackendBinding* CreateOpenGLBinding() {
        return new OpenGLBinding;
    }

}
