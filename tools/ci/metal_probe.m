// A sonda do Metal no runner de macOS da CI (.github/workflows/portable.yml; docs/port.md, "macOS: compilação e CI").
// Diz se o runner tem um dispositivo Metal (MTLCreateSystemDefaultDevice) e se ele de fato trabalha: compila um MSL em
// tempo de execução, roda um kernel de compute e desenha um triângulo numa textura fora da tela, lida de volta. A etapa
// 3e quer saber se os testes de conformidade do renderizador (as capturas comparadas pixel a pixel) podem rodar ali.
// Sempre sai com 0: é uma sonda, não um teste.
//
// xcrun clang -fobjc-arc -framework Foundation -framework Metal tools/ci/metal_probe.m -o metal_probe && ./metal_probe

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <stdio.h>

static NSString* const kSource =
    @"#include <metal_stdlib>\n"
     "using namespace metal;\n"
     "kernel void add_one(device float* v [[buffer(0)]], uint i [[thread_position_in_grid]]) { v[i] = v[i] + 1.0f; }\n"
     "struct VOut { float4 pos [[position]]; };\n"
     "vertex VOut vs_main(uint vid [[vertex_id]]) {\n"
     "    const float2 p[3] = { float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0) };\n"
     "    VOut o;\n"
     "    o.pos = float4(p[vid], 0.0, 1.0);\n"
     "    return o;\n"
     "}\n"
     "fragment float4 fs_main() { return float4(0.25, 0.5, 0.75, 1.0); }\n";

int main(void) {
    @autoreleasepool {
        NSArray<id<MTLDevice>>* all = MTLCopyAllDevices();
        printf("MTLCopyAllDevices: %lu dispositivo(s)\n", (unsigned long)all.count);
        for (id<MTLDevice> d in all) printf("  - %s (headless %d)\n", d.name.UTF8String, (int)d.isHeadless);
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        if (dev == nil) {
            // Sem um dispositivo padrão (um processo sem a sessão gráfica), o da lista: é o que um teste sem janela usaria.
            printf("MTLCreateSystemDefaultDevice: nil; usando o primeiro de MTLCopyAllDevices\n");
            if (all.count == 0) {
                printf("sem Metal neste runner\n");
                return 0;
            }
            dev = all[0];
        }
        printf("dispositivo: %s\n", dev.name.UTF8String);
        printf("memoria unificada %d, headless %d, baixo consumo %d, removivel %d, recommendedMaxWorkingSetSize %llu MB\n",
               (int)dev.hasUnifiedMemory, (int)dev.isHeadless, (int)dev.isLowPower, (int)dev.isRemovable,
               (unsigned long long)(dev.recommendedMaxWorkingSetSize >> 20));
        printf("familias: Apple7 %d, Apple8 %d, Mac2 %d, Metal3 %d; MSAA 4x %d, contadores de tempo %d\n",
               (int)[dev supportsFamily:MTLGPUFamilyApple7], (int)[dev supportsFamily:MTLGPUFamilyApple8],
               (int)[dev supportsFamily:MTLGPUFamilyMac2], (int)[dev supportsFamily:MTLGPUFamilyMetal3],
               (int)[dev supportsTextureSampleCount:4],
               (int)[dev supportsCounterSampling:MTLCounterSamplingPointAtStageBoundary]);

        NSError* err = nil;
        id<MTLLibrary> lib = [dev newLibraryWithSource:kSource options:nil error:&err];
        if (lib == nil) {
            printf("MSL em tempo de execucao: falhou (%s)\n", err.localizedDescription.UTF8String);
            return 0;
        }
        printf("MSL em tempo de execucao: ok\n");
        id<MTLCommandQueue> queue = [dev newCommandQueue];

        // Compute: 64 floats + 1.
        id<MTLComputePipelineState> cps = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"add_one"]
                                                                             error:&err];
        float data[64];
        for (int i = 0; i < 64; ++i) data[i] = (float)i;
        id<MTLBuffer> buf = [dev newBufferWithBytes:data length:sizeof(data) options:MTLResourceStorageModeShared];
        id<MTLCommandBuffer> cb = [queue commandBuffer];
        id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
        [ce setComputePipelineState:cps];
        [ce setBuffer:buf offset:0 atIndex:0];
        [ce dispatchThreadgroups:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
        [ce endEncoding];
        [cb commit];
        [cb waitUntilCompleted];
        const float* out = (const float*)buf.contents;
        int ok = cb.status == MTLCommandBufferStatusCompleted;
        for (int i = 0; i < 64 && ok; ++i) ok = out[i] == (float)i + 1.0f;
        printf("compute: %s\n", ok ? "ok" : "ERRADO");

        // Um triângulo que cobre a textura (16x16 RGBA8), lido de volta por um blit.
        MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                       width:16
                                                                                      height:16
                                                                                   mipmapped:NO];
        td.usage = MTLTextureUsageRenderTarget;
        td.storageMode = MTLStorageModePrivate;
        id<MTLTexture> tex = [dev newTextureWithDescriptor:td];
        MTLRenderPipelineDescriptor* rpd = [MTLRenderPipelineDescriptor new];
        rpd.vertexFunction = [lib newFunctionWithName:@"vs_main"];
        rpd.fragmentFunction = [lib newFunctionWithName:@"fs_main"];
        rpd.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
        id<MTLRenderPipelineState> rps = [dev newRenderPipelineStateWithDescriptor:rpd error:&err];
        if (rps == nil) {
            printf("desenho: pipeline falhou (%s)\n", err.localizedDescription.UTF8String);
            return 0;
        }
        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = tex;
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        id<MTLBuffer> pixels = [dev newBufferWithLength:16 * 16 * 4 options:MTLResourceStorageModeShared];
        cb = [queue commandBuffer];
        id<MTLRenderCommandEncoder> re = [cb renderCommandEncoderWithDescriptor:pass];
        [re setRenderPipelineState:rps];
        [re drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [re endEncoding];
        id<MTLBlitCommandEncoder> be = [cb blitCommandEncoder];
        [be copyFromTexture:tex
                     sourceSlice:0
                     sourceLevel:0
                    sourceOrigin:MTLOriginMake(0, 0, 0)
                      sourceSize:MTLSizeMake(16, 16, 1)
                        toBuffer:pixels
               destinationOffset:0
          destinationBytesPerRow:16 * 4
        destinationBytesPerImage:16 * 16 * 4];
        [be endEncoding];
        [cb commit];
        [cb waitUntilCompleted];
        const uint8_t* px = (const uint8_t*)pixels.contents + (8 * 16 + 8) * 4;
        const int drew = cb.status == MTLCommandBufferStatusCompleted && px[0] >= 62 && px[0] <= 66 && px[1] >= 126 &&
                         px[1] <= 130 && px[2] >= 189 && px[2] <= 193 && px[3] == 255;
        printf("desenho fora da tela: %s (pixel %u %u %u %u), GPU %.3f ms\n", drew ? "ok" : "ERRADO", px[0], px[1], px[2],
               px[3], (cb.GPUEndTime - cb.GPUStartTime) * 1000.0);
    }
    return 0;
}
