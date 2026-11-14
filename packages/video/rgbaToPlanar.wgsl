// A picture (an RGBA texture, R'G'B') converted for an encoder that takes 8-bit
// planar Y'CbCr (a registered encoder: packages/ffmpeg's DNxHR and FFV1): BT.709,
// limited range, the planes packed one after another (Y width x height, then
// Cb, then Cr at chromaWidth x chromaHeight). Chroma is the mean of the pixels
// it covers (2 x 1 for 4:2:2, 2 x 2 for 4:2:0). One invocation writes one
// 32-bit word: four bytes of the planes, little-endian, so any size works and
// no byte is written twice. Composed after lib/gpu/computeBounds.wgsl.

struct PlanarSizes {
  width: u32,
  height: u32,
  chromaWidth: u32,
  chromaHeight: u32,
  chromaRows: u32,
  reserved0: u32,
  reserved1: u32,
  reserved2: u32,
};

@group(0) @binding(1) var<uniform> sizes: PlanarSizes;
@group(0) @binding(2) var<storage, read_write> planes: array<u32>;
@group(0) @binding(3) var picture: texture_2d<f32>;

fn rgbAt(x: u32, y: u32) -> vec3<f32> {
  let at = vec2<i32>(i32(min(x, sizes.width - 1u)), i32(min(y, sizes.height - 1u)));
  return textureLoad(picture, at, 0).rgb;
}

fn lumaOf(rgb: vec3<f32>) -> f32 {
  return 0.2126 * rgb.r + 0.7152 * rgb.g + 0.0722 * rgb.b;
}

// The code of byte `index` of the packed planes
fn codeAt(index: u32) -> f32 {
  let lumaBytes = sizes.width * sizes.height;
  if (index < lumaBytes) {
    return 16.0 + 219.0 * lumaOf(rgbAt(index % sizes.width, index / sizes.width));
  }
  let chromaBytes = sizes.chromaWidth * sizes.chromaHeight;
  let offset = index - lumaBytes;
  let plane = offset / chromaBytes;
  if (plane > 1u) {
    return 0.0;
  }
  let at = offset % chromaBytes;
  let chromaX = at % sizes.chromaWidth;
  let chromaY = at / sizes.chromaWidth;
  var sum = vec3<f32>(0.0);
  for (var row = 0u; row < sizes.chromaRows; row = row + 1u) {
    sum = sum + rgbAt(2u * chromaX, chromaY * sizes.chromaRows + row);
    sum = sum + rgbAt(2u * chromaX + 1u, chromaY * sizes.chromaRows + row);
  }
  let rgb = sum / f32(2u * sizes.chromaRows);
  let luma = lumaOf(rgb);
  if (plane == 0u) {
    return 128.0 + 224.0 * (rgb.b - luma) / 1.8556;
  }
  return 128.0 + 224.0 * (rgb.r - luma) / 1.5748;
}

@compute @workgroup_size(computeWorkgroupSize)
fn main(@builtin(global_invocation_id) globalId: vec3<u32>) {
  if (!computeElementInRange(globalId)) {
    return;
  }
  let word = computeElementIndex(globalId);
  var packed = 0u;
  for (var k = 0u; k < 4u; k = k + 1u) {
    let code = u32(clamp(round(codeAt(word * 4u + k)), 0.0, 255.0));
    packed = packed | (code << (8u * k));
  }
  planes[word] = packed;
}
