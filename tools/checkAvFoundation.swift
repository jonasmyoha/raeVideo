// Opens the files written by tests/writer with AVFoundation (what QuickTime
// Player is built on) and decodes every video frame and sound sample: the
// automatic stand-in for opening them in QuickTime Player by hand.
//   swift tools/checkAvFoundation.swift build/writer/*.mov build/writer/*.mp4
import AVFoundation

var failed = false
for path in CommandLine.arguments.dropFirst() {
  let asset = AVURLAsset(url: URL(fileURLWithPath: path))
  guard let reader = try? AVAssetReader(asset: asset) else {
    print("\(path): AVFoundation cannot read it"); failed = true; continue
  }
  var outputs: [(String, AVAssetReaderTrackOutput)] = []
  for track in asset.tracks {
    var settings: [String: Any]? = nil
    if track.mediaType == .video {
      settings = [kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32BGRA]
    } else if track.mediaType == .audio {
      settings = [AVFormatIDKey: kAudioFormatLinearPCM]
    }
    let output = AVAssetReaderTrackOutput(track: track, outputSettings: settings)
    reader.add(output)
    outputs.append((track.mediaType.rawValue, output))
  }
  reader.startReading()
  var counts: [String] = []
  for (kind, output) in outputs {
    var samples = 0
    while let buffer = output.copyNextSampleBuffer() {
      samples += kind == "soun" ? CMSampleBufferGetNumSamples(buffer) : 1
    }
    counts.append("\(kind) \(samples)")
  }
  let ok = reader.status == .completed
  if !ok { failed = true }
  print("\(path): \(ok ? "decoded" : "FAILED \(String(describing: reader.error))"): \(counts.joined(separator: ", "))")
}
exit(failed ? 1 : 0)
