// icon-small.swift -- the rocket for the app icon's smallest sizes (GUI 11a).
//
//   icon-small <master> <out> <pixels> <side> <top> <left> <radius>
//
// At 32 pixels the rocket's outline, about 12 pixels wide on the master, would
// be a quarter of a pixel, so the plain artwork is a gray smear. This keeps
// the master's bright outline, which drops the gray field, thickens it by
// `radius` master pixels, crops to the square around the rocket (`side`, its
// `top` and `left` measured from the master's top left) and scales to `pixels`.
// scripts/icon.sh compiles and runs it.
import CoreImage
import CoreImage.CIFilterBuiltins
import Foundation
import ImageIO
import UniformTypeIdentifiers

func fail(_ message: String) -> Never {
    FileHandle.standardError.write(Data("[icon] error: \(message)\n".utf8))
    exit(1)
}

let arguments = CommandLine.arguments
guard arguments.count == 8,
    let pixels = Int(arguments[3]), let side = Double(arguments[4]), let top = Double(arguments[5]),
    let left = Double(arguments[6]), let radius = Float(arguments[7])
else { fail("usage: icon-small <master> <out> <pixels> <side> <top> <left> <radius>") }
guard let master = CIImage(contentsOf: URL(fileURLWithPath: arguments[1])) else {
    fail("could not read \(arguments[1])")
}

let threshold = CIFilter.colorThreshold()
threshold.inputImage = master
threshold.threshold = 0.62
let thicken = CIFilter.morphologyMaximum()
thicken.inputImage = threshold.outputImage
thicken.radius = radius
guard let outline = thicken.outputImage else { fail("could not thicken the outline") }

// Core Image counts from the bottom left.
let square = CGRect(x: left, y: master.extent.height - top - side, width: side, height: side)
let scale = CIFilter.lanczosScaleTransform()
scale.inputImage = outline.cropped(to: square)
    .transformed(by: CGAffineTransform(translationX: -square.minX, y: -square.minY))
    .clampedToExtent()
scale.scale = Float(Double(pixels) / side)
scale.aspectRatio = 1
guard let scaled = scale.outputImage else { fail("could not scale the rocket") }

let space = CGColorSpace(name: CGColorSpace.sRGB)!
let bounds = CGRect(x: 0, y: 0, width: pixels, height: pixels)
let context = CIContext(options: [.workingColorSpace: space, .outputColorSpace: space])
guard let image = context.createCGImage(scaled.cropped(to: bounds), from: bounds, format: .RGBA8, colorSpace: space),
    let file = CGImageDestinationCreateWithURL(
        URL(fileURLWithPath: arguments[2]) as CFURL, UTType.png.identifier as CFString, 1, nil)
else { fail("could not write \(arguments[2])") }
CGImageDestinationAddImage(file, image, nil)
guard CGImageDestinationFinalize(file) else { fail("could not write \(arguments[2])") }
