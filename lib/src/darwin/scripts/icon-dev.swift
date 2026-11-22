// icon-dev.swift -- an image of the "Apogee Dev" icon, from the release icon's
// (GUI M18).
//
//   icon-dev <in> <out> <dpi> <red> <green> <blue>
//
// A development build is told apart from the installed app in the Dock by its
// icon. This draws one image of the release icon -- already made from the
// master by scripts/icon.sh -- into 8-bit sRGB, multiplies each pixel's red,
// green and blue by the tint's (0 to 255), so the artwork's white becomes the
// tint and its black stays black, and writes it as a PNG at `dpi`, the density
// the asset catalog expects of its scale. scripts/icon.sh compiles and runs it.
import CoreGraphics
import Foundation
import ImageIO
import UniformTypeIdentifiers

func fail(_ message: String) -> Never {
    FileHandle.standardError.write(Data("[icon] error: \(message)\n".utf8))
    exit(1)
}

let arguments = CommandLine.arguments
let tint = arguments.count == 7 ? arguments[4...6].compactMap { UInt16($0) }.filter { $0 <= 255 } : []
guard arguments.count == 7, let dpi = Double(arguments[3]), tint.count == 3 else {
    fail("usage: icon-dev <in> <out> <dpi> <red> <green> <blue>")
}

guard let source = CGImageSourceCreateWithURL(URL(fileURLWithPath: arguments[1]) as CFURL, nil),
    let image = CGImageSourceCreateImageAtIndex(source, 0, nil)
else { fail("could not read \(arguments[1])") }

let width = image.width
let height = image.height
var pixels = [UInt8](repeating: 0, count: width * height * 4)
guard let space = CGColorSpace(name: CGColorSpace.sRGB) else { fail("no sRGB colour space") }
let info = CGImageAlphaInfo.premultipliedLast.rawValue
let drawn = pixels.withUnsafeMutableBytes { bytes -> Bool in
    guard
        let context = CGContext(
            data: bytes.baseAddress, width: width, height: height, bitsPerComponent: 8, bytesPerRow: width * 4,
            space: space, bitmapInfo: info)
    else { return false }
    context.draw(image, in: CGRect(x: 0, y: 0, width: width, height: height))
    return true
}
guard drawn else { fail("could not draw \(arguments[1])") }

// Premultiplied, so a colour scaled by the tint stays within its alpha.
for pixel in stride(from: 0, to: pixels.count, by: 4) {
    for channel in 0..<3 {
        pixels[pixel + channel] = UInt8((UInt16(pixels[pixel + channel]) * tint[channel] + 127) / 255)
    }
}

let tinted: CGImage? = pixels.withUnsafeMutableBytes { bytes in
    CGContext(
        data: bytes.baseAddress, width: width, height: height, bitsPerComponent: 8, bytesPerRow: width * 4,
        space: space, bitmapInfo: info)?.makeImage()
}
guard let tinted,
    let file = CGImageDestinationCreateWithURL(
        URL(fileURLWithPath: arguments[2]) as CFURL, UTType.png.identifier as CFString, 1, nil)
else { fail("could not write \(arguments[2])") }
CGImageDestinationAddImage(
    file, tinted, [kCGImagePropertyDPIWidth: dpi, kCGImagePropertyDPIHeight: dpi] as CFDictionary)
guard CGImageDestinationFinalize(file) else { fail("could not write \(arguments[2])") }
