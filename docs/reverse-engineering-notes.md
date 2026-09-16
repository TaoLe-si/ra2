# 严格逆向笔记

每条都标 **RE**（reverse-engineered）+ **地址** + **落点**。
不发明：只记逆向真证过的事实，不写"应该这样"。

## TAction 113（Crowd Cheer）— @0x50C8C0

**RE**：从 binary 反汇编拿到的实现路径：

```
0x0050C8C0  push ebx
0x0050C8C1  push esi
0x0050C8C2  push edi
0x0050C8C3  mov edi, *(int*)0xA8EC88         ; TechnoClass 数组容量
0x0050C8C9  xor esi, esi
0x0050C8CB  mov ebx, ecx                      ; ebx = house
0x0050C8CD  test edi, edi
0x0050C8CF  jle  0x50C8F4                    ; 空直接跳末尾
0x0050C8D1  mov eax, *(int*)0xA8EC7C         ; TechnoClass 数组首址
0x0050C8D6  mov ecx, *(eax + esi*4)          ; ecx = t = array[i]
0x0050C8D9  test ecx, ecx
0x0050C8DB  je   0x50C8EF                    ; 空槽跳
0x0050C8DD  cmp  *(int*)(ecx + 0x21C), ebx   ; t->house(+0x21C) ?
0x0050C8E3  jne  0x50C8EF                    ; 不是这房跳
0x0050C8E5  mov edx, *(int*)ecx              ; edx = vtable
0x0050C8E7  push 0                            ; arg=0（开/关标志）
0x0050C8E9  call dword ptr [edx + 0x388]      ; TechnoClass::Crowd_Cheer(0)
0x0050C8EF  inc esi
0x0050C8F0  cmp esi, edi
0x0050C8F2  jl  0x50C8D1
0x0050C8F4  mov eax, *(int*)0x8871E0         ; VocClass 数组
0x0050C8F9  push 0
0x0050C8FB  mov edx, 0x2000                   ; priority
0x0050C900  push 0x3F800000                   ; 1.0f volume
0x0050C8F5  mov ecx, *(eax + 0x1C8)          ; Voc 名
0x0050C90B  call 0x750920                     ; Play_Voc(voc, 1.0f, 0, 0x2000)
0x0050C910  pop edi
0x0050C911  pop esi
0x0050C912  pop ebx
0x0050C913  ret
```

**落点**（C++ 侧）：
- `src/game/World.cpp:All_To_Hunt` 模式 — `Crowd_Cheer` 用 `for (Object& o : objects_)` 扫 `house + Techno`；
- `Is_Techno()` 决定 + 跳过 Building（建筑原版也不动）；
- 严格对齐扫描集合，**不发明** vtable[226] 的动作实现；
- 末尾仍走 Sound stub（音频设备未接）。

**验证**：
- 自检加 `TAction 113 触发一次 sound_play`；
- 输出扫描数（9 个 Techno 在 player house），与原版扫描集合对齐。

## GAME.FNT — Westwood FNT 格式（粗探）

**RE**：从 binary 反汇编：
- `0x00434AD0` = BitFont 构造函数（`push 0x44; call new`），vtable=`0x7e3a80`；
- `0x00434AE7` push `0x818B98`（"GAME.FNT"），call `0x433880`（file loader）；
- `0x00434AF3` 写到 `0x89c4d0`（BitText singleton）；
- `0x00433895` vtable=`0x7e3a78`（BitFont vtable），写到 `esi+0..0x44` 多个字段；
- `0x004338C6` call `[0x7e14b4]`（文件打开）；
- `0x004338D3` call `0x433990`（FNT header parser）；
- `0x004338ED` call `0x434700`（glyph load）。

**FNT 头**（game.fnt 1792690 字节）：
```
66 6F 6E 54  "fonT"
14 00 00 00   header size = 0x14 (20)
03 00 00 00   block count = 3
10 00 00 00   block[0] type = 0x10
11 00 00 00   block[0] field1 = 0x11
76 84 00 00   block[0] data = 0x8476
31 00 00 00   block[1] type = 0x31
```

**未完成**：CJK glyph 渲染管线、读完整 block 解析。
CJK win/lose banner 仍走 FULLFNT3 英文 CSF。