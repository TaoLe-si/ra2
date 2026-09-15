// Dx12Renderer.cpp
//
// 最精简可用的一套 DX12：设备 / 交换链 / 根签名 / 一个 PSO / 精灵上传。
// 着色器源码内联在下面，用 D3DCompile 运行时编译（省掉一条 fxc 构建步骤）。

#include "gfx/dx12/Dx12Renderer.h"

#include <cstdio>
#include <cstring>

#include <d3dcompiler.h>

namespace ra2 {
namespace {

// ---------------------------------------------------------------------------
// 着色器
// ---------------------------------------------------------------------------
// 顶点：用 SV_VertexID 生成四边形，不需要顶点缓冲。
// 像素：索引纹理取索引 -> 调色板纹理查色。调色板只有 256x1，
//       换阵营色/选中高亮只要重传这张小纹理。
constexpr const char* kShaderSource = R"(
cbuffer Xform : register(b0) {
    float4 xform;   // xy = 左上角 NDC, zw = 尺寸 NDC
    float4 tint;    // 纯色图元的 RGBA；精灵路径恒为 (1,1,1,1)
};

struct VSOut {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

VSOut VSMain(uint vid : SV_VertexID) {
    float2 c = float2(
        (vid == 1 || vid == 4 || vid == 5) ? 1.0 : 0.0,
        (vid == 2 || vid == 3 || vid == 5) ? 1.0 : 0.0);
    VSOut o;
    o.pos = float4(xform.x + c.x * xform.z, xform.y - c.y * xform.w, 0.0, 1.0);
    o.uv  = c;
    return o;
}

Texture2D    indexTex   : register(t0);
Texture2D    paletteTex : register(t1);
SamplerState samp       : register(s0);

float4 PSMain(VSOut i) : SV_Target {
    uint idx = (uint)(indexTex.Sample(samp, i.uv).r * 255.0 + 0.5);
    return paletteTex.Sample(samp, float2((float(idx) + 0.5) / 256.0, 0.5));
}

// 真彩精灵（体素）：明暗已经在 CPU 端烘进 RGB，直接采样。
float4 PSMainRGBA(VSOut i) : SV_Target {
    return indexTex.Sample(samp, i.uv);
}

// 纯色图元：界面用（侧栏底板、按钮框、框选矩形、血条、小地图网格）。
// 不采样任何纹理，直接吐 tint —— 配根常量里的第 5..8 个 float 用。
float4 PSSolid(VSOut i) : SV_Target {
    return tint;
}
)";

void SetError(char* dst, const char* msg) {
    std::snprintf(dst, 256, "%s", msg);
}

// ---- 手写描述符句柄与屏障 ----
// 本机 WinSDK 10.0.26100 里没有 d3dx12.h（那是独立下载的辅助头），
// 所以不依赖 CD3DX12_*，自己算偏移。
D3D12_CPU_DESCRIPTOR_HANDLE Cpu_Handle(ID3D12DescriptorHeap* heap, UINT index, UINT size) {
    D3D12_CPU_DESCRIPTOR_HANDLE h = heap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<SIZE_T>(index) * size;
    return h;
}

D3D12_GPU_DESCRIPTOR_HANDLE Gpu_Handle(ID3D12DescriptorHeap* heap, UINT index, UINT size) {
    D3D12_GPU_DESCRIPTOR_HANDLE h = heap->GetGPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<UINT64>(index) * size;
    return h;
}

D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* res, D3D12_RESOURCE_STATES before,
                                  D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    b.Transition.pResource = res;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = before;
    b.Transition.StateAfter = after;
    return b;
}

void Wait_Queue(ID3D12CommandQueue* q, ID3D12Fence* f, HANDLE ev, UINT64& value) {
    const UINT64 v = ++value;
    q->Signal(f, v);
    if (f->GetCompletedValue() < v) {
        f->SetEventOnCompletion(v, ev);
        WaitForSingleObject(ev, INFINITE);
    }
}

}  // namespace

Dx12Renderer::~Dx12Renderer() { Shutdown(); }

void Dx12Renderer::Fail(const char* msg) {
    SetError(last_error_, msg);
    std::fprintf(stderr, "[DX12] %s\n", msg);
}

// ---------------------------------------------------------------------------
ID3D12Resource* Dx12Renderer::Current_Target() const {
    return headless_ ? rt_texture_.Get() : backbuffers_[frame_index_].Get();
}

bool Dx12Renderer::Init_Offscreen(int width, int height) {
    if (!Create_Device()) {
        return false;
    }
    headless_ = true;
    vp_width_ = width;
    vp_height_ = height;
    return Finish_Init();
}

bool Dx12Renderer::Init(HWND hwnd, int width, int height) {
    if (!Create_Device()) {
        return false;
    }
    if (!Create_SwapChain(hwnd, width, height)) {
        return false;
    }
    return Finish_Init();
}

bool Dx12Renderer::Finish_Init() {
    if (!Create_Descriptor_Heaps()) {
        return false;
    }
    if (!Create_Descriptor_Heaps()) {
        return false;
    }
    if (!Create_Pipeline()) {
        return false;
    }
    // 调色板纹理先建好，之后只更新内容。
    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = 256;
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags = D3D12_RESOURCE_FLAG_NONE;
    if (FAILED(device_->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&palette_tex_)))) {
        Fail("创建调色板纹理失败");
        return false;
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
    sv.Format = rd.Format;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2D.MipLevels = 1;
    device_->CreateShaderResourceView(palette_tex_.Get(), &sv,
                                      Cpu_Handle(srv_heap_.Get(), 0, srv_size_));
    srv_used_ = 1;   // slot 0 归调色板

    if (FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)))) {
        Fail("创建 fence 失败");
        return false;
    }
    fence_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    return true;
}

bool Dx12Renderer::Create_Device() {
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory_)))) {
        Fail("CreateDXGIFactory1 失败");
        return false;
    }
    // 先试硬件，失败退 WARP —— 没有独显的机器上也能看到画面。
    if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_)))) {
        ComPtr<IDXGIAdapter> warp;
        if (FAILED(factory_->EnumWarpAdapter(IID_PPV_ARGS(&warp))) ||
            FAILED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0,
                                     IID_PPV_ARGS(&device_)))) {
            Fail("D3D12CreateDevice 失败（硬件与 WARP 都不可用）");
            return false;
        }
    }
    D3D12_COMMAND_QUEUE_DESC qd = {};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    qd.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    if (FAILED(device_->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue_)))) {
        Fail("创建命令队列失败");
        return false;
    }
    if (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                               IID_PPV_ARGS(&alloc_)))) {
        Fail("创建命令分配器失败");
        return false;
    }
    if (FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc_.Get(),
                                          nullptr, IID_PPV_ARGS(&cmd_)))) {
        Fail("创建命令列表失败");
        return false;
    }
    cmd_->Close();

    // 诊断：设备可能"创建成功但立刻被摘掉"（无 GPU / 沙箱 / 驱动被禁用）。
    // 这种失败要到下一次 CreateCommittedResource 才暴露，报错信息还是
    // DEVICE_REMOVED，很容易误判成"回读缓冲有问题"。
    const HRESULT removed = device_->GetDeviceRemovedReason();
    ComPtr<ID3D12Resource> probe;
    D3D12_HEAP_PROPERTIES php = {};
    php.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC prd = {};
    prd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    prd.Width = 65536;
    prd.Height = 1;
    prd.DepthOrArraySize = 1;
    prd.MipLevels = 1;
    prd.SampleDesc.Count = 1;
    prd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    const HRESULT phr = device_->CreateCommittedResource(
        &php, D3D12_HEAP_FLAG_NONE, &prd, D3D12_RESOURCE_STATE_COMMON, nullptr,
        IID_PPV_ARGS(&probe));
    std::printf("[DX12] 设备自检：removed=0x%08lX 试建 64KB 缓冲=0x%08lX\n",
                static_cast<unsigned long>(removed), static_cast<unsigned long>(phr));
    probe.Reset();
    return true;
}

bool Dx12Renderer::Create_SwapChain(HWND hwnd, int width, int height) {
    vp_width_ = width;
    vp_height_ = height;
    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = static_cast<UINT>(width);
    sd.Height = static_cast<UINT>(height);
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;

    ComPtr<IDXGISwapChain1> sc1;
    if (FAILED(factory_->CreateSwapChainForHwnd(queue_.Get(), hwnd, &sd, nullptr, nullptr,
                                                &sc1))) {
        Fail("CreateSwapChainForHwnd 失败");
        return false;
    }
    sc1.As(&swapchain_);
    frame_index_ = swapchain_->GetCurrentBackBufferIndex();
    return true;
}

bool Dx12Renderer::Create_Descriptor_Heaps() {
    D3D12_DESCRIPTOR_HEAP_DESC rh = {};
    rh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rh.NumDescriptors = 2;
    if (FAILED(device_->CreateDescriptorHeap(&rh, IID_PPV_ARGS(&rtv_heap_)))) {
        Fail("创建 RTV 堆失败");
        return false;
    }
    rtv_size_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    if (headless_) {
        // 离屏：自己建一张渲染目标纹理。
        D3D12_HEAP_PROPERTIES hp = {};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd = {};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width = static_cast<UINT64>(vp_width_);
        rd.Height = static_cast<UINT>(vp_height_);
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        if (FAILED(device_->CreateCommittedResource(
                &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_RENDER_TARGET,
                nullptr, IID_PPV_ARGS(&rt_texture_)))) {
            Fail("创建离屏渲染目标失败");
            return false;
        }
        device_->CreateRenderTargetView(rt_texture_.Get(), nullptr,
                                        Cpu_Handle(rtv_heap_.Get(), 0, rtv_size_));
    } else {
        for (int i = 0; i < 2; ++i) {
            swapchain_->GetBuffer(i, IID_PPV_ARGS(&backbuffers_[i]));
            device_->CreateRenderTargetView(backbuffers_[i].Get(), nullptr,
                                            Cpu_Handle(rtv_heap_.Get(), i, rtv_size_));
        }
    }

    D3D12_DESCRIPTOR_HEAP_DESC sh = {};
    sh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    sh.NumDescriptors = 256;
    sh.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device_->CreateDescriptorHeap(&sh, IID_PPV_ARGS(&srv_heap_)))) {
        Fail("创建 SRV 堆失败");
        return false;
    }
    srv_size_ = device_->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    return true;
}

bool Dx12Renderer::Create_Pipeline() {
    ComPtr<ID3DBlob> vs, ps, err;
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
    if (FAILED(D3DCompile(kShaderSource, std::strlen(kShaderSource), nullptr, nullptr,
                          nullptr, "VSMain", "vs_5_0", flags, 0, &vs, &err))) {
        if (err) {
            Fail(static_cast<const char*>(err->GetBufferPointer()));
        } else {
            Fail("顶点着色器编译失败");
        }
        return false;
    }
    if (FAILED(D3DCompile(kShaderSource, std::strlen(kShaderSource), nullptr, nullptr,
                          nullptr, "PSMain", "ps_5_0", flags, 0, &ps, &err))) {
        if (err) {
            Fail(static_cast<const char*>(err->GetBufferPointer()));
        } else {
            Fail("像素着色器编译失败");
        }
        return false;
    }
    ComPtr<ID3DBlob> ps_rgba;
    if (FAILED(D3DCompile(kShaderSource, std::strlen(kShaderSource), nullptr, nullptr,
                          nullptr, "PSMainRGBA", "ps_5_0", flags, 0, &ps_rgba, &err))) {
        if (err) {
            Fail(static_cast<const char*>(err->GetBufferPointer()));
        } else {
            Fail("真彩像素着色器编译失败");
        }
        return false;
    }
    ComPtr<ID3DBlob> ps_solid;
    if (FAILED(D3DCompile(kShaderSource, std::strlen(kShaderSource), nullptr, nullptr,
                          nullptr, "PSSolid", "ps_5_0", flags, 0, &ps_solid, &err))) {
        if (err) {
            Fail(static_cast<const char*>(err->GetBufferPointer()));
        } else {
            Fail("纯色像素着色器编译失败");
        }
        return false;
    }

    // 根签名：t0=索引纹理, t1=调色板, b0=变换常量, s0=静态点采样器
    D3D12_DESCRIPTOR_RANGE r0 = {};
    r0.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    r0.NumDescriptors = 1;
    r0.BaseShaderRegister = 0;
    D3D12_DESCRIPTOR_RANGE r1 = {};
    r1.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    r1.NumDescriptors = 1;
    r1.BaseShaderRegister = 1;

    D3D12_ROOT_PARAMETER params[3] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[0].DescriptorTable.NumDescriptorRanges = 1;
    params[0].DescriptorTable.pDescriptorRanges = &r0;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &r1;
    // 8 个常量：前 4 个是 xform（顶点用），后 4 个是 tint（纯色图元的像素用）。
    // ShaderVisibility 必须是 ALL —— 顶点着色器和 PSSolid 都要读这个 cbuffer，
    // 写成 VERTEX 的话 PSSolid 里的 tint 读出来全是 0，界面会整片黑（踩过）。
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[2].Constants.Num32BitValues = 8;
    params[2].Constants.ShaderRegister = 0;

    D3D12_STATIC_SAMPLER_DESC ss = {};
    ss.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    ss.AddressU = ss.AddressV = ss.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    ss.ShaderRegister = 0;
    ss.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsd = {};
    rsd.NumParameters = 3;
    rsd.pParameters = params;
    rsd.NumStaticSamplers = 1;
    rsd.pStaticSamplers = &ss;
    rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> sig, sigerr;
    if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig,
                                           &sigerr))) {
        Fail("序列化根签名失败");
        return false;
    }
    if (FAILED(device_->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
                                            IID_PPV_ARGS(&root_sig_)))) {
        Fail("创建根签名失败");
        return false;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
    pd.pRootSignature = root_sig_.Get();
    pd.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    pd.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    pd.BlendState.RenderTarget[0].BlendEnable = TRUE;
    pd.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    pd.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    pd.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    pd.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    pd.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    pd.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.SampleMask = 0xFFFFFFFF;
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1;
    pd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pd.SampleDesc.Count = 1;
    if (FAILED(device_->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pso_)))) {
        Fail("创建 PSO 失败");
        return false;
    }
    // 真彩管线：除了换像素着色器，其它全部复用（根签名/混合/光栅都一致）。
    pd.PS = {ps_rgba->GetBufferPointer(), ps_rgba->GetBufferSize()};
    if (FAILED(device_->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pso_rgba_)))) {
        Fail("创建真彩 PSO 失败");
        return false;
    }
    // 纯色管线：给界面图元用。
    pd.PS = {ps_solid->GetBufferPointer(), ps_solid->GetBufferSize()};
    if (FAILED(device_->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pso_solid_)))) {
        Fail("创建纯色 PSO 失败");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
int Dx12Renderer::Upload_Sprite(const uint8_t* pixels, int width, int height) {
    return Upload_Texture(pixels, width, height, 1, DXGI_FORMAT_R8_UNORM, false);
}

int Dx12Renderer::Upload_Sprite_RGBA(const uint8_t* pixels, int width, int height) {
    return Upload_Texture(pixels, width, height, 4, DXGI_FORMAT_R8G8B8A8_UNORM, true);
}

int Dx12Renderer::Upload_Texture(const uint8_t* pixels, int width, int height,
                                 int bpp, DXGI_FORMAT fmt, bool rgba) {
    if (!device_ || srv_used_ >= 256) {
        return -1;
    }
    // 纹理拷贝的行距必须按 D3D12_TEXTURE_DATA_PITCH_ALIGNMENT(256) 对齐 ——
    // 不 aligned 的 CopyTextureRegion 是 INVALID_CALL，而且 DX12 会直接把设备
    // 摘掉（removed=0x887A0001），报错却出现在下一次资源创建上，极难定位。
    // 宽 632 的索引纹理就踩过这个坑。
    const UINT64 row_bytes = static_cast<UINT64>(width) * bpp;
    const UINT64 row = (row_bytes + 255) & ~static_cast<UINT64>(255);
    const UINT64 total = row * height;

    ComPtr<ID3D12Resource> tex;
    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = static_cast<UINT64>(width);
    rd.Height = static_cast<UINT>(height);
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = fmt;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    if (FAILED(device_->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&tex)))) {
        Fail("创建精灵纹理失败");
        return -1;
    }

    ComPtr<ID3D12Resource> up;
    D3D12_HEAP_PROPERTIES uhp = {};
    uhp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC urd = {};
    urd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    urd.Width = total;
    urd.Height = 1;
    urd.DepthOrArraySize = 1;
    urd.MipLevels = 1;
    urd.SampleDesc.Count = 1;
    urd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(device_->CreateCommittedResource(&uhp, D3D12_HEAP_FLAG_NONE, &urd,
                                                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                IID_PPV_ARGS(&up)))) {
        Fail("创建上传缓冲失败");
        return -1;
    }
    void* mapped = nullptr;
    if (FAILED(up->Map(0, nullptr, &mapped))) {
        return -1;
    }
    // 逐行拷贝：源是紧凑的 row_bytes 字节一行，目标是 256 对齐的 row 字节一行。
    for (int y = 0; y < height; ++y) {
        std::memcpy(static_cast<uint8_t*>(mapped) + y * row,
                    pixels + static_cast<size_t>(y) * row_bytes,
                    static_cast<size_t>(row_bytes));
    }
    up->Unmap(0, nullptr);

    alloc_->Reset();
    cmd_->Reset(alloc_.Get(), nullptr);
    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = tex.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = up.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Offset = 0;
    src.PlacedFootprint.Footprint.Format = fmt;
    src.PlacedFootprint.Footprint.Width = static_cast<UINT>(width);
    src.PlacedFootprint.Footprint.Height = static_cast<UINT>(height);
    src.PlacedFootprint.Footprint.Depth = 1;
    src.PlacedFootprint.Footprint.RowPitch = static_cast<UINT>(row);
    cmd_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    D3D12_RESOURCE_BARRIER b = Transition(tex.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                          D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmd_->ResourceBarrier(1, &b);
    cmd_->Close();
    ID3D12CommandList* lists[] = {cmd_.Get()};
    queue_->ExecuteCommandLists(1, lists);

    // 等 GPU 完成，之后上传缓冲就可以释放了。
    Wait_Queue(queue_.Get(), fence_.Get(), fence_event_, fence_value_);

    const UINT slot = srv_used_++;
    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
    sv.Format = fmt;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2D.MipLevels = 1;
    device_->CreateShaderResourceView(tex.Get(), &sv,
                                      Cpu_Handle(srv_heap_.Get(), slot, srv_size_));

    GpuSprite s;
    s.width = width;
    s.height = height;
    s.rgba = rgba;
    s.index_texture = tex;
    s.index_srv = Gpu_Handle(srv_heap_.Get(), slot, srv_size_);
    sprites_.push_back(s);
    return static_cast<int>(sprites_.size()) - 1;
}

void Dx12Renderer::Set_Palette(const Palette& pal) {
    if (!device_ || !palette_tex_) {
        return;
    }
    uint8_t rgba[256 * 4];
    pal.To_RGBA8(rgba);
    const UINT64 total = sizeof(rgba);

    ComPtr<ID3D12Resource> up;
    D3D12_HEAP_PROPERTIES uhp = {};
    uhp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC urd = {};
    urd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    urd.Width = total;
    urd.Height = 1;
    urd.DepthOrArraySize = 1;
    urd.MipLevels = 1;
    urd.SampleDesc.Count = 1;
    urd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(device_->CreateCommittedResource(&uhp, D3D12_HEAP_FLAG_NONE, &urd,
                                                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                IID_PPV_ARGS(&up)))) {
        return;
    }
    void* mapped = nullptr;
    if (FAILED(up->Map(0, nullptr, &mapped))) {
        return;
    }
    std::memcpy(mapped, rgba, total);
    up->Unmap(0, nullptr);

    alloc_->Reset();
    cmd_->Reset(alloc_.Get(), nullptr);
    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = palette_tex_.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = up.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    src.PlacedFootprint.Footprint.Width = 256;
    src.PlacedFootprint.Footprint.Height = 1;
    src.PlacedFootprint.Footprint.Depth = 1;
    src.PlacedFootprint.Footprint.RowPitch = 256 * 4;
    cmd_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    D3D12_RESOURCE_BARRIER b = Transition(palette_tex_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                          D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmd_->ResourceBarrier(1, &b);
    cmd_->Close();
    ID3D12CommandList* lists[] = {cmd_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    Wait_Queue(queue_.Get(), fence_.Get(), fence_event_, fence_value_);
}

void Dx12Renderer::Begin_Frame(const float clear_color[4]) {
    if (!device_ || in_frame_) {
        return;
    }
    in_frame_ = true;
    frame_index_ = headless_ ? 0 : swapchain_->GetCurrentBackBufferIndex();
    alloc_->Reset();
    cmd_->Reset(alloc_.Get(), nullptr);

    ID3D12Resource* bb = Current_Target();
    if (!bb) {
        Fail("没有渲染目标");
        return;
    }
    if (!headless_) {
        D3D12_RESOURCE_BARRIER b = Transition(bb, D3D12_RESOURCE_STATE_PRESENT,
                                              D3D12_RESOURCE_STATE_RENDER_TARGET);
        cmd_->ResourceBarrier(1, &b);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = Cpu_Handle(rtv_heap_.Get(), frame_index_, rtv_size_);
    cmd_->ClearRenderTargetView(rtv, clear_color, 0, nullptr);
    cmd_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    D3D12_VIEWPORT vp = {};
    vp.TopLeftX = vp.TopLeftY = 0.0f;
    vp.Width = static_cast<float>(vp_width_);
    vp.Height = static_cast<float>(vp_height_);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    cmd_->RSSetViewports(1, &vp);
    D3D12_RECT sc = {0, 0, static_cast<LONG>(vp_width_), static_cast<LONG>(vp_height_)};
    cmd_->RSSetScissorRects(1, &sc);
    cmd_->SetGraphicsRootSignature(root_sig_.Get());
    cmd_->SetPipelineState(pso_.Get());
    cmd_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    ID3D12DescriptorHeap* heaps[] = {srv_heap_.Get()};
    cmd_->SetDescriptorHeaps(1, heaps);
    // t1 = 调色板，全程不变
    cmd_->SetGraphicsRootDescriptorTable(1, Gpu_Handle(srv_heap_.Get(), 0, srv_size_));
}

void Dx12Renderer::Draw_Sprite(int sprite, int dx, int dy, float scale) {
    if (!in_frame_ || sprite < 0 || sprite >= static_cast<int>(sprites_.size())) {
        return;
    }
    const GpuSprite& s = sprites_[sprite];
    // 像素坐标 -> NDC。左上角为 (dx, dy)，y 向下为负。
    const float w = static_cast<float>(s.width) * scale;
    const float h = static_cast<float>(s.height) * scale;
    const float ndc_x = (static_cast<float>(dx) / vp_width_) * 2.0f - 1.0f;
    const float ndc_y = 1.0f - (static_cast<float>(dy) / vp_height_) * 2.0f;
    const float ndc_w = (w / vp_width_) * 2.0f;
    const float ndc_h = (h / vp_height_) * 2.0f;
    const float xform[8] = {ndc_x, ndc_y, ndc_w, ndc_h, 1.0f, 1.0f, 1.0f, 1.0f};

    // 真彩精灵用另一条 PSO（像素着色器不查调色板），其余完全一致。
    cmd_->SetPipelineState(s.rgba ? pso_rgba_.Get() : pso_.Get());
    cmd_->SetGraphicsRootDescriptorTable(0, s.index_srv);
    cmd_->SetGraphicsRoot32BitConstants(2, 8, xform, 0);
    cmd_->DrawInstanced(6, 1, 0, 0);
}

void Dx12Renderer::Draw_Rect(int x, int y, int w, int h, const float color[4]) {
    if (!in_frame_ || w <= 0 || h <= 0) {
        return;
    }
    const float ndc_x = (static_cast<float>(x) / vp_width_) * 2.0f - 1.0f;
    const float ndc_y = 1.0f - (static_cast<float>(y) / vp_height_) * 2.0f;
    const float ndc_w = (static_cast<float>(w) / vp_width_) * 2.0f;
    const float ndc_h = (static_cast<float>(h) / vp_height_) * 2.0f;
    const float k[8] = {ndc_x, ndc_y, ndc_w, ndc_h, color[0], color[1], color[2],
                        color[3]};
    cmd_->SetPipelineState(pso_solid_.Get());
    cmd_->SetGraphicsRoot32BitConstants(2, 8, k, 0);
    cmd_->DrawInstanced(6, 1, 0, 0);
}

void Dx12Renderer::Draw_Rect_Outline(int x, int y, int w, int h,
                                     const float color[4], int thickness) {
    // 四条边各一个填充矩形。厚度默认 1 像素，框选时用 2 更接近原版观感。
    if (thickness < 1) {
        thickness = 1;
    }
    Draw_Rect(x, y, w, thickness, color);                     // 上
    Draw_Rect(x, y + h - thickness, w, thickness, color);     // 下
    Draw_Rect(x, y, thickness, h, color);                     // 左
    Draw_Rect(x + w - thickness, y, thickness, h, color);     // 右
}

void Dx12Renderer::End_Frame() {
    if (!in_frame_) {
        return;
    }
    in_frame_ = false;
    ID3D12Resource* bb = Current_Target();

    ComPtr<ID3D12Resource> readback;
    UINT row_pitch = 0;
    if (capture_requested_) {
        // 后台缓冲 -> 回读缓冲。行距必须按 256 字节对齐（D3D12 的硬性要求）。
        row_pitch = (static_cast<UINT>(vp_width_) * 4 + 255) & ~255u;
        D3D12_HEAP_PROPERTIES rhp = {};
        rhp.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC rrd = {};
        rrd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rrd.Width = static_cast<UINT64>(row_pitch) * vp_height_;
        rrd.Height = 1;
        rrd.DepthOrArraySize = 1;
        rrd.MipLevels = 1;
        rrd.SampleDesc.Count = 1;
        rrd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        const HRESULT hr = device_->CreateCommittedResource(
            &rhp, D3D12_HEAP_FLAG_NONE, &rrd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&readback));
        if (SUCCEEDED(hr)) {
            D3D12_RESOURCE_BARRIER to_copy = Transition(bb, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                       D3D12_RESOURCE_STATE_COPY_SOURCE);
            cmd_->ResourceBarrier(1, &to_copy);
            D3D12_TEXTURE_COPY_LOCATION dst = {};
            dst.pResource = readback.Get();
            dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            dst.PlacedFootprint.Offset = 0;
            dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            dst.PlacedFootprint.Footprint.Width = static_cast<UINT>(vp_width_);
            dst.PlacedFootprint.Footprint.Height = static_cast<UINT>(vp_height_);
            dst.PlacedFootprint.Footprint.Depth = 1;
            dst.PlacedFootprint.Footprint.RowPitch = row_pitch;
            D3D12_TEXTURE_COPY_LOCATION src = {};
            src.pResource = bb;
            src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            src.SubresourceIndex = 0;
            cmd_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
            D3D12_RESOURCE_BARRIER back = Transition(bb, D3D12_RESOURCE_STATE_COPY_SOURCE,
                                                     D3D12_RESOURCE_STATE_RENDER_TARGET);
            cmd_->ResourceBarrier(1, &back);
        } else {
            char msg[200];
            std::snprintf(
                msg, sizeof(msg),
                "创建回读缓冲失败 hr=0x%08lX removed=0x%08lX (w=%d h=%d pitch=%u size=%llu)",
                static_cast<unsigned long>(hr),
                static_cast<unsigned long>(device_->GetDeviceRemovedReason()), vp_width_,
                vp_height_, row_pitch, static_cast<unsigned long long>(rrd.Width));
            Fail(msg);
        }
    }

    if (!headless_) {
        D3D12_RESOURCE_BARRIER b = Transition(bb, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                              D3D12_RESOURCE_STATE_PRESENT);
        cmd_->ResourceBarrier(1, &b);
    }
    cmd_->Close();
    ID3D12CommandList* lists[] = {cmd_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    if (!headless_) {
        const HRESULT hr = swapchain_->Present(1, 0);
        if (FAILED(hr)) {
            char msg[128];
            std::snprintf(msg, sizeof(msg), "Present 失败 hr=0x%08lX",
                          static_cast<unsigned long>(hr));
            Fail(msg);
        }
    }
    Wait_Queue(queue_.Get(), fence_.Get(), fence_event_, fence_value_);

    if (capture_requested_ && readback) {
        capture_requested_ = false;
        void* p = nullptr;
        if (SUCCEEDED(readback->Map(0, nullptr, &p))) {
            capture_.assign(static_cast<size_t>(vp_width_) * vp_height_ * 4, 0);
            const uint8_t* s = static_cast<const uint8_t*>(p);
            for (int y = 0; y < vp_height_; ++y) {
                std::memcpy(capture_.data() + static_cast<size_t>(y) * vp_width_ * 4,
                            s + static_cast<size_t>(y) * row_pitch,
                            static_cast<size_t>(vp_width_) * 4);
            }
            readback->Unmap(0, nullptr);
            capture_w_ = vp_width_;
            capture_h_ = vp_height_;
        }
    }
}

bool Dx12Renderer::Get_Capture(std::vector<uint8_t>& out_rgba, int& out_w, int& out_h) const {
    if (capture_.empty()) {
        return false;
    }
    out_rgba = capture_;
    out_w = capture_w_;
    out_h = capture_h_;
    return true;
}

void Dx12Renderer::Shutdown() {
    sprites_.clear();
    if (fence_event_) {
        CloseHandle(fence_event_);
        fence_event_ = nullptr;
    }
    backbuffers_[0].Reset();
    backbuffers_[1].Reset();
    device_.Reset();
}

}  // namespace ra2
