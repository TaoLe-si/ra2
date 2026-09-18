#include <cstdint>
#include <cstddef>
#include <cstdio>

using u8 = uint8_t;
using u32 = uint32_t;

#pragma pack(push, 1)
struct A { u32 v; u8 x[0x1D]; };                          // 0x00 .. 0x21
struct B : A { u32 w[3]; u8 y[0x38]; };                   // 0x21 .. 0x65
struct C : B { u8 z[0x22B]; u32 q; };                     // 0x65 .. 0x294
struct D : C { u32 a[4]; u8 s[0xB4C]; u8 n[4]; };         // 0x294 .. 0xDF4
#pragma pack(pop)

int main() {
    std::printf("sizeof A=0x%zX B=0x%zX C=0x%zX D=0x%zX\n",
                sizeof(A), sizeof(B), sizeof(C), sizeof(D));
    std::printf("offsetof(C,q)=0x%zX  offsetof(D,q)=0x%zX  offsetof(D,n)=0x%zX\n",
                offsetof(C, q), offsetof(D, q), offsetof(D, n));
    static_assert(offsetof(C, q) == 0x290, "own field");
    static_assert(offsetof(D, q) == 0x290, "inherited field through chain");
    static_assert(sizeof(C) == 0x294, "boundary");
    static_assert(offsetof(D, s) == 0x2A4, "derived body start");
    static_assert(sizeof(D) == 0xDF4, "derived end");
    return 0;
}
