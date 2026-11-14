// A picture (an RGBA texture) packed for an encoder (video/VideoEncoder): one
// 32-bit word per pixel, bytes B G R A in memory (little-endian), rows packed,
// the 32-bit BGRA frames VideoToolbox takes. Composed after
// lib/gpu/computeBounds.wgsl (binding 0); the buffer is read back.

struct PackSizes {
  width: u32,
  height: u32,
  reserved0: u32,
  reserved1: u32,
};

@group(0) @binding(1) var<uniform> sizes: PackSizes;
@group(0) @binding(2) var<storage, read_write> packed: array<u32>;
@group(0) @binding(3) var picture: texture_2d<f32>;

@compute @workgroup_size(computeWorkgroupSize)
fn main(@builtin(global_invocation_id) globalId: vec3<u32>) {
  if (!computeElementInRange(globalId)) {
    return;
  }
  let index = computeElementIndex(globalId);
  let x = index % sizes.width;
  let y = index / sizes.width;
  let color = textureLoad(picture, vec2<i32>(i32(x), i32(y)), 0);
  packed[index] = pack4x8unorm(vec4<f32>(color.b, color.g, color.r, color.a));
}
