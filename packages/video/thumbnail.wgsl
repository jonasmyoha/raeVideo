// A picture scaled down to a thumbnail (video/Thumbnails), one invocation per
// thumbnail pixel: the mean of the source pixels it covers (a box filter, at
// most 32 x 32 of them, evenly spread over a larger footprint), so a scaled
// frame does not shimmer. Composed after lib/gpu/computeBounds.wgsl (binding 0).

struct ThumbnailParameters {
  sourceWidth: u32,
  sourceHeight: u32,
  width: u32,
  height: u32,
};

@group(0) @binding(1) var<uniform> sizes: ThumbnailParameters;
@group(0) @binding(2) var source: texture_2d<f32>;
@group(0) @binding(3) var thumbnail: texture_storage_2d<rgba8unorm, write>;

@compute @workgroup_size(computeWorkgroupSize)
fn main(@builtin(global_invocation_id) globalId: vec3<u32>) {
  if (!computeElementInRange(globalId)) {
    return;
  }
  let index = computeElementIndex(globalId);
  let x = index % sizes.width;
  let y = index / sizes.width;
  let left = x * sizes.sourceWidth / sizes.width;
  let right = max(left + 1u, (x + 1u) * sizes.sourceWidth / sizes.width);
  let top = y * sizes.sourceHeight / sizes.height;
  let bottom = max(top + 1u, (y + 1u) * sizes.sourceHeight / sizes.height);
  let stepX = max(1u, (right - left) / 32u);
  let stepY = max(1u, (bottom - top) / 32u);
  var sum = vec4<f32>(0.0);
  var count = 0.0;
  for (var sourceY = top; sourceY < bottom; sourceY = sourceY + stepY) {
    for (var sourceX = left; sourceX < right; sourceX = sourceX + stepX) {
      sum = sum + textureLoad(source, vec2<i32>(i32(sourceX), i32(sourceY)), 0);
      count = count + 1.0;
    }
  }
  textureStore(thumbnail, vec2<i32>(i32(x), i32(y)), sum / count);
}
