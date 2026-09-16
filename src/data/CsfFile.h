// CsfFile.h — ra2.csf / ra2md.csf 字符串表（gamemd 加载器 @0x00734770）。
//
// 证据（非社区文档）：
//   magic cmp 0x43534620 (' FSC')；头长 0x18 @0x734794
//   标签 cmp 0x4C424C20 (' LBL') @0x734A0A
//   串值 cmp 0x53545220 (' RTS') / 0x53545257 @0x734ADE
//   宽字符按字节 not 解码 @0x734B35

#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace ra2 {

class CsfFile {
public:
    bool Load(const uint8_t* data, size_t size);
    /// 查标签（大小写不敏感）；找不到返回空串。
    std::string Get(const char* label) const;

private:
    std::unordered_map<std::string, std::string> entries_;
};

}  // namespace ra2
