// Dx12Renderer.cpp
//
// 最精简可用的一套 DX12：设备 / 交换链 / 根签名 / 一个 PSO / 精灵上传。
// 着色器源码内联在下面，用 D3DCompile 运行时编译（省掉一条 fxc 构建步骤）。

#include "gfx/dx12/Dx12Renderer.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <d3dcompiler.h>
#include <d3d12sdklayers.h>

#include "gfx/VxlNormals.h"

namespace ra2 {
namespace {

/// 着色器可见 SRV 堆的槽位数（见 Create_Descriptor_Heaps 的注释）。
constexpr UINT kSrvCapacity = 4096;

/// 体素烘焙画布的最大边长。坦克 scale=8 约 120×90；校车 BUS 约 557×459，
/// 512² 不够会 Bake 失败，抬到 1024²。
constexpr int kBakeMax = 1024;

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

// ---------------------------------------------------------------------------
// 体素光栅化：投影 / 明暗查表 / 调色板查表全在这里做，CPU 只负责解码。
//
// 每个体素 = 一个屏幕空间方块（scale × scale 像素），一次 DrawInstanced。
// 画家序不靠 CPU 排序，靠深度缓冲：dep = px+py+pz 归一化后写深度，
// 深度测试 LESS 让近的赢。可证与 CPU 的"按 dep 排序后从远到近落格"等价。
// ---------------------------------------------------------------------------
// 用 4 个 float4 而不是逐个标量声明：cbuffer 里 float3 会被补齐到 16 字节
// 边界，标量混排的偏移很容易和 CPU 侧的数组对不上（踩过一次，画面全黑）。
// 打包成 float4 之后偏移是确定的：CPU 那边就是 16 个 float 平铺。
cbuffer VC : register(b0) {
    float4 c0;   // xy = 画布像素尺寸, z = scale, w = cos(yaw)
    float4 c1;   // x = sin(yaw), yz = 投影原点 (x0,y0), w = depth_bias
    float4 c2;   // xyz = 世界光向量, w = depth_scale
    float4 c3;   // x = ambient, y = diffuse, z = levels（0.6 / 0.8 / 16）
};

StructuredBuffer<uint2> voxBuf  : register(t0);   // 打包的体素
StructuredBuffer<float4> limbBuf : register(t1);  // 每根肢体 4×float4
Texture2D<float4> normTex : register(t2);         // 法线表 256×4，w=1 表示有效
Texture2D<float4> palTex  : register(t3);         // 调色板 256×1

struct VOut {
    float4 pos : SV_Position;
    float4 col : COLOR0;
};

VOut VSVoxel(uint vid : SV_VertexID, uint iid : SV_InstanceID) {
    uint2 v = voxBuf[iid];
    uint vx = v.x & 0xFFu;
    uint vy = (v.x >> 8) & 0xFFu;
    uint vz = (v.x >> 16) & 0xFFu;
    uint vc = (v.x >> 24) & 0xFFu;   // 调色板索引
    uint vn = v.y & 0xFFu;           // 法线索引
    uint vl = (v.y >> 8) & 0xFFu;    // 肢体下标
    uint vs = (v.y >> 16) & 0xFFu;   // 法线表槽位

    float4 r0 = limbBuf[vl * 4 + 0];
    float4 r1 = limbBuf[vl * 4 + 1];
    float4 r2 = limbBuf[vl * 4 + 2];
    float3 mn = limbBuf[vl * 4 + 3].xyz;   // 肢体局部 AABB 最小角
    float2 vp = c0.xy;
    float scale = c0.z, yaw_c = c0.w;
    float yaw_s = c1.x, x0 = c1.y, y0 = c1.z, depth_bias = c1.w;
    float3 light = c2.xyz;
    float depth_scale = c2.w;
    float ambient = c3.x, diffuse = c3.y, levels = c3.z;
    // c3.w：阴影模式。>0.5 = 沿 -L 投到地面（与 VxlFile 0x 投影同一公式），
    // 写出黑+alpha；本体盖住影子靠后画不透明体素（Composite_Shadow 语义）。
    float shadow_mode = c3.w;

    // world = Yaw · (R·(index + min_bounds) + T)
    // min_bounds 这一步不能省：肢体局部原点在 AABB 中心，不加会散架。
    float3 lp = float3(float(vx), float(vy), float(vz)) + mn;
    float3 p = float3(dot(r0.xyz, lp) + r0.w,
                      dot(r1.xyz, lp) + r1.w,
                      dot(r2.xyz, lp) + r2.w);
    float3 w = float3(yaw_c * p.x - yaw_s * p.y,
                      yaw_s * p.x + yaw_c * p.y,
                      p.z);

    // 阴影：t = (pz - ground_z) / Lz；落点 = P - L*t（见 VxlFile.cpp）。
    // 阴影趟时 c1.w 塞 ground_z（本体趟仍是 dmin）。
    float3 draw_w = w;
    if (shadow_mode > 0.5) {
        float gz = depth_bias;
        float lz = light.z;
        if (lz > 1e-4) {
            float t = (w.z - gz) / lz;
            draw_w = float3(w.x - light.x * t, w.y - light.y * t, gz);
        } else {
            draw_w.z = gz;
        }
    }

    const float kk = 0.70710678;
    float sx = (draw_w.x - draw_w.y) * kk;
    float sy = (draw_w.x + draw_w.y) * kk * 0.5 - draw_w.z;
    float dep = draw_w.x + draw_w.y + draw_w.z;

    // 明暗：法线先过肢体旋转、再过朝向，然后和世界光向量点乘。
    //   level = (d > 0) ? floor(d * levels) : 0
    // exe 用的是 ftol（截断），所以这里也是 floor 而不是 round。
    // 法线表里没填到的项（w == 0）取最亮级 —— exe 是 `rep stosd` 先把整张
    // 表填 0x10 再算，语义一致。
    float4 nt = normTex.Load(int3(int(vn), int(vs), 0));
    float3 nl = float3(dot(r0.xyz, nt.xyz), dot(r1.xyz, nt.xyz), dot(r2.xyz, nt.xyz));
    float3 nw = float3(yaw_c * nl.x - yaw_s * nl.y,
                       yaw_s * nl.x + yaw_c * nl.y,
                       nl.z);
    float d = dot(nw, light);
    float lvl = (nt.w < 0.5) ? levels
              : ((d > 0.0) ? min(floor(d * levels), levels) : 0.0);

    float2 c = float2((vid == 1 || vid == 4 || vid == 5) ? 1.0 : 0.0,
                      (vid == 2 || vid == 3 || vid == 5) ? 1.0 : 0.0);
    // 方块原点要**先 floor 再铺 scale×scale**，不能直接让浮点范围去盖像素：
    // 后者是按"像素中心是否落在范围内"取整的，等效于四舍五入，和 CPU 那边的
    // (int) 截断在不同小数部分上会差 1 个像素。体素方块本来就有重叠（相邻体素
    // 在屏幕上只隔 0.7071×scale 像素，比 scale 小），差 1 像素就会换掉重叠区
    // 里"谁盖谁"，细节（装甲缝、履带）会整片对不上。
    float px = floor((sx - x0) * scale) + c.x * scale;
    float py = floor((sy - y0) * scale) + c.y * scale;

    VOut o;
    if (shadow_mode > 0.5) {
        // 影子画在本体之前；深度写 0（最远），本体 GREATER 盖住脚下阴影
        // —— 对齐 Composite_Shadow「本体像素不画影子」。
        o.pos = float4(px / vp.x * 2.0 - 1.0,
                       1.0 - py / vp.y * 2.0,
                       0.0,
                       1.0);
        // 阴影趟：c3.x 塞的是 shadow_alpha，不是 ambient。
        o.col = float4(0.0, 0.0, 0.0, (vc == 0u) ? 0.0 : ambient);
        return o;
    }
    o.pos = float4(px / vp.x * 2.0 - 1.0,
                   1.0 - py / vp.y * 2.0,
                   saturate((dep - depth_bias) * depth_scale),
                   1.0);
    // 最终颜色 = 调色板色 × 明暗系数。系数就是 exe 的 level/16*0.8+0.6。
    // 索引 0 是"无此色"，输出全透明 —— 和 CPU 侧 Shade_To_RGBA 的语义一致。
    float4 pc = palTex.Load(int3(int(vc), 0, 0));
    float f = ambient + diffuse * (lvl / max(levels, 1.0));
    o.col = float4(saturate(pc.rgb * f), (vc == 0u) ? 0.0 : 1.0);
    return o;
}

float4 PSVoxel(VOut i) : SV_Target {
    return i.col;
}
)";

void SetError(char* dst, const char* msg) {
    std::snprintf(dst, 256, "%s", msg);
}

/// 设了 RA2_D3D_DEBUG 才打诊断，正常跑不刷屏。
bool Debug_Trace_On() { return std::getenv("RA2_D3D_DEBUG") != nullptr; }

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

void Dx12Renderer::Flush_Debug_Messages() {
    if (!device_) {
        return;
    }
    ComPtr<ID3D12InfoQueue> iq;
    if (FAILED(device_.As(&iq)) || !iq) {
        return;
    }
    const UINT64 n = iq->GetNumStoredMessages();
    for (UINT64 i = 0; i < n; ++i) {
        SIZE_T len = 0;
        if (FAILED(iq->GetMessage(i, nullptr, &len)) || len == 0) {
            continue;
        }
        std::vector<uint8_t> buf(len);
        D3D12_MESSAGE* m = reinterpret_cast<D3D12_MESSAGE*>(buf.data());
        if (SUCCEEDED(iq->GetMessage(i, m, &len)) && m->pDescription != nullptr) {
            std::fprintf(stderr, "[D3D12] %s\n", m->pDescription);
        }
    }
    iq->ClearStoredMessages();
}

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
    // 【顺序是有讲究的，别随便挪】srv_used_ 是描述符槽位的水位线，谁先分配谁
    // 拿前面的槽位。slot 0 必须**先**留给全局调色板（索引色精灵管线要用），
    // 否则后面建体素资源时会先占掉 slot 0/1，然后全局调色板又回过头覆写
    // slot 0、体素缓冲覆写 slot 1 —— 两个纹理描述符全被顶掉，
    // 症状是"形状对、颜色全黑、法线查表也全空"（踩过，很难定位）。
    if (!Create_Descriptor_Heaps()) {
        return false;
    }
    if (!Create_Global_Palette()) {
        return false;
    }
    if (!Create_Pipeline()) {
        return false;
    }
    if (!Create_Voxel_Resources()) {
        return false;
    }
    if (!Create_Voxel_Pipeline()) {
        return false;
    }
    return true;
}

bool Dx12Renderer::Create_Global_Palette() {
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
    srv_used_ = 1;   // slot 0 归全局调色板，后面的分配从 1 开始
    return true;
}

bool Dx12Renderer::Create_Device() {
    // 调试层：设 RA2_D3D_DEBUG=1 才开。开启后非法调用会在**发生那一行**
    // 打印原因，而不是等到设备被摘掉才报 DEVICE_REMOVED。
    if (std::getenv("RA2_D3D_DEBUG") != nullptr) {
        ComPtr<ID3D12Debug> dbg;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg)))) {
            dbg->EnableDebugLayer();
            std::printf("[DX12] 调试层已开\n");
        } else {
            std::printf("[DX12] 调试层不可用（要装 Graphics Tools 可选功能）\n");
        }
    }
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

    // fence 要在这儿就建好：体素资源的首次上传（Create_Voxel_Resources）也要
    // 等 GPU，而它跑在 Finish_Init 里、比原来的建 fence 位置更早。
    if (FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)))) {
        Fail("创建 fence 失败");
        return false;
    }
    fence_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (fence_event_ == nullptr) {
        Fail("创建 fence 事件失败");
        return false;
    }
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
    rh.NumDescriptors = 3;   // 0/1 = 后台缓冲（或离屏），2 = 体素烘焙临时目标
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
    // 4096 而不是 256：体素路径每个"几何"要占 3 个槽（体素 / 肢体 / 调色板），
    // 每烘出的一张精灵还要 1 个。8 朝向 × 多阵营一展开，256 个远远不够。
    // 描述符堆本身很便宜（一个槽 32~64 字节），放开不用省。
    sh.NumDescriptors = kSrvCapacity;
    sh.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device_->CreateDescriptorHeap(&sh, IID_PPV_ARGS(&srv_heap_)))) {
        Fail("创建 SRV 堆失败");
        return false;
    }
    srv_size_ = device_->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    srv_capacity_ = kSrvCapacity;
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
// 缓冲 / 纹理上传的公共片段
// ---------------------------------------------------------------------------

ComPtr<ID3D12Resource> Dx12Renderer::Upload_Buffer(const void* data, size_t bytes,
                                                   D3D12_RESOURCE_STATES after) {
    ComPtr<ID3D12Resource> res;
    if (!device_ || data == nullptr || bytes == 0) {
        return res;
    }
    const UINT64 size = (static_cast<UINT64>(bytes) + 255) & ~static_cast<UINT64>(255);
    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = size;
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                IID_PPV_ARGS(&res)))) {
        Fail("创建缓冲失败");
        return res;
    }
    ComPtr<ID3D12Resource> up;
    D3D12_HEAP_PROPERTIES uhp = {};
    uhp.Type = D3D12_HEAP_TYPE_UPLOAD;
    if (FAILED(device_->CreateCommittedResource(&uhp, D3D12_HEAP_FLAG_NONE, &rd,
                                                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                IID_PPV_ARGS(&up)))) {
        Fail("创建上传缓冲失败");
        res.Reset();
        return res;
    }
    void* p = nullptr;
    if (FAILED(up->Map(0, nullptr, &p))) {
        res.Reset();
        return res;
    }
    std::memcpy(p, data, bytes);
    up->Unmap(0, nullptr);

    alloc_->Reset();
    cmd_->Reset(alloc_.Get(), nullptr);
    cmd_->CopyBufferRegion(res.Get(), 0, up.Get(), 0, size);
    D3D12_RESOURCE_BARRIER b =
        Transition(res.Get(), D3D12_RESOURCE_STATE_COPY_DEST, after);
    cmd_->ResourceBarrier(1, &b);
    cmd_->Close();
    ID3D12CommandList* lists[] = {cmd_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    Wait_Queue(queue_.Get(), fence_.Get(), fence_event_, fence_value_);
    return res;
}

bool Dx12Renderer::Upload_Texture_Data(ID3D12Resource* dst, const void* data, UINT w,
                                       UINT h, DXGI_FORMAT fmt, UINT bpp,
                                       D3D12_RESOURCE_STATES after) {
    if (!device_ || dst == nullptr || data == nullptr || w == 0 || h == 0) {
        return false;
    }
    const UINT64 row_bytes = static_cast<UINT64>(w) * bpp;
    const UINT64 row = (row_bytes + 255) & ~static_cast<UINT64>(255);
    const UINT64 total = row * h;
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
        return false;
    }
    void* p = nullptr;
    if (FAILED(up->Map(0, nullptr, &p))) {
        return false;
    }
    const uint8_t* src = static_cast<const uint8_t*>(data);
    for (UINT y = 0; y < h; ++y) {
        std::memcpy(static_cast<uint8_t*>(p) + static_cast<size_t>(y) * row,
                    src + static_cast<size_t>(y) * row_bytes,
                    static_cast<size_t>(row_bytes));
    }
    up->Unmap(0, nullptr);

    alloc_->Reset();
    cmd_->Reset(alloc_.Get(), nullptr);
    D3D12_TEXTURE_COPY_LOCATION d = {};
    d.pResource = dst;
    d.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION s = {};
    s.pResource = up.Get();
    s.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    s.PlacedFootprint.Footprint.Format = fmt;
    s.PlacedFootprint.Footprint.Width = w;
    s.PlacedFootprint.Footprint.Height = h;
    s.PlacedFootprint.Footprint.Depth = 1;
    s.PlacedFootprint.Footprint.RowPitch = static_cast<UINT>(row);
    cmd_->CopyTextureRegion(&d, 0, 0, 0, &s, nullptr);
    D3D12_RESOURCE_BARRIER b =
        Transition(dst, D3D12_RESOURCE_STATE_COPY_DEST, after);
    cmd_->ResourceBarrier(1, &b);
    cmd_->Close();
    ID3D12CommandList* lists[] = {cmd_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    Wait_Queue(queue_.Get(), fence_.Get(), fence_event_, fence_value_);
    return true;
}

UINT Dx12Renderer::Alloc_Srv_Structured(ID3D12Resource* res, UINT stride, UINT count) {
    if (!device_ || res == nullptr || srv_used_ >= srv_capacity_) {
        return 0xFFFFFFFFu;
    }
    const UINT slot = srv_used_++;
    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
    sv.Format = DXGI_FORMAT_UNKNOWN;   // 结构化缓冲必须 UNKNOWN，格式在 stride 里
    sv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Buffer.FirstElement = 0;
    sv.Buffer.NumElements = count;
    sv.Buffer.StructureByteStride = stride;
    device_->CreateShaderResourceView(res, &sv,
                                      Cpu_Handle(srv_heap_.Get(), slot, srv_size_));
    return slot;
}

UINT Dx12Renderer::Alloc_Srv_Texture(ID3D12Resource* res, DXGI_FORMAT fmt) {
    if (!device_ || res == nullptr || srv_used_ >= srv_capacity_) {
        return 0xFFFFFFFFu;
    }
    const UINT slot = srv_used_++;
    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
    sv.Format = fmt;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2D.MipLevels = 1;
    device_->CreateShaderResourceView(res, &sv,
                                      Cpu_Handle(srv_heap_.Get(), slot, srv_size_));
    return slot;
}

// ---------------------------------------------------------------------------
// 体素资源与管线
// ---------------------------------------------------------------------------

bool Dx12Renderer::Create_Voxel_Resources() {
    // 1) 法线表：4 个槽位 × 256 项，打包成一张 256×4 的 RGBA32F。
    //    w 分量是"有效位" —— exe 里表是先用 rep stosd 填成最亮级再逐项算的，
    //    表里没填到的法线索引必须落到"最亮"，着色器靠 w 来区分。
    {
        std::vector<float> buf(static_cast<size_t>(256) * kNormalSlotCount * 4, 0.0f);
        for (int slot = 0; slot < kNormalSlotCount; ++slot) {
            int count = 0;
            const float* tab = Voxel_Normal_Table(slot, &count);
            if (tab == nullptr) {
                continue;
            }
            if (count > 256) {
                count = 256;
            }
            for (int i = 0; i < count; ++i) {
                const size_t o = (static_cast<size_t>(slot) * 256 + i) * 4;
                buf[o + 0] = tab[static_cast<size_t>(i) * 3 + 0];
                buf[o + 1] = tab[static_cast<size_t>(i) * 3 + 1];
                buf[o + 2] = tab[static_cast<size_t>(i) * 3 + 2];
                buf[o + 3] = 1.0f;
            }
        }
        D3D12_HEAP_PROPERTIES hp = {};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd = {};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width = 256;
        rd.Height = kNormalSlotCount;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        if (FAILED(device_->CreateCommittedResource(
                &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                IID_PPV_ARGS(&normals_tex_)))) {
            Fail("创建法线表纹理失败");
            return false;
        }
        Upload_Texture_Data(normals_tex_.Get(), buf.data(), 256, kNormalSlotCount,
                            DXGI_FORMAT_R32G32B32A32_FLOAT, 16,
                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        const UINT slot = Alloc_Srv_Texture(normals_tex_.Get(),
                                            DXGI_FORMAT_R32G32B32A32_FLOAT);
        if (slot == 0xFFFFFFFFu) {
            return false;
        }
        normals_srv_ = Gpu_Handle(srv_heap_.Get(), slot, srv_size_);
    }

    // 2) 体素调色板 256×1。每次烘焙前重写（一张一张烘，共用就行）。
    {
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
        if (FAILED(device_->CreateCommittedResource(
                &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                IID_PPV_ARGS(&voxel_palette_)))) {
            Fail("创建体素调色板纹理失败");
            return false;
        }
        // 先灌一次空调色板，把它从 COPY_DEST 转到"可读"状态。
        // 之后每次烘焙都是 NON_PIXEL_SHADER_RESOURCE -> COPY_DEST -> 上传，
        // 状态机才对得上。
        const uint8_t pal0[256 * 4] = {};
        Upload_Texture_Data(voxel_palette_.Get(), pal0, 256, 1,
                            DXGI_FORMAT_R8G8B8A8_UNORM, 4,
                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        const UINT slot = Alloc_Srv_Texture(voxel_palette_.Get(),
                                            DXGI_FORMAT_R8G8B8A8_UNORM);
        if (slot == 0xFFFFFFFFu) {
            return false;
        }
        voxel_palette_srv_ = Gpu_Handle(srv_heap_.Get(), slot, srv_size_);

        // 常驻上传缓冲（256×4 = 1KB，256 字节对齐）。烘焙时 CPU 直接写这里，
        // 不再为一次调色板更新单独起一段命令列表等 GPU。
        D3D12_HEAP_PROPERTIES uhp = {};
        uhp.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC urd = {};
        urd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        urd.Width = 256 * 4;
        urd.Height = 1;
        urd.DepthOrArraySize = 1;
        urd.MipLevels = 1;
        urd.SampleDesc.Count = 1;
        urd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(device_->CreateCommittedResource(
                &uhp, D3D12_HEAP_FLAG_NONE, &urd,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                IID_PPV_ARGS(&voxel_palette_up_)))) {
            Fail("创建调色板上传缓冲失败");
            return false;
        }
        if (FAILED(voxel_palette_up_->Map(0, nullptr, &voxel_palette_up_ptr_))) {
            Fail("映射调色板上传缓冲失败");
            return false;
        }
    }

    // 3) 烘焙用的临时渲染目标 + 深度缓冲。
    {
        D3D12_HEAP_PROPERTIES hp = {};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd = {};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width = kBakeMax;
        rd.Height = kBakeMax;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        // 建的时候就给清屏值：调试层会为"没带清屏值的 ClearRenderTargetView"
        // 报一条"这条清屏会变慢"的提示，顺手消掉。
        D3D12_CLEAR_VALUE bake_cv = {};
        bake_cv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        if (FAILED(device_->CreateCommittedResource(
                &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_RENDER_TARGET,
                &bake_cv, IID_PPV_ARGS(&bake_tex_)))) {
            Fail("创建体素烘焙目标失败");
            return false;
        }
        device_->CreateRenderTargetView(bake_tex_.Get(), nullptr,
                                        Cpu_Handle(rtv_heap_.Get(), 2, rtv_size_));

        D3D12_RESOURCE_DESC dd = rd;
        dd.Format = DXGI_FORMAT_D32_FLOAT;
        dd.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_CLEAR_VALUE cv = {};
        cv.Format = DXGI_FORMAT_D32_FLOAT;
        cv.DepthStencil.Depth = 0.0f;   // 画家序：键值越大越近，所以清成 0
        if (FAILED(device_->CreateCommittedResource(
                &hp, D3D12_HEAP_FLAG_NONE, &dd, D3D12_RESOURCE_STATE_DEPTH_WRITE, &cv,
                IID_PPV_ARGS(&bake_depth_)))) {
            Fail("创建体素深度缓冲失败");
            return false;
        }
        D3D12_DESCRIPTOR_HEAP_DESC dh = {};
        dh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dh.NumDescriptors = 1;
        if (FAILED(device_->CreateDescriptorHeap(&dh, IID_PPV_ARGS(&dsv_heap_)))) {
            Fail("创建 DSV 堆失败");
            return false;
        }
        dsv_size_ = device_->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        device_->CreateDepthStencilView(bake_depth_.Get(), nullptr,
                                        dsv_heap_->GetCPUDescriptorHandleForHeapStart());
    }
    if (Debug_Trace_On()) {
        std::fprintf(stderr, "[trace] Create_Voxel_Resources: removed=0x%08lX\n",
                     static_cast<unsigned long>(device_->GetDeviceRemovedReason()));
        Flush_Debug_Messages();
    }
    return true;
}

bool Dx12Renderer::Create_Voxel_Pipeline() {
    ComPtr<ID3DBlob> vs, ps, err;
    const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
    if (FAILED(D3DCompile(kShaderSource, std::strlen(kShaderSource), nullptr, nullptr,
                          nullptr, "VSVoxel", "vs_5_0", flags, 0, &vs, &err))) {
        Fail(err ? static_cast<const char*>(err->GetBufferPointer())
                 : "体素顶点着色器编译失败");
        return false;
    }
    if (FAILED(D3DCompile(kShaderSource, std::strlen(kShaderSource), nullptr, nullptr,
                          nullptr, "PSVoxel", "ps_5_0", flags, 0, &ps, &err))) {
        Fail(err ? static_cast<const char*>(err->GetBufferPointer())
                 : "体素像素着色器编译失败");
        return false;
    }

    // t0 体素 / t1 肢体矩阵 / t2 法线表 / t3 调色板，b0 是根常量。
    // 前三个都在顶点着色器里读（体素是逐实例的，投影也在 VS 里做完），
    // 所以可见性写 VERTEX —— 写 ALL 也行，写 VERTEX 更贴切。
    D3D12_DESCRIPTOR_RANGE ranges[4] = {};
    for (int i = 0; i < 4; ++i) {
        ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[i].NumDescriptors = 1;
        ranges[i].BaseShaderRegister = static_cast<UINT>(i);
    }
    D3D12_ROOT_PARAMETER params[5] = {};
    for (int i = 0; i < 4; ++i) {
        params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        params[i].DescriptorTable.NumDescriptorRanges = 1;
        params[i].DescriptorTable.pDescriptorRanges = &ranges[i];
    }
    params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[4].Constants.Num32BitValues = 16;
    params[4].Constants.ShaderRegister = 0;

    D3D12_ROOT_SIGNATURE_DESC rsd = {};
    rsd.NumParameters = 5;
    rsd.pParameters = params;
    rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> sig;
    if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig,
                                           nullptr))) {
        Fail("序列化体素根签名失败");
        return false;
    }
    if (FAILED(device_->CreateRootSignature(0, sig->GetBufferPointer(),
                                            sig->GetBufferSize(),
                                            IID_PPV_ARGS(&root_sig_voxel_)))) {
        Fail("创建体素根签名失败");
        return false;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
    pd.pRootSignature = root_sig_voxel_.Get();
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
    // 画家序：depth = px+py+pz，**越大越近**，所以清 0、测 GREATER_EQUAL。
    //
    // 【踩过的坑】原版写的是 GREATER。Shadow 趟 vertex shader 把 depth=0
    // （NDC 最远端）写进去，配 "清 0 + GREATER" 会让 `0 > 0` 失败 —— shadow
    // 一个像素都进不了 bake RT。dump `build/_arena_off.raw`：全部 786432
    // 个像素 alpha 只是 {0, 255}，中间值（即 0.45*255 ≈ 115）一条都没有，
    // 那正是 shadow 被静默踢掉的现场。改 GREATER_EQUAL（`0 >= 0` 通过）后，
    // 本体 depth > 0 仍按 GREATER 严格通过 —— 画家序"近盖远"语义不变。
    pd.DepthStencilState.DepthEnable = TRUE;
    pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pd.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    pd.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pd.NumRenderTargets = 1;
    pd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pd.SampleDesc.Count = 1;
    if (FAILED(device_->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pso_voxel_)))) {
        Fail("创建体素 PSO 失败");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
int Dx12Renderer::Upload_Voxel_Geom(const uint32_t* voxels, int count,
                                    const VxlGpuLimb* limbs, int limb_count) {
    if (!device_ || voxels == nullptr || count <= 0 || limbs == nullptr ||
        limb_count <= 0) {
        return -1;
    }
    // 肢体矩阵打包成 float4 × 4：3 行旋转平移 + min_bounds。
    std::vector<float> lm(static_cast<size_t>(limb_count) * 16, 0.0f);
    for (int i = 0; i < limb_count; ++i) {
        const VxlGpuLimb& g = limbs[i];
        float* d = &lm[static_cast<size_t>(i) * 16];
        d[0] = g.m[0];  d[1] = g.m[1];  d[2] = g.m[2];   d[3] = g.m[3];
        d[4] = g.m[4];  d[5] = g.m[5];  d[6] = g.m[6];   d[7] = g.m[7];
        d[8] = g.m[8];  d[9] = g.m[9];  d[10] = g.m[10]; d[11] = g.m[11];
        d[12] = g.min_b[0];
        d[13] = g.min_b[1];
        d[14] = g.min_b[2];
        d[15] = 0.0f;
    }

    GpuVoxelGeom g;
    g.count = count;
    g.voxel_buf = Upload_Buffer(voxels, static_cast<size_t>(count) * 8,
                                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    g.limb_buf = Upload_Buffer(lm.data(), lm.size() * sizeof(float),
                               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (!g.voxel_buf || !g.limb_buf) {
        return -1;
    }
    UINT s0 = Alloc_Srv_Structured(g.voxel_buf.Get(), 8, static_cast<UINT>(count));
    UINT s1 = Alloc_Srv_Structured(g.limb_buf.Get(), 16,
                                   static_cast<UINT>(limb_count) * 4);
    if (s0 == 0xFFFFFFFFu || s1 == 0xFFFFFFFFu) {
        return -1;
    }
    g.voxel_srv = Gpu_Handle(srv_heap_.Get(), s0, srv_size_);
    g.limb_srv = Gpu_Handle(srv_heap_.Get(), s1, srv_size_);
    voxel_geoms_.push_back(g);
    return static_cast<int>(voxel_geoms_.size()) - 1;
}

int Dx12Renderer::Bake_Voxels(int geom, const VoxelBakeParams& p, int* out_w,
                              int* out_h) {
    if (!device_ || in_frame_ || !pso_voxel_) {
        return -1;
    }
    if (geom < 0 || geom >= static_cast<int>(voxel_geoms_.size())) {
        return -1;
    }
    if (p.bbox == nullptr || p.depth_range == nullptr || p.pal768 == nullptr) {
        return -1;
    }
    const int w = static_cast<int>((p.bbox[2] - p.bbox[0]) * p.scale) + 2;
    const int h = static_cast<int>((p.bbox[3] - p.bbox[1]) * p.scale) + 2;
    if (w <= 0 || h <= 0 || w > kBakeMax || h > kBakeMax) {
        return -1;
    }
    const GpuVoxelGeom& g = voxel_geoms_[static_cast<size_t>(geom)];

    // 分步探针：非法调用不会当场报错，只会让设备在之后某个任意点被摘掉，
    // 所以每做完一段就查一次"设备还活着吗 + 调试层有没有攒下消息"。
    auto trace = [&](const char* where) {
        if (!Debug_Trace_On()) {
            return;
        }
        std::fprintf(stderr, "[trace] %s: removed=0x%08lX\n", where,
                     static_cast<unsigned long>(device_->GetDeviceRemovedReason()));
        Flush_Debug_Messages();
    };
    trace("bake 开始");

    // 1) 调色板：768 字节 RGB -> 256×1 RGBA。索引 0 留成透明。
    uint8_t pal[256 * 4];
    for (int i = 0; i < 256; ++i) {
        pal[i * 4 + 0] = p.pal768[i * 3 + 0];
        pal[i * 4 + 1] = p.pal768[i * 3 + 1];
        pal[i * 4 + 2] = p.pal768[i * 3 + 2];
        pal[i * 4 + 3] = (i == 0) ? 0 : 255;
    }
    // 写进常驻映射的上传缓冲：不需要单独一段命令列表，也就少一次等 GPU。
    if (voxel_palette_up_ptr_ != nullptr) {
        std::memcpy(voxel_palette_up_ptr_, pal, sizeof(pal));
    }
    if (Debug_Trace_On()) {
        std::fprintf(stderr,
                     "[trace] 调色板 idx32=(%d,%d,%d) idx48=(%d,%d,%d) "
                     "idx255=(%d,%d,%d)\n",
                     pal[32 * 4], pal[32 * 4 + 1], pal[32 * 4 + 2], pal[48 * 4],
                     pal[48 * 4 + 1], pal[48 * 4 + 2], pal[255 * 4],
                     pal[255 * 4 + 1], pal[255 * 4 + 2]);
    }
    trace("调色板上传后");

    // 2) 渲染到临时目标
    float light[3] = {0.40824829f, 0.40824829f, 0.81649658f};
    if (p.light != nullptr) {
        light[0] = p.light[0];
        light[1] = p.light[1];
        light[2] = p.light[2];
    }
    const float dmin = p.depth_range[0];
    const float dmax = p.depth_range[1];
    const float depth_scale = (dmax > dmin) ? (1.0f / (dmax - dmin)) : 1.0f;
    const float yaw_c = std::cos(p.yaw);
    const float yaw_s = std::sin(p.yaw);

    // 顺序必须和着色器里的 c0/c1/c2/c3 逐个对应（每个 float4 一组）。
    auto fill_consts = [&](float* k, bool shadow_pass) {
        k[0] = static_cast<float>(w);
        k[1] = static_cast<float>(h);
        k[2] = p.scale;
        k[3] = yaw_c;
        k[4] = yaw_s;
        k[5] = p.bbox[0];
        k[6] = p.bbox[1];
        // 阴影趟：c1.w = ground_z；本体趟：c1.w = dmin
        k[7] = shadow_pass ? p.ground_z : dmin;
        k[8] = light[0];
        k[9] = light[1];
        k[10] = light[2];
        k[11] = depth_scale;
        if (shadow_pass) {
            k[12] = p.shadow_alpha;
            k[13] = 0.0f;
            k[14] = 0.0f;
            k[15] = 1.0f;  // shadow_mode
        } else {
            k[12] = p.ambient;
            k[13] = p.diffuse;
            k[14] = p.levels;
            k[15] = 0.0f;
        }
    };

    if (Debug_Trace_On()) {
        std::fprintf(stderr,
                     "[trace] 烘焙 %d 个体素，画布 %dx%d shadow=%d\n",
                     g.count, w, h, p.shadow ? 1 : 0);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = Cpu_Handle(rtv_heap_.Get(), 2, rtv_size_);
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsv_heap_->GetCPUDescriptorHandleForHeapStart();

    alloc_->Reset();
    cmd_->Reset(alloc_.Get(), nullptr);
    // 调色板：NON_PIXEL_SHADER_RESOURCE -> COPY_DEST -> 拷 -> 转回可读。
    // 以前这三步各自开一段命令列表、各等一次 GPU（一次烘焙 4 次 fence 往返），
    // 现在全塞进这一段里，只等一次。
    {
        D3D12_RESOURCE_BARRIER b =
            Transition(voxel_palette_.Get(),
                       D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                       D3D12_RESOURCE_STATE_COPY_DEST);
        cmd_->ResourceBarrier(1, &b);
        D3D12_TEXTURE_COPY_LOCATION pd = {};
        pd.pResource = voxel_palette_.Get();
        pd.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION ps = {};
        ps.pResource = voxel_palette_up_.Get();
        ps.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        ps.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        ps.PlacedFootprint.Footprint.Width = 256;
        ps.PlacedFootprint.Footprint.Height = 1;
        ps.PlacedFootprint.Footprint.Depth = 1;
        ps.PlacedFootprint.Footprint.RowPitch = 256 * 4;
        cmd_->CopyTextureRegion(&pd, 0, 0, 0, &ps, nullptr);
        D3D12_RESOURCE_BARRIER b2 =
            Transition(voxel_palette_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                       D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        cmd_->ResourceBarrier(1, &b2);
    }
    const float clear[4] = {0, 0, 0, 0};
    cmd_->ClearRenderTargetView(rtv, clear, 0, nullptr);
    cmd_->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, nullptr);
    cmd_->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    D3D12_VIEWPORT vp = {};
    vp.Width = static_cast<float>(w);
    vp.Height = static_cast<float>(h);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    cmd_->RSSetViewports(1, &vp);
    D3D12_RECT sc = {0, 0, w, h};
    cmd_->RSSetScissorRects(1, &sc);
    cmd_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12DescriptorHeap* heaps[] = {srv_heap_.Get()};
    cmd_->SetDescriptorHeaps(1, heaps);
    cmd_->SetGraphicsRootSignature(root_sig_voxel_.Get());
    cmd_->SetPipelineState(pso_voxel_.Get());
    cmd_->SetGraphicsRootDescriptorTable(0, g.voxel_srv);
    cmd_->SetGraphicsRootDescriptorTable(1, g.limb_srv);
    cmd_->SetGraphicsRootDescriptorTable(2, normals_srv_);
    cmd_->SetGraphicsRootDescriptorTable(3, voxel_palette_srv_);
    UINT instances = static_cast<UINT>(g.count);
    if (std::getenv("RA2_VOXEL_PROBE") != nullptr) {
        instances = 1;
    }
    float k[16] = {};
    // 先影子后本体（与 VxlFile 先写 shadow_out 再画家序本体一致）。
    if (p.shadow && p.shadow_alpha > 0.0f) {
        fill_consts(k, true);
        cmd_->SetGraphicsRoot32BitConstants(4, 16, k, 0);
        cmd_->DrawInstanced(6, instances, 0, 0);
    }
    fill_consts(k, false);
    cmd_->SetGraphicsRoot32BitConstants(4, 16, k, 0);
    cmd_->DrawInstanced(6, instances, 0, 0);
    trace("录完绘制（还没提交）");

    // 3) 拷到一张尺寸刚好的纹理上（临时目标是复用的，下一次烘焙会覆盖）
    ComPtr<ID3D12Resource> dst;
    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = static_cast<UINT>(w);
    rd.Height = static_cast<UINT>(h);
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    if (FAILED(device_->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&dst)))) {
        Fail("创建体素烘焙结果纹理失败");
        return -1;
    }
    {
        D3D12_RESOURCE_BARRIER b =
            Transition(bake_tex_.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                       D3D12_RESOURCE_STATE_COPY_SOURCE);
        cmd_->ResourceBarrier(1, &b);
    }
    D3D12_TEXTURE_COPY_LOCATION d = {};
    d.pResource = dst.Get();
    d.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION s = {};
    s.pResource = bake_tex_.Get();
    s.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    // 【踩过】pSrcBox 传 nullptr 不是"拷到哪算哪"，而是"拷**整个**源子资源"，
    // 这张源是 kBakeMax² 的复用目标，目标只有 w×h —— 越界直接把设备摘掉
    // （removed=0x887A0001），报错却出现在后面的 CreateCommittedResource 上。
    // 必须显式给源盒。
    const D3D12_BOX src_box = {0, 0, 0, static_cast<UINT>(w), static_cast<UINT>(h), 1};
    cmd_->CopyTextureRegion(&d, 0, 0, 0, &s, &src_box);
    {
        D3D12_RESOURCE_BARRIER b =
            Transition(bake_tex_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                       D3D12_RESOURCE_STATE_RENDER_TARGET);
        cmd_->ResourceBarrier(1, &b);
        D3D12_RESOURCE_BARRIER b2 =
            Transition(dst.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                       D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        cmd_->ResourceBarrier(1, &b2);
    }
    cmd_->Close();
    ID3D12CommandList* lists[] = {cmd_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    Wait_Queue(queue_.Get(), fence_.Get(), fence_event_, fence_value_);
    trace("绘制提交并等待后");

    const UINT slot = Alloc_Srv_Texture(dst.Get(), DXGI_FORMAT_R8G8B8A8_UNORM);
    if (slot == 0xFFFFFFFFu) {
        return -1;
    }
    GpuSprite sp;
    sp.width = w;
    sp.height = h;
    sp.rgba = true;
    sp.index_texture = dst;
    sp.index_srv = Gpu_Handle(srv_heap_.Get(), slot, srv_size_);
    sprites_.push_back(sp);
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    return static_cast<int>(sprites_.size()) - 1;
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
    if (!device_ || srv_used_ >= srv_capacity_) {
        return -1;
    }
    // 帧录制中途绝不能 Reset 命令分配器，否则本帧 Clear/Draw 全丢，回读全黑。
    if (in_frame_) {
        std::printf("[!] Upload_Texture 在帧内被调用（%dx%d），已拒绝\n", width,
                    height);
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
