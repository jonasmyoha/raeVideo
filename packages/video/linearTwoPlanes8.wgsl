// linearCommon.wgsl's planes for 8-bit frames in two planes (NV12): r8unorm Y,
// rg8unorm CbCr
@group(0) @binding(2) var lumaPlane: texture_2d<f32>;
@group(0) @binding(3) var chromaPlane: texture_2d<f32>;
@group(0) @binding(4) var picture: texture_storage_2d<rgba16float, write>;

fn lumaCode(at: vec2<i32>) -> f32 {
  return textureLoad(lumaPlane, at, 0).r * 255.0;
}

fn chromaCode(at: vec2<i32>) -> vec2<f32> {
  return textureLoad(chromaPlane, at, 0).rg * 255.0;
}
