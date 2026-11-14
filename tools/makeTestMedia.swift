// makeTestMedia.swift — makes raeVideo's test media from simple math, through
// Apple's own encoders (AVAssetWriter: VideoToolbox and AudioToolbox). Run by
// tests/media/make.sh; see that script for the files and why each exists.
//
//   swift tools/makeTestMedia.swift <out> <codec> <width> <height> <frames>
//     <timescale> <frameDuration> <matrix> <range> [audio]
//
//   codec:  h264 | hevc | prores, or hevc10 / prores422hq: 10-bit (below),
//           or raw422: the frames as raw planar 4:2:2
//           Y'CbCr (Y, then Cb and Cr at half width), for an encoder this
//           platform does not have (tests/media/make.sh: DNxHR)
//   matrix: 709 | 601     range: limited | full
//   audio:  "tone" adds a rising tone at -12 dBFS, 48 kHz stereo AAC: from
//           500 Hz at the start to 1500 Hz at the end, so no two places in
//           the file sound the same (a player's sync can be checked to the
//           sample: tests/player)
//
// Every frame n is drawn the same way, in four bands from the top:
//   1. rows [0, h/2): seven 75% color bars (white, yellow, cyan, green,
//      magenta, red, blue), each w/7 wide;
//   2. rows [h/2, 3h/4): gray (RGB 64) with a white square, side h/8, whose
//      left edge is at x = (4 n) mod (w - side);
//   3. rows [3h/4, 7h/8): the frame counter as 16 cells, each w/16 wide, most
//      significant bit first: white (RGB 255) for 1, black for 0;
//   4. rows [7h/8, h): the counter as five seven-segment digits, white on black.
//
// hevc10 (HEVC Main10, 4:2:0) and prores422hq (ProRes 422 HQ, 4:2:2) are
// 10-bit: their frames are written as 10-bit Y'CbCr ('x420' / 'x422', or the
// full-range 'xf20' / 'xf22'), and band 2 is a ramp instead of the square: from
// x = 128, each column one 10-bit luma code more: 0 to 1023 over 1,024 columns
// in a full-range file, 64 to 940 (877 codes, all a limited-range file has) in
// a limited-range one; neutral chroma, the rest of the band mid-gray. The width should be at least 1,152.
//
// The YUV is computed here, not by the encoder: R'G'B' to Y'CbCr with the
// stated matrix (BT.709 or BT.601) and range (limited 16-235 / 16-240, or full
// 0-255), 4:2:0 chroma as the mean of each 2x2 block. The file is tagged with
// the same matrix and range, and BT.709 primaries and transfer. So a reader
// that honors the tags gets the bars' RGB back within the codec's error.
import AVFoundation
import VideoToolbox
import CoreMedia
import CoreVideo
import Foundation

let arguments = CommandLine.arguments
guard arguments.count >= 10 else {
  FileHandle.standardError.write(
    "usage: makeTestMedia <out> <codec> <width> <height> <frames> <timescale> <frameDuration> <matrix> <range> [tone]\n"
      .data(using: .utf8)!)
  exit(2)
}
let outPath = arguments[1]
let codecName = arguments[2]
let width = Int(arguments[3])!
let height = Int(arguments[4])!
let frameCount = Int(arguments[5])!
let timescale = Int32(arguments[6])!
let frameDuration = Int64(arguments[7])!
let matrixName = arguments[8]
let fullRange = arguments[9] == "full"
let withTone = arguments.count > 10 && arguments[10] == "tone"

let (kr, kb): (Double, Double) = matrixName == "601" ? (0.299, 0.114) : (0.2126, 0.0722)
let kg = 1.0 - kr - kb

// R'G'B' in 0...255 to Y'CbCr code values (limited or full range)
func ycbcr(_ r: Double, _ g: Double, _ b: Double) -> (Double, Double, Double) {
  let rn = r / 255.0
  let gn = g / 255.0
  let bn = b / 255.0
  let luma = kr * rn + kg * gn + kb * bn
  let cb = (bn - luma) / (2.0 * (1.0 - kb))
  let cr = (rn - luma) / (2.0 * (1.0 - kr))
  if fullRange {
    return (255.0 * luma, 128.0 + 255.0 * cb, 128.0 + 255.0 * cr)
  }
  return (16.0 + 219.0 * luma, 128.0 + 224.0 * cb, 128.0 + 224.0 * cr)
}

let bars: [(Double, Double, Double)] = [
  (191, 191, 191), (191, 191, 0), (0, 191, 191), (0, 191, 0), (191, 0, 191), (191, 0, 0),
  (0, 0, 191),
]

// Seven-segment digits: segments a b c d e f g
let segments: [[Bool]] = [
  [true, true, true, true, true, true, false], [false, true, true, false, false, false, false],
  [true, true, false, true, true, false, true], [true, true, true, true, false, false, true],
  [false, true, true, false, false, true, true], [true, false, true, true, false, true, true],
  [true, false, true, true, true, true, true], [true, true, true, false, false, false, false],
  [true, true, true, true, true, true, true], [true, true, true, true, false, true, true],
]

func digitLit(_ digit: Int, _ x: Int, _ y: Int, _ cellWidth: Int, _ cellHeight: Int) -> Bool {
  // a digit drawn in a cellWidth x cellHeight box with a stroke of 1/5 the width
  let stroke = max(1, cellWidth / 5)
  let middle = cellHeight / 2
  let lit = segments[digit]
  let left = x < stroke
  let right = x >= cellWidth - stroke
  let top = y < stroke
  let bottom = y >= cellHeight - stroke
  let center = abs(y - middle) < (stroke + 1) / 2 + 0
  let upper = y < middle
  if lit[0] && top { return true }
  if lit[1] && right && upper { return true }
  if lit[2] && right && !upper { return true }
  if lit[3] && bottom { return true }
  if lit[4] && left && !upper { return true }
  if lit[5] && left && upper { return true }
  if lit[6] && center { return true }
  return false
}

// The R'G'B' of pixel (x, y) in frame n
func pixel(_ x: Int, _ y: Int, _ n: Int) -> (Double, Double, Double) {
  if y < height / 2 {
    let bar = min(6, x * 7 / width)
    return bars[bar]
  }
  if y < height * 3 / 4 {
    let side = height / 8
    let left = (4 * n) % (width - side)
    let top = height / 2 + (height / 4 - side) / 2
    if x >= left && x < left + side && y >= top && y < top + side { return (255, 255, 255) }
    return (64, 64, 64)
  }
  if y < height * 7 / 8 {
    let bit = min(15, x * 16 / width)
    let set = (n >> (15 - bit)) & 1 == 1
    return set ? (255, 255, 255) : (0, 0, 0)
  }
  let bandTop = height * 7 / 8
  let bandHeight = height - bandTop
  let cellHeight = bandHeight * 3 / 4
  let cellWidth = cellHeight / 2
  let gap = max(2, cellWidth / 3)
  let marginTop = (bandHeight - cellHeight) / 2
  let marginLeft = gap
  let digitIndex = (x - marginLeft) / (cellWidth + gap)
  let insideX = (x - marginLeft) % (cellWidth + gap)
  let insideY = y - bandTop - marginTop
  if x >= marginLeft && digitIndex < 5 && insideX < cellWidth && insideY >= 0
    && insideY < cellHeight
  {
    var value = n
    for _ in 0..<(4 - digitIndex) { value /= 10 }
    if digitLit(value % 10, insideX, insideY, cellWidth, cellHeight) { return (255, 255, 255) }
  }
  return (0, 0, 0)
}

let tenBit = codecName == "hevc10" || codecName == "prores422hq"
let chroma422 = codecName == "prores422hq"

// R'G'B' (0...255) as 10-bit Y'CbCr codes, limited or full range
func ycbcr10(_ r: Double, _ g: Double, _ b: Double) -> (Double, Double, Double) {
  let rn = r / 255.0
  let gn = g / 255.0
  let bn = b / 255.0
  let luma = kr * rn + kg * gn + kb * bn
  let cb = (bn - luma) / (2.0 * (1.0 - kb))
  let cr = (rn - luma) / (2.0 * (1.0 - kr))
  if fullRange {
    return (1023.0 * luma, 512.0 + 1023.0 * cb, 512.0 + 1023.0 * cr)
  }
  return (64.0 + 876.0 * luma, 512.0 + 896.0 * cb, 512.0 + 896.0 * cr)
}

// A 10-bit frame's codes at (x, y): band 2 is the ramp, the rest as pixel()
func codes10(_ x: Int, _ y: Int, _ n: Int) -> (Double, Double, Double) {
  if y >= height / 2 && y < height * 3 / 4 {
    if x >= 128 && x < 128 + 1024 {
      // full range: 0 to 1023; limited (ProRes stores no other): 64 to 940,
      // then 940 to the end
      let code = fullRange ? Double(x - 128) : min(940.0, 64.0 + Double(x - 128))
      return (code, 512.0, 512.0)
    }
    return ycbcr10(128, 128, 128)
  }
  let (r, g, b) = pixel(x, y, n)
  return ycbcr10(r, g, b)
}

func code16(_ value: Double) -> UInt16 {
  return UInt16(max(0, min(1023, value.rounded()))) << 6
}

func makePixelBuffer10(_ n: Int, _ pool: CVPixelBufferPool) -> CVPixelBuffer {
  var made: CVPixelBuffer?
  CVPixelBufferPoolCreatePixelBuffer(nil, pool, &made)
  let buffer = made!
  CVPixelBufferLockBaseAddress(buffer, [])
  let lumaBase = CVPixelBufferGetBaseAddressOfPlane(buffer, 0)!.assumingMemoryBound(to: UInt16.self)
  let lumaStride = CVPixelBufferGetBytesPerRowOfPlane(buffer, 0) / 2
  let chromaBase = CVPixelBufferGetBaseAddressOfPlane(buffer, 1)!.assumingMemoryBound(to: UInt16.self)
  let chromaStride = CVPixelBufferGetBytesPerRowOfPlane(buffer, 1) / 2
  let rowsPerChroma = chroma422 ? 1 : 2
  for cy in 0..<(height / rowsPerChroma) {
    for cx in 0..<(width / 2) {
      var cbSum = 0.0
      var crSum = 0.0
      for dy in 0..<rowsPerChroma {
        for dx in 0..<2 {
          let x = cx * 2 + dx
          let y = cy * rowsPerChroma + dy
          let (luma, cb, cr) = codes10(x, y, n)
          lumaBase[y * lumaStride + x] = code16(luma)
          cbSum += cb
          crSum += cr
        }
      }
      let samples = Double(2 * rowsPerChroma)
      chromaBase[cy * chromaStride + cx * 2] = code16(cbSum / samples)
      chromaBase[cy * chromaStride + cx * 2 + 1] = code16(crSum / samples)
    }
  }
  CVPixelBufferUnlockBaseAddress(buffer, [])
  let matrix =
    matrixName == "601" ? kCVImageBufferYCbCrMatrix_ITU_R_601_4 : kCVImageBufferYCbCrMatrix_ITU_R_709_2
  CVBufferSetAttachment(buffer, kCVImageBufferYCbCrMatrixKey, matrix, .shouldPropagate)
  CVBufferSetAttachment(
    buffer, kCVImageBufferColorPrimariesKey, kCVImageBufferColorPrimaries_ITU_R_709_2,
    .shouldPropagate)
  CVBufferSetAttachment(
    buffer, kCVImageBufferTransferFunctionKey, kCVImageBufferTransferFunction_ITU_R_709_2,
    .shouldPropagate)
  return buffer
}

func makePixelBuffer(_ n: Int, _ pool: CVPixelBufferPool) -> CVPixelBuffer {
  var made: CVPixelBuffer?
  CVPixelBufferPoolCreatePixelBuffer(nil, pool, &made)
  let buffer = made!
  CVPixelBufferLockBaseAddress(buffer, [])
  let lumaBase = CVPixelBufferGetBaseAddressOfPlane(buffer, 0)!.assumingMemoryBound(to: UInt8.self)
  let lumaStride = CVPixelBufferGetBytesPerRowOfPlane(buffer, 0)
  let chromaBase = CVPixelBufferGetBaseAddressOfPlane(buffer, 1)!.assumingMemoryBound(
    to: UInt8.self)
  let chromaStride = CVPixelBufferGetBytesPerRowOfPlane(buffer, 1)
  for cy in 0..<(height / 2) {
    for cx in 0..<(width / 2) {
      var cbSum = 0.0
      var crSum = 0.0
      for dy in 0..<2 {
        for dx in 0..<2 {
          let x = cx * 2 + dx
          let y = cy * 2 + dy
          let (r, g, b) = pixel(x, y, n)
          let (luma, cb, cr) = ycbcr(r, g, b)
          lumaBase[y * lumaStride + x] = UInt8(max(0, min(255, luma.rounded())))
          cbSum += cb
          crSum += cr
        }
      }
      chromaBase[cy * chromaStride + cx * 2] = UInt8(max(0, min(255, (cbSum / 4).rounded())))
      chromaBase[cy * chromaStride + cx * 2 + 1] = UInt8(max(0, min(255, (crSum / 4).rounded())))
    }
  }
  CVPixelBufferUnlockBaseAddress(buffer, [])
  let matrix =
    matrixName == "601" ? kCVImageBufferYCbCrMatrix_ITU_R_601_4 : kCVImageBufferYCbCrMatrix_ITU_R_709_2
  CVBufferSetAttachment(buffer, kCVImageBufferYCbCrMatrixKey, matrix, .shouldPropagate)
  CVBufferSetAttachment(
    buffer, kCVImageBufferColorPrimariesKey, kCVImageBufferColorPrimaries_ITU_R_709_2,
    .shouldPropagate)
  CVBufferSetAttachment(
    buffer, kCVImageBufferTransferFunctionKey, kCVImageBufferTransferFunction_ITU_R_709_2,
    .shouldPropagate)
  return buffer
}

// raw422: each frame's Y plane, then Cb and Cr at half width (the mean of
// each pair), no container
if codecName == "raw422" {
  var bytes = [UInt8]()
  bytes.reserveCapacity(frameCount * width * height * 2)
  for n in 0..<frameCount {
    var cbPlane = [UInt8](repeating: 0, count: width / 2 * height)
    var crPlane = [UInt8](repeating: 0, count: width / 2 * height)
    for y in 0..<height {
      for cx in 0..<(width / 2) {
        var cbSum = 0.0
        var crSum = 0.0
        for dx in 0..<2 {
          let (r, g, b) = pixel(cx * 2 + dx, y, n)
          let (luma, cb, cr) = ycbcr(r, g, b)
          bytes.append(UInt8(max(0, min(255, luma.rounded()))))
          cbSum += cb
          crSum += cr
        }
        cbPlane[y * width / 2 + cx] = UInt8(max(0, min(255, (cbSum / 2).rounded())))
        crPlane[y * width / 2 + cx] = UInt8(max(0, min(255, (crSum / 2).rounded())))
      }
    }
    bytes.append(contentsOf: cbPlane)
    bytes.append(contentsOf: crPlane)
  }
  FileManager.default.createFile(atPath: outPath, contents: Data(bytes))
  print("made \(outPath): raw 4:2:2, \(width)x\(height), \(frameCount) frames, BT.\(matrixName) \(fullRange ? "full" : "limited")")
  exit(0)
}

let url = URL(fileURLWithPath: outPath)
try? FileManager.default.removeItem(at: url)
let fileType: AVFileType = outPath.hasSuffix(".mov") ? .mov : .mp4
let writer = try! AVAssetWriter(outputURL: url, fileType: fileType)

let codec: AVVideoCodecType
switch codecName {
case "hevc", "hevc10": codec = .hevc
case "prores": codec = .proRes422Proxy
case "prores422hq": codec = .proRes422HQ
default: codec = .h264
}
var videoSettings: [String: Any] = [
  AVVideoCodecKey: codec,
  AVVideoWidthKey: width,
  AVVideoHeightKey: height,
  AVVideoColorPropertiesKey: [
    AVVideoColorPrimariesKey: AVVideoColorPrimaries_ITU_R_709_2,
    AVVideoTransferFunctionKey: AVVideoTransferFunction_ITU_R_709_2,
    AVVideoYCbCrMatrixKey: matrixName == "601"
      ? AVVideoYCbCrMatrix_ITU_R_601_4 : AVVideoYCbCrMatrix_ITU_R_709_2,
  ],
]
if codecName == "hevc10" {
  // Main10, every frame a keyframe, a high bit rate: the ramp's single codes
  // are what the test reads
  videoSettings[AVVideoCompressionPropertiesKey] = [
    AVVideoProfileLevelKey: kVTProfileLevel_HEVC_Main10_AutoLevel as String,
    AVVideoQualityKey: 1.0,
    AVVideoMaxKeyFrameIntervalKey: 1,
    AVVideoAllowFrameReorderingKey: false,
  ]
}
if codecName == "h264" || codecName == "hevc" {
  var compression: [String: Any] = [
    AVVideoAverageBitRateKey: max(400_000, width * height * 8),
    AVVideoMaxKeyFrameIntervalKey: 30,
    AVVideoAllowFrameReorderingKey: true,
  ]
  if codecName == "h264" { compression[AVVideoProfileLevelKey] = AVVideoProfileLevelH264HighAutoLevel }
  videoSettings[AVVideoCompressionPropertiesKey] = compression
}
let videoInput = AVAssetWriterInput(mediaType: .video, outputSettings: videoSettings)
videoInput.expectsMediaDataInRealTime = false
videoInput.mediaTimeScale = timescale
var pixelFormat =
  fullRange
  ? kCVPixelFormatType_420YpCbCr8BiPlanarFullRange : kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange
if tenBit {
  if chroma422 {
    pixelFormat =
      fullRange
      ? kCVPixelFormatType_422YpCbCr10BiPlanarFullRange : kCVPixelFormatType_422YpCbCr10BiPlanarVideoRange
  } else {
    pixelFormat =
      fullRange
      ? kCVPixelFormatType_420YpCbCr10BiPlanarFullRange : kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange
  }
}
let adaptor = AVAssetWriterInputPixelBufferAdaptor(
  assetWriterInput: videoInput,
  sourcePixelBufferAttributes: [
    kCVPixelBufferPixelFormatTypeKey as String: pixelFormat,
    kCVPixelBufferWidthKey as String: width,
    kCVPixelBufferHeightKey as String: height,
  ])
writer.add(videoInput)

let sampleRate = 48000
let channels = 2
var audioInput: AVAssetWriterInput? = nil
if withTone {
  let input = AVAssetWriterInput(
    mediaType: .audio,
    outputSettings: [
      AVFormatIDKey: kAudioFormatMPEG4AAC,
      AVSampleRateKey: sampleRate,
      AVNumberOfChannelsKey: channels,
      AVEncoderBitRateKey: 128_000,
    ])
  input.expectsMediaDataInRealTime = false
  writer.add(input)
  audioInput = input
}

writer.movieTimeScale = timescale
writer.startWriting()
writer.startSession(atSourceTime: .zero)

let totalAudioFrames = Int(
  (Int64(frameCount) * frameDuration * Int64(sampleRate) + Int64(timescale) - 1) / Int64(timescale))
var audioDescription = AudioStreamBasicDescription(
  mSampleRate: Float64(sampleRate), mFormatID: kAudioFormatLinearPCM,
  mFormatFlags: kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked,
  mBytesPerPacket: UInt32(4 * channels), mFramesPerPacket: 1, mBytesPerFrame: UInt32(4 * channels),
  mChannelsPerFrame: UInt32(channels), mBitsPerChannel: 32, mReserved: 0)
var audioFormat: CMAudioFormatDescription?
CMAudioFormatDescriptionCreate(
  allocator: nil, asbd: &audioDescription, layoutSize: 0, layout: nil, magicCookieSize: 0,
  magicCookie: nil, extensions: nil, formatDescriptionOut: &audioFormat)

// The rising tone's phase at sample n: its frequency goes from 500 Hz to
// 1500 Hz over the file's length T, so the phase is
// 2 pi (500 t + 1000 t^2 / (2 T))
let toneSeconds = Double(Int64(frameCount) * frameDuration) / Double(timescale)
func tonePhase(_ n: Int) -> Double {
  let t = Double(n) / Double(sampleRate)
  return 2.0 * Double.pi * (500.0 * t + 1000.0 * t * t / (2.0 * toneSeconds))
}

// One block of the tone from frame `first`
func toneBuffer(first: Int, count: Int) -> CMSampleBuffer {
  let bytes = count * channels * 4
  var block: CMBlockBuffer?
  CMBlockBufferCreateWithMemoryBlock(
    allocator: nil, memoryBlock: nil, blockLength: bytes, blockAllocator: nil,
    customBlockSource: nil, offsetToData: 0, dataLength: bytes, flags: 0, blockBufferOut: &block)
  CMBlockBufferAssureBlockMemory(block!)
  var samples = [Float](repeating: 0, count: count * channels)
  for i in 0..<count {
    let value = Float(0.25 * sin(tonePhase(first + i)))
    for c in 0..<channels { samples[i * channels + c] = value }
  }
  samples.withUnsafeBytes { raw in
    _ = CMBlockBufferReplaceDataBytes(
      with: raw.baseAddress!, blockBuffer: block!, offsetIntoDestination: 0, dataLength: bytes)
  }
  var sample: CMSampleBuffer?
  CMAudioSampleBufferCreateReadyWithPacketDescriptions(
    allocator: nil, dataBuffer: block!, formatDescription: audioFormat!, sampleCount: count,
    presentationTimeStamp: CMTime(value: Int64(first), timescale: Int32(sampleRate)),
    packetDescriptions: nil, sampleBufferOut: &sample)
  return sample!
}

let group = DispatchGroup()
group.enter()
var frameIndex = 0
videoInput.requestMediaDataWhenReady(on: DispatchQueue(label: "video")) {
  while videoInput.isReadyForMoreMediaData && frameIndex < frameCount {
    let buffer =
      tenBit
      ? makePixelBuffer10(frameIndex, adaptor.pixelBufferPool!)
      : makePixelBuffer(frameIndex, adaptor.pixelBufferPool!)
    let time = CMTime(value: Int64(frameIndex) * frameDuration, timescale: timescale)
    if !adaptor.append(buffer, withPresentationTime: time) {
      FileHandle.standardError.write("video append failed: \(String(describing: writer.error))\n".data(using: .utf8)!)
      exit(1)
    }
    frameIndex += 1
  }
  if frameIndex >= frameCount {
    videoInput.markAsFinished()
    group.leave()
  }
}
if let input = audioInput {
  group.enter()
  var audioFrame = 0
  input.requestMediaDataWhenReady(on: DispatchQueue(label: "audio")) {
    while input.isReadyForMoreMediaData && audioFrame < totalAudioFrames {
      let count = min(1024, totalAudioFrames - audioFrame)
      if !input.append(toneBuffer(first: audioFrame, count: count)) {
        FileHandle.standardError.write("audio append failed: \(String(describing: writer.error))\n".data(using: .utf8)!)
        exit(1)
      }
      audioFrame += count
    }
    if audioFrame >= totalAudioFrames {
      input.markAsFinished()
      group.leave()
    }
  }
}
group.wait()
let done = DispatchSemaphore(value: 0)
writer.finishWriting { done.signal() }
done.wait()
if writer.status != .completed {
  FileHandle.standardError.write("writing failed: \(String(describing: writer.error))\n".data(using: .utf8)!)
  exit(1)
}
print("made \(outPath): \(codecName) \(width)x\(height), \(frameCount) frames at \(timescale)/\(frameDuration), BT.\(matrixName) \(fullRange ? "full" : "limited")\(withTone ? ", tone" : "")")
