// Dx12Renderer.h -- DX12 渲染器
//
// 为什么重写渲染层：原引擎是 DirectDraw 软件 blit（Blitter 家族 108 个派生类），
// 在现代系统上既跑不稳也吃不到 GPU，更与"多核并行"的目标背道而驰。
// 这里保留原素材格式（索引色 + 调色板），把"像素搬到屏幕上"换成 GPU：
//
//   索引纹理（R8, w*h） + 调色板纹理（RGBA8, 256x1） --像素着色器查表--> 屏幕
//
// 好处：调色板换色（阵营色、选中高亮、淡入淡出）只要换 256x1 的纹理，
// 不用碰精灵数据 —— 这正好对上 RA2 那套重映射机制。

#pragma once

#include <cstdint>
#include <vector>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "gfx/Palette.h"
#include "gfx/ShpFile.h"
#include "gfx/VxlFile.h"

namespace ra2 {

using Microsoft::WRL::ComPtr;

/// 一张已上传到 GPU 的精灵。
///
/// 两种形态：
///   * 索引色（rgba=false）：R8 每像素一个调色板索引，像素着色器查 256×1 调色板。
///     SHP/TMP/PCX 这些"画好的图"走这条 —— 换阵营色只要重传调色板。
///   * 真彩（rgba=true）：RGBA8，直接采样。体素走这条 —— 它的明暗是逐体素算好
///     烘进去的（见 gfx/VoxelLight.h），没有"换调色板改色"这回事了。
struct GpuSprite {
    int width = 0;
    int height = 0;
    bool rgba = false;
    ComPtr<ID3D12Resource> index_texture;   ///< R8 或 RGBA8
    D3D12_GPU_DESCRIPTOR_HANDLE index_srv = {};
};

/// 一次体素烘焙的参数。
///
/// 【为什么是"烘焙成一张精灵"而不是每帧画体素】
/// 一辆坦克 8 万体素，屏幕上几百个单位就是上千万体素/帧，直接画会跪。
/// 原版是进图时把每个朝向预渲成一张位图，之后只 blit —— 这里照抄这个
/// 思路，差别只是"预渲"这一步从 CPU 软光栅搬到了 GPU。
struct VoxelBakeParams {
    float yaw = 0.0f;                  ///< 朝向（弧度，绕模型空间 Z 轴）
    float scale = 8.0f;                ///< 一个体素占几个像素
    const float* bbox = nullptr;       ///< VxlGpuGeom::bbox（已含 yaw）
    const float* depth_range = nullptr;///< VxlGpuGeom::depth（画家序深度范围）
    const uint8_t* pal768 = nullptr;   ///< 768 字节 RGB，remap 已经合好
    const float* light = nullptr;      ///< 世界光向量（单位长）。空 = VoxelLight 默认
    float ambient = 0.6f;              ///< exe 的 0.6（见 gfx/VoxelLight.h）
    float diffuse = 0.8f;              ///< exe 的 0.8
    float levels = 16.0f;              ///< exe 的 16 级明暗
};

class Dx12Renderer {
public:
    ~Dx12Renderer();

    /// 创建设备并挂到窗口上。
    bool Init(HWND hwnd, int width, int height);

    /// 离屏模式：不要窗口、不要交换链，直接渲到一张纹理上。
    ///
    /// 存在的理由很实际：无桌面的环境（CI、远程会话）里 swapchain 的 Present
    /// 会把设备摘掉（实测 hr=0x887A0005 DEVICE_REMOVED），于是"画对了没有"
    /// 根本验不了。离屏路径绕开 Present，专门用来做像素级回归。
    bool Init_Offscreen(int width, int height);

    /// 把一帧索引像素传上去。返回句柄下标，-1 表示失败。
    int Upload_Sprite(const uint8_t* indexed_pixels, int width, int height);

    /// 把一帧真彩像素传上去（RGBA8，每行 w*4 字节）。
    ///
    /// 给体素用：明暗是 CPU 端按法线算好烘进像素的（gfx/VoxelLight.h），
    /// 走索引管线没法表达"同一个色号在不同法线下亮度不同"。
    int Upload_Sprite_RGBA(const uint8_t* rgba_pixels, int width, int height);

    /// 设置当前调色板（影响后续所有绘制）。
    void Set_Palette(const Palette& pal);

    /// 上传一份体素几何。返回句柄，-1 表示失败。
    ///
    /// 这里只接收 **VxlFile::Build_GPU_Geom 的产物** —— 也就是"只解码、不投影"
    /// 的体素。投影 / 明暗查表 / 调色板查表 / 画家序全在着色器里做，CPU 不碰。
    ///
    /// 几何与朝向无关，所以**一个模型只传一次**；换朝向只是换一次烘焙的参数，
    /// 8 向 32 向都不用重传。
    int Upload_Voxel_Geom(const uint32_t* voxels /* 2 × count */, int count,
                          const VxlGpuLimb* limbs, int limb_count);

    /// 在 GPU 上把体素光栅化成一张 RGBA 精灵，返回精灵句柄（可直接喂 Draw_Sprite）。
    ///
    /// 必须在 Begin_Frame / End_Frame **之外**调用：它自己开一段命令列表，
    /// 渲到一张临时渲染目标再拷出来。跟帧内的状态机混在一起会打架。
    ///
    /// 【画家序】不用 CPU 排序，改用深度缓冲：每个体素把
    /// `depth = px+py+pz` 归一化后写进深度，GPU 逐像素判胜负。
    /// 可证等价 —— 两个体素屏幕位置相同 <=> 它们在同一条视线 (1,1,1) 上，
    /// 此时 px+py+pz 必然不同（推导：sx,sy 相同 => px-py 与 (px+py)/2-pz 相同，
    /// 再叠加 px+py+pz 相同 => px,py,pz 逐项相同，即同一个体素）。
    /// 而且逐像素比"整块按平均深度排序"更准。
    ///
    /// 【落点】out_w/out_h 之外，精灵左上角在屏幕上的位置应该是
    /// `格心 + (bbox[0]*scale, bbox[1]*scale)`。这样模型原点（投影后恒为 (0,0)）
    /// 正好落在格心 —— 与画布大小无关，所以 bbox 取保守上界也不会让单位浮空。
    int Bake_Voxels(int geom, const VoxelBakeParams& p, int* out_w, int* out_h);

    /// 画一帧。dx/dy 是屏幕坐标，scale 是放大倍数。
    void Draw_Sprite(int sprite, int dx, int dy, float scale = 1.0f);

    /// 纯色填充矩形。界面全部靠它：侧栏底板、按钮、血条、小地图格子。
    ///
    /// 为什么单开一条 PSO：精灵路径是"索引 -> 查调色板 -> 出颜色"，
    /// 界面要的是"直接给一个 RGBA"。用 1×1 白精灵 + 缩放也能凑，
    /// 但那样每次换色都要传一张纹理，白搭一条上传通道。
    /// color 分量 0..1，走的是和精灵同一套 alpha 混合（SrcAlpha/InvSrcAlpha）。
    void Draw_Rect(int x, int y, int w, int h, const float color[4]);

    /// 矩形描边（四条边各一个填充矩形）。框选用它。
    void Draw_Rect_Outline(int x, int y, int w, int h, const float color[4],
                           int thickness = 1);

    /// 清屏 -> 提交所有 Draw_Sprite -> 呈现。
    void Begin_Frame(const float clear_color[4]);
    void End_Frame();

    /// 请求把当前帧的后台缓冲拷回 CPU（在 End_Frame 里执行）。
    /// 这是"画对了没有"的验收手段：渲染成功不等于像素正确，
    /// 必须真的把结果读回来跟参考图比。
    void Request_Capture() { capture_requested_ = true; }
    bool Get_Capture(std::vector<uint8_t>& out_rgba, int& out_w, int& out_h) const;

    void Shutdown();

    bool Is_Ready() const noexcept { return device_ != nullptr; }
    const char* Last_Error() const noexcept { return last_error_; }

    /// 诊断：设备被摘掉的原因（0 = 还活着）。
    ///
    /// 非法调用不会在调用那一行报错，而是等下一次资源创建才冒出来，
    /// 且报的还都是 DEVICE_REMOVED —— 只有这个返回值能定位到"到底哪一步
    /// 把设备弄死了"。所以每做完一件可疑的事就查一次。
    unsigned long Removal_Reason() const {
        return device_ ? static_cast<unsigned long>(device_->GetDeviceRemovedReason())
                       : 0ul;
    }

    /// 把调试层攒下的消息打到 stderr。
    ///
    /// 调试层默认只写 OutputDebugString，命令行下根本看不到；
    /// 而"设备被摘掉"又总是滞后到下一次资源创建才报。
    /// 所以每做完一件可疑的事就调一次，谁犯的错当场就现形。
    void Flush_Debug_Messages();

    /// 一份已上传的体素几何。
    struct GpuVoxelGeom {
        int count = 0;
        ComPtr<ID3D12Resource> voxel_buf;   ///< uint2 × count（打包的体素）
        ComPtr<ID3D12Resource> limb_buf;    ///< float4 × limbs × 4（含 min_bounds）
        D3D12_GPU_DESCRIPTOR_HANDLE voxel_srv = {};
        D3D12_GPU_DESCRIPTOR_HANDLE limb_srv = {};
    };

private:
    /// Upload_Sprite / Upload_Sprite_RGBA 的公共实现。
    int Upload_Texture(const uint8_t* pixels, int width, int height, int bpp,
                       DXGI_FORMAT fmt, bool rgba);

    /// 上传一段缓冲（默认堆 + 上传堆 + 拷贝 + 等 GPU）。给体素几何用。
    ComPtr<ID3D12Resource> Upload_Buffer(const void* data, size_t bytes,
                                         D3D12_RESOURCE_STATES after);
    /// 往一张已建好的 COPY_DEST 纹理里灌数据，再转到 after 状态。
    bool Upload_Texture_Data(ID3D12Resource* dst, const void* data, UINT w, UINT h,
                             DXGI_FORMAT fmt, UINT bpp, D3D12_RESOURCE_STATES after);
    /// 给结构化缓冲建一个 SRV，返回描述符下标；失败返回 0xFFFFFFFF。
    UINT Alloc_Srv_Structured(ID3D12Resource* res, UINT stride, UINT count);
    /// 给贴图建一个 SRV，返回描述符下标。
    UINT Alloc_Srv_Texture(ID3D12Resource* res, DXGI_FORMAT fmt);

    bool Finish_Init();          ///< 两个 Init 共用的后半段
    /// 全局调色板（slot 0）。**必须最先建** —— 见 Finish_Init 里的顺序说明。
    bool Create_Global_Palette();
    bool Create_Device();
    bool Create_SwapChain(HWND hwnd, int width, int height);
    bool Create_Pipeline();
    bool Create_Descriptor_Heaps();
    /// 体素管线：单独的根签名 + PSO（要读结构化缓冲，还得开深度）。
    bool Create_Voxel_Pipeline();
    /// 烘焙用的临时渲染目标 + 深度 + 法线表 / 调色板贴图。
    bool Create_Voxel_Resources();

    void Fail(const char* msg);
    ID3D12Resource* Current_Target() const;

    ComPtr<ID3D12Device> device_;
    ComPtr<IDXGIFactory4> factory_;
    ComPtr<IDXGISwapChain3> swapchain_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<ID3D12CommandAllocator> alloc_;
    ComPtr<ID3D12GraphicsCommandList> cmd_;
    ComPtr<ID3D12DescriptorHeap> rtv_heap_;
    ComPtr<ID3D12DescriptorHeap> srv_heap_;
    ComPtr<ID3D12RootSignature> root_sig_;
    ComPtr<ID3D12PipelineState> pso_;        ///< 索引色 -> 查调色板
    ComPtr<ID3D12PipelineState> pso_rgba_;   ///< 真彩直接采样
    ComPtr<ID3D12PipelineState> pso_solid_;  ///< 纯色（界面图元）

    // ---- 体素管线（投影 / 明暗 / 调色板全在 GPU） ----
    ComPtr<ID3D12RootSignature> root_sig_voxel_;
    ComPtr<ID3D12PipelineState> pso_voxel_;
    ComPtr<ID3D12DescriptorHeap> dsv_heap_;
    ComPtr<ID3D12Resource> bake_tex_;        ///< 烘焙用临时渲染目标（512²）
    ComPtr<ID3D12Resource> bake_depth_;      ///< 画家序用的深度缓冲（512²）
    ComPtr<ID3D12Resource> normals_tex_;     ///< 体素法线表 256×4（RGBA32F）
    ComPtr<ID3D12Resource> voxel_palette_;   ///< 体素调色板 256×1，每烘一张重写一次
    /// 调色板的上传缓冲，**常驻 + 常驻映射**：一次烘焙里 4 次 fence 往返太贵，
    /// 改成"CPU 直接写进常驻映射内存，整条命令列表只提交一次"。
    ComPtr<ID3D12Resource> voxel_palette_up_;
    void* voxel_palette_up_ptr_ = nullptr;
    std::vector<GpuVoxelGeom> voxel_geoms_;
    D3D12_GPU_DESCRIPTOR_HANDLE normals_srv_ = {};
    D3D12_GPU_DESCRIPTOR_HANDLE voxel_palette_srv_ = {};
    UINT dsv_size_ = 0;
    ComPtr<ID3D12Resource> backbuffers_[2];
    ComPtr<ID3D12Fence> fence_;
    HANDLE fence_event_ = nullptr;
    UINT64 fence_value_ = 0;

    ComPtr<ID3D12Resource> palette_tex_;
    ComPtr<ID3D12Resource> palette_upload_;
    ComPtr<ID3D12Resource> vertex_buffer_;

    std::vector<GpuSprite> sprites_;
    std::vector<ComPtr<ID3D12Resource>> upload_buffers_;  ///< 撑到上传结束

    UINT rtv_size_ = 0;
    UINT srv_size_ = 0;
    UINT srv_used_ = 0;
    UINT srv_capacity_ = 0;
    UINT frame_index_ = 0;
    int vp_width_ = 0;
    int vp_height_ = 0;
    bool headless_ = false;
    ComPtr<ID3D12Resource> rt_texture_;   ///< 离屏模式下的渲染目标
    bool in_frame_ = false;

    bool capture_requested_ = false;
    std::vector<uint8_t> capture_;
    int capture_w_ = 0;
    int capture_h_ = 0;

    char last_error_[256] = {};
};

}  // namespace ra2
