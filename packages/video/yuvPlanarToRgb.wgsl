// Y'CbCr in three planes (Y, Cb, Cr; chroma at 4:2:0, 4:2:2 or 4:4:4) to
// R'G'B', one invocation per pixel: yuvToRgb.wgsl's conversion for the frames a
// registered backend gives (packages/ffmpeg). Composed after
// lib/gpu/computeBounds.wgsl (binding 0); video/YuvPicture writes the same
// parameters, with the chroma planes' divisors (1 or 2) in the last two words.
// Chroma is taken from the nearest sample.

struct YuvParameters {
  lumaOffset: f32,
  lumaScale: f32,
  chromaOffset: f32,
  chromaScale: f32,
  crToRed: f32,
  cbToGreen: f32,
  crToGreen: f32,
  cbToBlue: f32,
  width: u32,
  height: u32,
  chromaWidthDivisor: u32,
  chromaHeightDivisor: u32,
};

@group(0) @binding(1) var<uniform> yuv: YuvParameters;
@group(0) @binding(2) var lumaPlane: texture_2d<f32>;
@group(0) @binding(3) var blueDifferencePlane: texture_2d<f32>;
@group(0) @binding(4) var redDifferencePlane: texture_2d<f32>;
@group(0) @binding(5) var picture: texture_storage_2d<rgba8unorm, write>;

@compute @workgroup_size(computeWorkgroupSize)
fn main(@builtin(global_invocation_id) globalId: vec3<u32>) {
  if (!computeElementInRange(globalId)) {
    return;
  }
  let index = computeElementIndex(globalId);
  let x = index % yuv.width;
  let y = index / yuv.width;
  let chromaAt = vec2<i32>(i32(x / yuv.chromaWidthDivisor), i32(y / yuv.chromaHeightDivisor));
  let luma = textureLoad(lumaPlane, vec2<i32>(i32(x), i32(y)), 0).r;
  let lumaValue = (luma - yuv.lumaOffset) * yuv.lumaScale;
  let cb = (textureLoad(blueDifferencePlane, chromaAt, 0).r - yuv.chromaOffset) * yuv.chromaScale;
  let cr = (textureLoad(redDifferencePlane, chromaAt, 0).r - yuv.chromaOffset) * yuv.chromaScale;
  let rgb = vec3<f32>(
    lumaValue + yuv.crToRed * cr,
    lumaValue - yuv.cbToGreen * cb - yuv.crToGreen * cr,
    lumaValue + yuv.cbToBlue * cb
  );
  textureStore(picture, vec2<i32>(i32(x), i32(y)), vec4<f32>(clamp(rgb, vec3<f32>(0.0), vec3<f32>(1.0)), 1.0));
}
