// linearCommon.wgsl's planes for 8-bit frames in three planes (a registered
// backend's 4:2:0, 4:2:2, 4:4:4): r8unorm Y, Cb, Cr
@group(0) @binding(2) var lumaPlane: texture_2d<f32>;
@group(0) @binding(3) var blueDifferencePlane: texture_2d<f32>;
@group(0) @binding(4) var redDifferencePlane: texture_2d<f32>;
@group(0) @binding(5) var picture: texture_storage_2d<rgba16float, write>;

fn lumaCode(at: vec2<i32>) -> f32 {
  return textureLoad(lumaPlane, at, 0).r * 255.0;
}

fn chromaCode(at: vec2<i32>) -> vec2<f32> {
  return vec2<f32>(textureLoad(blueDifferencePlane, at, 0).r, textureLoad(redDifferencePlane, at, 0).r) * 255.0;
}
