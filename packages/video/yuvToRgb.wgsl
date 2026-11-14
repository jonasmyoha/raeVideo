// Y'CbCr (NV12: a Y plane, and Cb Cr interleaved at half width and height) to
// R'G'B' (docs/design.md §7), one invocation per pixel. Composed after
// lib/gpu/computeBounds.wgsl (binding 0); video/YuvPicture writes the
// parameters from the frame's tags: the range (limited or full) as an offset
// and scale for luma and chroma, the matrix as the four coefficients of
//   R' = Y' + crToRed Cr
//   G' = Y' - cbToGreen Cb - crToGreen Cr
//   B' = Y' + cbToBlue Cb
// The result stays in the stream's transfer (display-referred R'G'B'); the
// step to linear light and the output transform are lib/color's (docs/design.md
// §8.2). Chroma is taken from the nearest sample (each 2x2 block shares one);
// filtered upsampling with the chroma siting comes with the color work.

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
  reserved0: u32,
  reserved1: u32,
};

@group(0) @binding(1) var<uniform> yuv: YuvParameters;
@group(0) @binding(2) var lumaPlane: texture_2d<f32>;
@group(0) @binding(3) var chromaPlane: texture_2d<f32>;
@group(0) @binding(4) var picture: texture_storage_2d<rgba8unorm, write>;

@compute @workgroup_size(computeWorkgroupSize)
fn main(@builtin(global_invocation_id) globalId: vec3<u32>) {
  if (!computeElementInRange(globalId)) {
    return;
  }
  let index = computeElementIndex(globalId);
  let x = index % yuv.width;
  let y = index / yuv.width;
  let luma = textureLoad(lumaPlane, vec2<i32>(i32(x), i32(y)), 0).r;
  let chroma = textureLoad(chromaPlane, vec2<i32>(i32(x / 2u), i32(y / 2u)), 0).rg;
  let lumaValue = (luma - yuv.lumaOffset) * yuv.lumaScale;
  let cb = (chroma.r - yuv.chromaOffset) * yuv.chromaScale;
  let cr = (chroma.g - yuv.chromaOffset) * yuv.chromaScale;
  let rgb = vec3<f32>(
    lumaValue + yuv.crToRed * cr,
    lumaValue - yuv.cbToGreen * cb - yuv.crToGreen * cr,
    lumaValue + yuv.cbToBlue * cb
  );
  textureStore(picture, vec2<i32>(i32(x), i32(y)), vec4<f32>(clamp(rgb, vec3<f32>(0.0), vec3<f32>(1.0)), 1.0));
}
