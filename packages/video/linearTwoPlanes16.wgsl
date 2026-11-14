// linearCommon.wgsl's planes for frames deeper than 8 bits in two planes
// (VideoToolbox's 'x420' / 'x422', P010-like): r16uint Y, rg16uint CbCr, each
// sample's code in its top bits (shifted down by `codeShift`, 6 for 10-bit).
// Integer textures: the code is read exactly.
@group(0) @binding(2) var lumaPlane: texture_2d<u32>;
@group(0) @binding(3) var chromaPlane: texture_2d<u32>;
@group(0) @binding(4) var picture: texture_storage_2d<rgba16float, write>;

fn lumaCode(at: vec2<i32>) -> f32 {
  return f32(textureLoad(lumaPlane, at, 0).r >> yuv.codeShift);
}

fn chromaCode(at: vec2<i32>) -> vec2<f32> {
  let sample = textureLoad(chromaPlane, at, 0).rg >> vec2<u32>(yuv.codeShift);
  return vec2<f32>(f32(sample.x), f32(sample.y));
}
