// Y'CbCr to linear RGB in the source's gamut (docs/design.md §7, §8.1: the
// input side of lib/color, here until lib/color is written; its curves are to
// move there). One invocation per pixel; composed after
// lib/gpu/computeBounds.wgsl (binding 0) and before one of linearTwoPlanes8.wgsl,
// linearTwoPlanes16.wgsl, linearThreePlanes8.wgsl, which bind the planes and
// say how to read a sample's code (lumaCode, chromaCode).
//
// Codes are in their own units (0-255 for 8-bit, 0-1023 for 10-bit), so one
// set of parameters serves every depth:  Y' = (luma - lumaOffset) / lumaRange,
// Cb = (cb - chromaOffset) / chromaRange (limited range: 16, 219, 128, 224 at
// 8 bits, 64, 876, 512, 896 at 10). Chroma is upsampled bilinearly from where
// its samples are: co-sited with the even luma columns, between the rows of a
// pair (MPEG-2 4:2:0 siting; 4:2:2 has no vertical offset). The transfer to
// linear (`transfer`): 1 the inverse of the BT.709 / BT.2020 OETF (scene light,
// 0 to 1), 2 PQ (SMPTE ST 2084) as cd/m2 / 100 (1.0 = 100 cd/m2), 3 the inverse
// of the HLG OETF (BT.2100, scene light 0 to 1), 4 sRGB, 5 none (already
// linear). R'G'B' is clamped to 0..1 first. The output is rgba16float.

struct LinearParameters {
  lumaOffset: f32,
  lumaRange: f32,
  chromaOffset: f32,
  chromaRange: f32,
  crToRed: f32,
  cbToGreen: f32,
  crToGreen: f32,
  cbToBlue: f32,
  width: u32,
  height: u32,
  chromaWidthDivisor: u32,
  chromaHeightDivisor: u32,
  chromaWidth: u32,
  chromaHeight: u32,
  transfer: u32,
  codeShift: u32,
};

@group(0) @binding(1) var<uniform> yuv: LinearParameters;

fn bt709ToLinear(value: f32) -> f32 {
  if (value < 0.081) {
    return value / 4.5;
  }
  return pow((value + 0.099) / 1.099, 1.0 / 0.45);
}

fn pqToLinear(value: f32) -> f32 {
  let m1 = 0.1593017578125;
  let m2 = 78.84375;
  let c1 = 0.8359375;
  let c2 = 18.8515625;
  let c3 = 18.6875;
  let powered = pow(value, 1.0 / m2);
  let nits = 10000.0 * pow(max(powered - c1, 0.0) / (c2 - c3 * powered), 1.0 / m1);
  return nits / 100.0;
}

fn hlgToLinear(value: f32) -> f32 {
  let a = 0.17883277;
  let b = 0.28466892;
  let c = 0.55991073;
  if (value <= 0.5) {
    return value * value / 3.0;
  }
  return (exp((value - c) / a) + b) / 12.0;
}

fn srgbToLinear(value: f32) -> f32 {
  if (value <= 0.04045) {
    return value / 12.92;
  }
  return pow((value + 0.055) / 1.055, 2.4);
}

fn toLinear(value: f32) -> f32 {
  switch yuv.transfer {
    case 2u: {
      return pqToLinear(value);
    }
    case 3u: {
      return hlgToLinear(value);
    }
    case 4u: {
      return srgbToLinear(value);
    }
    case 5u: {
      return value;
    }
    default: {
      return bt709ToLinear(value);
    }
  }
}

// The chroma codes at luma pixel (x, y), bilinear between the chroma samples
fn chromaAt(x: u32, y: u32) -> vec2<f32> {
  let lastX = f32(yuv.chromaWidth - 1u);
  let lastY = f32(yuv.chromaHeight - 1u);
  let fx = clamp(f32(x) / f32(yuv.chromaWidthDivisor), 0.0, lastX);
  let fy = clamp((f32(y) + 0.5) / f32(yuv.chromaHeightDivisor) - 0.5, 0.0, lastY);
  let x0 = floor(fx);
  let y0 = floor(fy);
  let x1 = min(x0 + 1.0, lastX);
  let y1 = min(y0 + 1.0, lastY);
  let wx = fx - x0;
  let wy = fy - y0;
  let top = mix(chromaCode(vec2<i32>(i32(x0), i32(y0))), chromaCode(vec2<i32>(i32(x1), i32(y0))), wx);
  let bottom = mix(chromaCode(vec2<i32>(i32(x0), i32(y1))), chromaCode(vec2<i32>(i32(x1), i32(y1))), wx);
  return mix(top, bottom, wy);
}

@compute @workgroup_size(computeWorkgroupSize)
fn main(@builtin(global_invocation_id) globalId: vec3<u32>) {
  if (!computeElementInRange(globalId)) {
    return;
  }
  let index = computeElementIndex(globalId);
  let x = index % yuv.width;
  let y = index / yuv.width;
  let luma = (lumaCode(vec2<i32>(i32(x), i32(y))) - yuv.lumaOffset) / yuv.lumaRange;
  let chroma = (chromaAt(x, y) - vec2<f32>(yuv.chromaOffset)) / yuv.chromaRange;
  let rgb = clamp(
    vec3<f32>(
      luma + yuv.crToRed * chroma.y,
      luma - yuv.cbToGreen * chroma.x - yuv.crToGreen * chroma.y,
      luma + yuv.cbToBlue * chroma.x
    ),
    vec3<f32>(0.0),
    vec3<f32>(1.0)
  );
  let linear = vec3<f32>(toLinear(rgb.r), toLinear(rgb.g), toLinear(rgb.b));
  textureStore(picture, vec2<i32>(i32(x), i32(y)), vec4<f32>(linear, 1.0));
}
