#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
namespace nx::nvn {
namespace upload_bounds {
inline bool multiply(uint64_t a,uint64_t b,uint64_t& result) {
    if(b && a>std::numeric_limits<uint64_t>::max()/b) return false;
    result=a*b;return true;
}
inline bool add(uint64_t a,uint64_t b,uint64_t& result) {
    if(b>std::numeric_limits<uint64_t>::max()-a) return false;
    result=a+b;return true;
}
inline uint64_t blocks(uint32_t texels,uint32_t block) { return uint64_t(texels)/block+(texels%block!=0); }
}
struct TextureCopyShape {
    uint64_t offset;
    uint32_t width,height,depth,layers,row_length,image_height,block_width,block_height,bytes_per_block;
};
inline std::optional<uint64_t> texture_copy_end(const TextureCopyShape& s,uint64_t base) {
    if(s.offset<base || !s.width || !s.height || !s.depth || !s.layers || !s.block_width || !s.block_height ||
       !s.bytes_per_block || (s.row_length && s.row_length<s.width) || (s.image_height && s.image_height<s.height)) return {};
    const uint64_t row=upload_bounds::blocks(s.row_length ? s.row_length : s.width,s.block_width);
    const uint64_t image_rows=upload_bounds::blocks(s.image_height ? s.image_height : s.height,s.block_height);
    const uint64_t width=upload_bounds::blocks(s.width,s.block_width),height=upload_bounds::blocks(s.height,s.block_height);
    uint64_t count=uint64_t(s.depth)*s.layers-1;
    if(!upload_bounds::multiply(count,image_rows,count) || !upload_bounds::add(count,height-1,count) ||
       !upload_bounds::multiply(count,row,count) || !upload_bounds::add(count,width,count) ||
       !upload_bounds::multiply(count,s.bytes_per_block,count) || !upload_bounds::add(s.offset-base,count,count)) return {};
    return count;
}
inline std::optional<uint64_t> astc_level_bytes(uint32_t width,uint32_t height,uint32_t layers,uint32_t bw,uint32_t bh,uint64_t stored_layer) {
    if(!width || !height || !layers || !bw || !bh) return {};
    uint64_t need=upload_bounds::blocks(width,bw),stored=0;
    if(!upload_bounds::multiply(need,upload_bounds::blocks(height,bh),need) ||
       !upload_bounds::multiply(need,16,need) || !upload_bounds::multiply(need,layers,need) ||
       !upload_bounds::multiply(stored_layer,layers,stored)) return {};
    return std::max(need,stored);
}
inline bool texture_span_fits(uint64_t offset,uint64_t need,uint64_t available) {
    return offset<=available && need<=available-offset;
}
}
