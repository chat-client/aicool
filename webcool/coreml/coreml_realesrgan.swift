import Foundation
import CoreML
import CoreImage
import CoreVideo
import AVFoundation

enum RunnerError: Error { case usage, imageLoad, pixelBuffer, pixelBufferPool, prediction, output }

func makePixelBufferPool(width: Int, height: Int) throws -> CVPixelBufferPool {
	var pool: CVPixelBufferPool?
	let poolAttributes = [kCVPixelBufferPoolMinimumBufferCountKey: 2] as CFDictionary
	let pixelAttributes = [
		kCVPixelBufferWidthKey: width,
		kCVPixelBufferHeightKey: height,
		kCVPixelBufferPixelFormatTypeKey: kCVPixelFormatType_32BGRA,
		kCVPixelBufferIOSurfacePropertiesKey: [:],
		kCVPixelBufferMetalCompatibilityKey: true,
		kCVPixelBufferCGImageCompatibilityKey: true,
		kCVPixelBufferCGBitmapContextCompatibilityKey: true
	] as CFDictionary
	guard CVPixelBufferPoolCreate(kCFAllocatorDefault, poolAttributes,
	                              pixelAttributes, &pool) == kCVReturnSuccess,
	      let result = pool else { throw RunnerError.pixelBufferPool }
	return result
}

func pixelBuffer(from pool: CVPixelBufferPool) throws -> CVPixelBuffer {
    var buffer: CVPixelBuffer?
    guard CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, pool, &buffer) == kCVReturnSuccess,
          let result = buffer else { throw RunnerError.pixelBuffer }
    return result
}

struct TileJob {
    let provider: MLFeatureProvider
    let inputBuffer: CVPixelBuffer
    let x: Int
    let y: Int
    let outputWidth: Int
    let outputHeight: Int
}

func enhanceImage(_ source: CIImage, model: MLModel, context: CIContext,
                  inputPool: CVPixelBufferPool, inputName: String,
                  outputName: String, tileSize: Int, modelScale: Int,
                  batchSize: Int, overlapMode: String) throws -> CIImage {
    let sourceRect = source.extent.integral
    let outputWidth = Int(sourceRect.width) * modelScale
    let outputHeight = Int(sourceRect.height) * modelScale
    var result = CIImage(color: .black).cropped(to: CGRect(x: 0, y: 0,
                                                           width: outputWidth,
                                                           height: outputHeight))
    // Keep only the context-rich center of each prediction. Adjacent tiles
    // overlap in the source but never blend generated pixels, avoiding the
    // grid seams that are especially visible with the x2 RRDB model.
    let margin: Int
    switch overlapMode {
    case "low": margin = max(4, tileSize / 32)
    case "quality": margin = max(12, tileSize / 12)
    default: margin = tileSize <= 256 ? max(8, tileSize / 16) : max(12, tileSize / 20)
    }
    let coreSize = tileSize - margin * 2
    var jobs: [TileJob] = []

    func flushJobs() throws {
        guard !jobs.isEmpty else { return }
        let predictions: [MLFeatureProvider]
        if jobs.count == 1 {
            predictions = [try model.prediction(from: jobs[0].provider)]
        } else {
            let batch = MLArrayBatchProvider(array: jobs.map { $0.provider })
            let outputBatch = try model.predictions(fromBatch: batch)
            predictions = (0..<outputBatch.count).map { outputBatch.features(at: $0) }
        }
        for (index, prediction) in predictions.enumerated() {
            let job = jobs[index]
            guard let enhancedBuffer = prediction.featureValue(for: outputName)?.imageBufferValue
            else { throw RunnerError.prediction }
            let enhanced = CIImage(cvPixelBuffer: enhancedBuffer)
                .cropped(to: CGRect(x: margin * modelScale, y: margin * modelScale,
                                    width: job.outputWidth * modelScale,
                                    height: job.outputHeight * modelScale))
                .transformed(by: CGAffineTransform(translationX: CGFloat(-margin * modelScale),
                                                   y: CGFloat(-margin * modelScale)))
                .transformed(by: CGAffineTransform(translationX: CGFloat(job.x * modelScale),
                                                   y: CGFloat(job.y * modelScale)))
            result = enhanced.composited(over: result)
        }
        jobs.removeAll(keepingCapacity: true)
    }

    for y in stride(from: 0, to: Int(sourceRect.height), by: coreSize) {
        for x in stride(from: 0, to: Int(sourceRect.width), by: coreSize) {
            let sampleX = max(0, x - margin)
            let sampleY = max(0, y - margin)
            let padX = max(0, margin - x)
            let padY = max(0, margin - y)
            let sampleWidth = min(tileSize - padX, Int(sourceRect.width) - sampleX)
            let sampleHeight = min(tileSize - padY, Int(sourceRect.height) - sampleY)
            let outputCoreWidth = min(coreSize, Int(sourceRect.width) - x)
            let outputCoreHeight = min(coreSize, Int(sourceRect.height) - y)
            let cropRect = CGRect(x: sourceRect.minX + CGFloat(sampleX),
                                  y: sourceRect.minY + CGFloat(sampleY),
                                  width: CGFloat(sampleWidth), height: CGFloat(sampleHeight))
            let tile = source.cropped(to: cropRect)
                .transformed(by: CGAffineTransform(translationX: -cropRect.minX,
                                                   y: -cropRect.minY))
                .transformed(by: CGAffineTransform(translationX: CGFloat(padX),
                                                   y: CGFloat(padY)))
            let background = CIImage(color: .black).cropped(to: CGRect(x: 0, y: 0,
                                                                        width: tileSize,
                                                                        height: tileSize))
            let padded = tile.composited(over: background)
            let inputBuffer = try pixelBuffer(from: inputPool)
            context.render(padded, to: inputBuffer,
                           bounds: CGRect(x: 0, y: 0, width: tileSize, height: tileSize),
                           colorSpace: CGColorSpaceCreateDeviceRGB())
            let provider = try MLDictionaryFeatureProvider(
                dictionary: [inputName: MLFeatureValue(pixelBuffer: inputBuffer)])
            jobs.append(TileJob(provider: provider, inputBuffer: inputBuffer, x: x, y: y,
                                outputWidth: outputCoreWidth, outputHeight: outputCoreHeight))
            if jobs.count >= batchSize { try flushJobs() }
        }
    }
    try flushJobs()
    return result
}

func processImage(_ inputURL: URL, outputURL: URL, model: MLModel,
                  context: CIContext, inputPool: CVPixelBufferPool,
                  inputName: String, outputName: String,
                  tileSize: Int, modelScale: Int, batchSize: Int,
                  overlapMode: String) throws {
    guard let source = CIImage(contentsOf: inputURL) else { throw RunnerError.imageLoad }
    let result = try enhanceImage(source, model: model, context: context,
                                  inputPool: inputPool, inputName: inputName,
                                  outputName: outputName, tileSize: tileSize,
                                  modelScale: modelScale, batchSize: batchSize,
                                  overlapMode: overlapMode)
    try context.writePNGRepresentation(of: result,
                                       to: outputURL,
                                       format: .RGBA8,
                                       colorSpace: CGColorSpaceCreateDeviceRGB())
}

func argument(_ name: String) -> String? {
    guard let index = CommandLine.arguments.firstIndex(of: name),
          index + 1 < CommandLine.arguments.count else { return nil }
    return CommandLine.arguments[index + 1]
}

guard let modelPath = argument("--model"),
      let inputPath = argument("--input"),
      let outputPath = argument("--output") else { throw RunnerError.usage }

let rawModelURL = URL(fileURLWithPath: modelPath)
let compiledURL: URL
if rawModelURL.pathExtension == "mlmodelc" {
    compiledURL = rawModelURL
} else {
    compiledURL = try MLModel.compileModel(at: rawModelURL)
}
let configuration = MLModelConfiguration()
let computeUnits = (argument("--compute-units") ?? "auto").lowercased()
switch computeUnits {
case "gpu": configuration.computeUnits = .cpuAndGPU
case "ane": configuration.computeUnits = .cpuAndNeuralEngine
case "cpu": configuration.computeUnits = .cpuOnly
default: configuration.computeUnits = .all
}
configuration.allowLowPrecisionAccumulationOnGPU = true
let workerCount = max(1, min(4, Int(argument("--workers") ?? "2") ?? 2))
let tileBatchSize = max(1, min(4, Int(argument("--tile-batch") ?? "1") ?? 1))
let overlapMode: String = {
    let value = (argument("--overlap") ?? "balanced").lowercased()
    return value == "low" || value == "quality" ? value : "balanced"
}()
final class InferenceWorker {
    let model: MLModel
    let context: CIContext
    let inputPool: CVPixelBufferPool
    let outputPool: CVPixelBufferPool?
    let queue: DispatchQueue
    let inputName: String
    let outputName: String
    let tileSize: Int
    let modelScale: Int

    init(index: Int, outputWidth: Int? = nil, outputHeight: Int? = nil) throws {
        // Apple requires an MLModel instance to be used on only one thread or
        // dispatch queue at a time. Each worker therefore owns its complete
        // inference state instead of contending on one shared model instance.
        model = try MLModel(contentsOf: compiledURL, configuration: configuration)
        guard let input = model.modelDescription.inputDescriptionsByName.first(where: { $0.value.imageConstraint != nil }),
              let output = model.modelDescription.outputDescriptionsByName.first(where: { $0.value.imageConstraint != nil }),
              let inputConstraint = input.value.imageConstraint,
              let outputConstraint = output.value.imageConstraint,
              inputConstraint.pixelsWide > 0,
              outputConstraint.pixelsWide % inputConstraint.pixelsWide == 0 else {
            throw RunnerError.prediction
        }
        inputName = input.key
        outputName = output.key
        tileSize = inputConstraint.pixelsWide
        modelScale = outputConstraint.pixelsWide / inputConstraint.pixelsWide
        context = CIContext(options: [.cacheIntermediates: false,
                                      .priorityRequestLow: false])
        inputPool = try makePixelBufferPool(width: inputConstraint.pixelsWide,
                                            height: inputConstraint.pixelsHigh)
        if let width = outputWidth, let height = outputHeight {
            outputPool = try makePixelBufferPool(width: width, height: height)
        } else {
            outputPool = nil
        }
        queue = DispatchQueue(label: "webcool.coreml.worker.\(index)", qos: .userInitiated)
    }
}

struct VideoFrame {
    // A nil buffer means reuse the most recent enhanced frame at this PTS.
    // This is only produced when the user explicitly enables temporal reuse.
    let buffer: CVPixelBuffer?
    let time: CMTime
}

final class PipelineState {
    let condition = NSCondition()
    var frames: [Int: VideoFrame] = [:]
    var submitted = 0
    var producerDone = false
    var error: Error?
}

func renderVideoFrame(_ sourceBuffer: CVPixelBuffer, time: CMTime,
                      worker: InferenceWorker, width: Int, height: Int,
                      preferredTransform: CGAffineTransform,
                      inputSizing: String, batchSize: Int,
                      overlapMode: String) throws -> VideoFrame {
    var source = CIImage(cvPixelBuffer: sourceBuffer).transformed(by: preferredTransform)
    source = source.transformed(by: CGAffineTransform(translationX: -source.extent.minX,
                                                       y: -source.extent.minY))
    if inputSizing == "target" && source.extent.width > 0 && source.extent.height > 0 {
        let outputFit = min(CGFloat(width) / source.extent.width,
                            CGFloat(height) / source.extent.height)
        let inferenceScale = min(1.0, outputFit / CGFloat(worker.modelScale))
        if inferenceScale < 0.999 {
            source = source.transformed(by: CGAffineTransform(scaleX: inferenceScale,
                                                               y: inferenceScale))
        }
    }
    let enhanced = try enhanceImage(source, model: worker.model,
                                    context: worker.context, inputPool: worker.inputPool,
                                    inputName: worker.inputName, outputName: worker.outputName,
                                    tileSize: worker.tileSize, modelScale: worker.modelScale,
                                    batchSize: batchSize, overlapMode: overlapMode)
    guard let pool = worker.outputPool else { throw RunnerError.pixelBufferPool }
    let destination = try pixelBuffer(from: pool)
    let scale = min(CGFloat(width) / enhanced.extent.width,
                    CGFloat(height) / enhanced.extent.height)
    let scaled = enhanced.transformed(by: CGAffineTransform(scaleX: scale, y: scale))
    let x = (CGFloat(width) - scaled.extent.width) / 2.0 - scaled.extent.minX
    let y = (CGFloat(height) - scaled.extent.height) / 2.0 - scaled.extent.minY
    let positioned = scaled.transformed(by: CGAffineTransform(translationX: x, y: y))
    let canvas = CIImage(color: .black).cropped(to: CGRect(x: 0, y: 0,
                                                           width: width, height: height))
    worker.context.render(positioned.composited(over: canvas), to: destination,
                          bounds: CGRect(x: 0, y: 0, width: width, height: height),
                          colorSpace: CGColorSpaceCreateDeviceRGB())
    return VideoFrame(buffer: destination, time: time)
}

func runVideoPipeline(inputPath: String, outputPath: String, workers: [InferenceWorker],
                      width: Int, height: Int, bitrate: Int, previewSeconds: Double,
                      inputSizing: String, batchSize: Int, overlapMode: String,
                      temporalStep: Int) throws {
    let asset = AVURLAsset(url: URL(fileURLWithPath: inputPath))
    guard let track = asset.tracks(withMediaType: .video).first else { throw RunnerError.imageLoad }
    let preferredTransform = track.preferredTransform
    let reader = try AVAssetReader(asset: asset)
    let readerOutput = AVAssetReaderTrackOutput(track: track, outputSettings: [
        kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32BGRA,
        kCVPixelBufferMetalCompatibilityKey as String: true
    ])
    readerOutput.alwaysCopiesSampleData = false
    guard reader.canAdd(readerOutput) else { throw RunnerError.output }
    reader.add(readerOutput)

    let outputURL = URL(fileURLWithPath: outputPath)
    try? FileManager.default.removeItem(at: outputURL)
    let writer = try AVAssetWriter(outputURL: outputURL, fileType: .mp4)
    let writerInput = AVAssetWriterInput(mediaType: .video, outputSettings: [
        AVVideoCodecKey: AVVideoCodecType.h264,
        AVVideoWidthKey: width,
        AVVideoHeightKey: height,
        AVVideoCompressionPropertiesKey: [AVVideoAverageBitRateKey: bitrate]
    ])
    writerInput.expectsMediaDataInRealTime = false
    let adaptor = AVAssetWriterInputPixelBufferAdaptor(assetWriterInput: writerInput,
        sourcePixelBufferAttributes: [
            kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32BGRA,
            kCVPixelBufferWidthKey as String: width,
            kCVPixelBufferHeightKey as String: height
        ])
    guard writer.canAdd(writerInput) else { throw RunnerError.output }
    writer.add(writerInput)
    guard writer.startWriting() else { throw writer.error ?? RunnerError.output }
    writer.startSession(atSourceTime: .zero)
    guard reader.startReading() else { throw reader.error ?? RunnerError.imageLoad }

    let state = PipelineState()
    let workGroup = DispatchGroup()
    let writerGroup = DispatchGroup()
    let capacity = DispatchSemaphore(value: max(2, workers.count * 2))
    writerGroup.enter()
    DispatchQueue(label: "webcool.coreml.video-writer", qos: .userInitiated).async {
        defer { writerGroup.leave() }
        var expected = 0
        var lastEnhancedBuffer: CVPixelBuffer?
        while true {
            state.condition.lock()
            while state.frames[expected] == nil && state.error == nil
                    && !(state.producerDone && expected >= state.submitted) {
                state.condition.wait()
            }
            if state.error != nil {
                state.condition.unlock()
                break
            }
            if state.producerDone && expected >= state.submitted {
                state.condition.unlock()
                break
            }
            guard let frame = state.frames.removeValue(forKey: expected) else {
                state.condition.unlock()
                continue
            }
            state.condition.unlock()
            guard let outputBuffer = frame.buffer ?? lastEnhancedBuffer else {
                state.condition.lock()
                state.error = RunnerError.output
                state.condition.broadcast()
                state.condition.unlock()
                capacity.signal()
                break
            }
            if let enhancedBuffer = frame.buffer { lastEnhancedBuffer = enhancedBuffer }
            while !writerInput.isReadyForMoreMediaData { Thread.sleep(forTimeInterval: 0.002) }
            if !adaptor.append(outputBuffer, withPresentationTime: frame.time) {
                state.condition.lock()
                state.error = writer.error ?? RunnerError.output
                state.condition.broadcast()
                state.condition.unlock()
                capacity.signal()
                break
            }
            expected += 1
            let microseconds = Int64(max(0, CMTimeGetSeconds(frame.time)) * 1_000_000.0)
            print("out_time_us=\(microseconds)")
            fflush(stdout)
            capacity.signal()
        }
    }

    var index = 0
    while let sample = readerOutput.copyNextSampleBuffer() {
        let time = CMSampleBufferGetPresentationTimeStamp(sample)
        if previewSeconds > 0 && CMTimeGetSeconds(time) >= previewSeconds { break }
        guard let sourceBuffer = CMSampleBufferGetImageBuffer(sample) else { continue }
        var acquiredCapacity = false
        while !acquiredCapacity {
            if capacity.wait(timeout: .now() + 0.1) == .success {
                acquiredCapacity = true
            } else {
                state.condition.lock()
                let failed = state.error != nil
                state.condition.unlock()
                if failed { break }
            }
        }
        if !acquiredCapacity { break }
        state.condition.lock()
        if state.error != nil {
            state.condition.unlock()
            capacity.signal()
            break
        }
        state.submitted += 1
        state.condition.unlock()
        let frameIndex = index
        let worker = workers[index % workers.count]
        index += 1
        if frameIndex % temporalStep != 0 {
            state.condition.lock()
            state.frames[frameIndex] = VideoFrame(buffer: nil, time: time)
            state.condition.broadcast()
            state.condition.unlock()
            continue
        }
        workGroup.enter()
        worker.queue.async {
            defer { workGroup.leave() }
            do {
                let result = try autoreleasepool {
                    try renderVideoFrame(sourceBuffer, time: time, worker: worker,
                                         width: width, height: height,
                                         preferredTransform: preferredTransform,
                                         inputSizing: inputSizing, batchSize: batchSize,
                                         overlapMode: overlapMode)
                }
                state.condition.lock()
                state.frames[frameIndex] = result
                state.condition.broadcast()
                state.condition.unlock()
            } catch {
                state.condition.lock()
                if state.error == nil { state.error = error }
                state.condition.broadcast()
                state.condition.unlock()
                capacity.signal()
            }
        }
    }
    workGroup.wait()
    state.condition.lock()
    state.producerDone = true
    state.condition.broadcast()
    state.condition.unlock()
    writerGroup.wait()
    if let error = state.error { reader.cancelReading(); writer.cancelWriting(); throw error }
    writerInput.markAsFinished()
    let finish = DispatchSemaphore(value: 0)
    writer.finishWriting { finish.signal() }
    finish.wait()
    guard writer.status == .completed else { throw writer.error ?? RunnerError.output }
    print("progress=end")
    fflush(stdout)
}

if CommandLine.arguments.contains("--video") {
    let width = Int(argument("--width") ?? "0") ?? 0
    let height = Int(argument("--height") ?? "0") ?? 0
    let bitrate = Int(argument("--bitrate") ?? "8000000") ?? 8000000
    let preview = Double(argument("--preview-seconds") ?? "0") ?? 0
    let inputSizing = argument("--input-sizing") == "source" ? "source" : "target"
    let temporalStep = max(1, min(3, Int(argument("--temporal-step") ?? "1") ?? 1))
    guard width > 0, height > 0 else { throw RunnerError.usage }
    let workers = try (0..<workerCount).map {
        try InferenceWorker(index: $0, outputWidth: width, outputHeight: height)
    }
    try runVideoPipeline(inputPath: inputPath, outputPath: outputPath, workers: workers,
                         width: width, height: height, bitrate: bitrate,
                         previewSeconds: preview, inputSizing: inputSizing,
                         batchSize: tileBatchSize, overlapMode: overlapMode,
                         temporalStep: temporalStep)
} else {
    let inputDirectory = URL(fileURLWithPath: inputPath)
    let outputDirectory = URL(fileURLWithPath: outputPath)
    let files = try FileManager.default.contentsOfDirectory(at: inputDirectory,
        includingPropertiesForKeys: nil).filter { $0.pathExtension.lowercased() == "png" }
        .sorted { $0.lastPathComponent < $1.lastPathComponent }
    let workers = try (0..<workerCount).map { try InferenceWorker(index: $0) }
    let group = DispatchGroup()
    let stateLock = NSLock()
    var completed = 0
    var firstError: Error?
    for (index, file) in files.enumerated() {
        let worker = workers[index % workers.count]
        group.enter()
        worker.queue.async {
            defer { group.leave() }
            do {
                try autoreleasepool {
                    try processImage(file,
                        outputURL: outputDirectory.appendingPathComponent(file.lastPathComponent),
                        model: worker.model, context: worker.context,
                        inputPool: worker.inputPool, inputName: worker.inputName,
                        outputName: worker.outputName, tileSize: worker.tileSize,
                        modelScale: worker.modelScale, batchSize: tileBatchSize,
                        overlapMode: overlapMode)
                }
                stateLock.lock()
                completed += 1
                let current = completed
                stateLock.unlock()
                print("frame=\(current)/\(files.count)")
                fflush(stdout)
            } catch {
                stateLock.lock()
                if firstError == nil { firstError = error }
                stateLock.unlock()
            }
        }
    }
    group.wait()
    if let error = firstError { throw error }
}
