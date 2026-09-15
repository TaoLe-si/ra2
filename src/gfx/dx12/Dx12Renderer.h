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

private:
    /// Upload_Sprite / Upload_Sprite_RGBA 的公共实现。
    int Upload_Texture(const uint8_t* pixels, int width, int height, int bpp,
                       DXGI_FORMAT fmt, bool rgba);

    bool Finish_Init();          ///< 两个 Init 共用的后半段
    bool Create_Device();
    bool Create_SwapChain(HWND hwnd, int width, int height);
    bool Create_Pipeline();
    bool Create_Descriptor_Heaps();

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
