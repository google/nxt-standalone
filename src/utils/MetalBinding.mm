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

#define GLFW_EXPOSE_NATIVE_COCOA
#include "GLFW/glfw3.h"
#include "GLFW/glfw3native.h"

#import <QuartzCore/CAMetalLayer.h>
#import <Metal/Metal.h>

namespace backend {
namespace metal {
    void Init(id<MTLDevice> metalDevice, nxtProcTable* procs, nxtDevice* device);
    void SetNextDrawable(nxtDevice device, id<CAMetalDrawable> drawable);
    void Present(nxtDevice device);
}
}

namespace utils {

    // TODO(kainino@chromium.org): probably make this reference counted
    class SwapChainImplMTL {
        public:
            static nxtSwapChainImplementation Create(GLFWwindow* window) {
                nxtSwapChainImplementation impl = {};
                impl.Init = Init;
                impl.Destroy = Destroy;
                impl.Configure = Configure;
                impl.GetNextTexture = GetNextTexture;
                impl.Present = Present;
                impl.userData = new SwapChainImplMTL(window);
                return impl;
            }

        private:
            id nsWindow = nil;
            id<MTLDevice> mtlDevice = nil;
            id<MTLCommandQueue> commandQueue = nil;

            CAMetalLayer* layer = nullptr;
            id<CAMetalDrawable> currentDrawable = nil;
            id<MTLTexture> currentTexture = nil;

            SwapChainImplMTL(GLFWwindow* window)
                : nsWindow(glfwGetCocoaWindow(window)) {
            }

            ~SwapChainImplMTL() {
                [currentTexture release];
                [currentDrawable release];
            }

            void Init(nxtWSIContextMetal* ctx) {
                mtlDevice = ctx->device;
                commandQueue = [mtlDevice newCommandQueue];
            }

            nxtSwapChainError Configure(nxtTextureFormat format,
                    uint32_t width, uint32_t height) {
                if (format != NXT_TEXTURE_FORMAT_R8_G8_B8_A8_UNORM) {
                    return "unsupported format";
                }

                NSView* contentView = [nsWindow contentView];
                [contentView setWantsLayer: YES];

                CGSize size = {};
                size.width = width;
                size.height = height;

                layer = [CAMetalLayer layer];
                [layer setDevice: mtlDevice];
                [layer setPixelFormat: MTLPixelFormatBGRA8Unorm];
                [layer setFramebufferOnly: YES];
                [layer setDrawableSize: size];

                [contentView setLayer: layer];

                return NXT_SWAP_CHAIN_NO_ERROR;
            }

            nxtSwapChainError GetNextTexture(nxtSwapChainNextTexture* nextTexture) {
                [currentDrawable release];
                currentDrawable = [layer nextDrawable];
                [currentDrawable retain];

                [currentTexture release];
                currentTexture = currentDrawable.texture;
                [currentTexture retain];

                // Clear initial contents of the texture
                {
                    MTLRenderPassDescriptor* passDescriptor = [MTLRenderPassDescriptor renderPassDescriptor];
                    passDescriptor.colorAttachments[0].texture = currentTexture;
                    passDescriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
                    passDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
                    passDescriptor.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);

                    id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];
                    id<MTLRenderCommandEncoder> commandEncoder = [commandBuffer
                        renderCommandEncoderWithDescriptor:passDescriptor];
                    [commandEncoder endEncoding];
                    [commandBuffer commit];
                }

                nextTexture->texture = reinterpret_cast<void*>(currentTexture);

                return NXT_SWAP_CHAIN_NO_ERROR;
            }

            nxtSwapChainError Present() {
                id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];
                [commandBuffer presentDrawable: currentDrawable];
                [commandBuffer commit];

                return NXT_SWAP_CHAIN_NO_ERROR;
            }

            // C stubs for C++ methods

            static void Init(void* userData, void* wsiContext) {
                auto* ctx = reinterpret_cast<nxtWSIContextMetal*>(wsiContext);
                reinterpret_cast<SwapChainImplMTL*>(userData)->Init(ctx);
            }

            static void Destroy(void* userData) {
                delete reinterpret_cast<SwapChainImplMTL*>(userData);
            }

            static nxtSwapChainError Configure(void* userData, nxtTextureFormat format, uint32_t width, uint32_t height) {
                return reinterpret_cast<SwapChainImplMTL*>(userData)->Configure(
                        format, width, height);
            }

            static nxtSwapChainError GetNextTexture(void* userData, nxtSwapChainNextTexture* nextTexture) {
                return reinterpret_cast<SwapChainImplMTL*>(userData)->GetNextTexture(
                        nextTexture);
            }

            static nxtSwapChainError Present(void* userData) {
                return reinterpret_cast<SwapChainImplMTL*>(userData)->Present();
            }
    };

    class MetalBinding : public BackendBinding {
        public:
            void SetupGLFWWindowHints() override {
                glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            }
            void GetProcAndDevice(nxtProcTable* procs, nxtDevice* device) override {
                metalDevice = MTLCreateSystemDefaultDevice();

                backend::metal::Init(metalDevice, procs, device);
                backendDevice = *device;
            }

            nxtSwapChainImplementation GetSwapChainImplementation() override {
                return SwapChainImplMTL::Create(window);
            }

        private:
            id<MTLDevice> metalDevice = nil;
            nxtDevice backendDevice = nullptr;
    };

    BackendBinding* CreateMetalBinding() {
        return new MetalBinding;
    }

}
