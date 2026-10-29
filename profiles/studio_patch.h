#pragma once
#include <cstdint>
namespace StudioPatch {
inline constexpr char kVersion[] = "0.744.0.7441077";
inline constexpr char kOriginalSha256[] = "81be5bc2916ba979e0ecc68b5fd039a48821765c6e9a7bcfd8d27ad877cf1692";
inline constexpr char kPatchedSha256[] = "64519625110c52312c3763b1c30d209cb342613c4909d764395317372da99fa4";
inline constexpr std::uint64_t kSize = 227540944;
inline constexpr std::uint32_t kRva = 0x0316a870;
inline constexpr std::uint32_t kOffset = 0x03169c70;
inline constexpr std::uint32_t kTopOffset = 0x70;
inline constexpr char kBeforeHex[] = "83fb017537413b76087f3185f67e2133d2448bc6498b4e18488d5210488b47700f104411f00f114410f04983e80175e48bc648c1e00448014770eb2a";
inline constexpr char kAfterHex[] = "8d43ff413b46087333418b4e082bc83bf1772948c1e004490346188bce488bf08bc103c94c8bd7488b7f70f348a549897a70eb349090909090909090";
}
