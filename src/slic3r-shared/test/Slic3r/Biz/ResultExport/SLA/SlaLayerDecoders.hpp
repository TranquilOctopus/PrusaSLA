#pragma once

// Decoders for the layer image encodings the SLA exporters write, so a test can read an exported
// layer back as pixels without going through a printer. The schemes are the ones the exporters'
// own rasterizers write; doc/sla-fork/formats/*.md and the encoder in
// src/libslic3r/src/libslic3r/Format/*.cpp describe them.

#include <algorithm>
#include <cstdint>
#include <vector>

namespace Slic3r::Test {

// ---------------------------------------------------------------------------
// .ctb (Chitubox, unencrypted v2/v3), from Format/CtbSLA.cpp CtbSLARasterEncoder
// ---------------------------------------------------------------------------
// One byte per pixel, in raster order. A control byte 0x00..0xFD is a run of (control + 1) pixels
// of the value byte after it; a control byte 0xFF is a literal block whose length byte is followed
// by that many raw pixel bytes. There is no end marker and no checksum: the layer definition
// carries the byte count.
inline std::vector<uint8_t> decode_ctb_layer(const std::vector<uint8_t>& encoded, size_t expected_pixels)
{
    std::vector<uint8_t> pixels;
    pixels.reserve(expected_pixels);

    size_t i = 0;
    while (i < encoded.size() && pixels.size() < expected_pixels) {
        const uint8_t control = encoded[i++];
        if (control == 0xFF) {
            if (i >= encoded.size())
                break;
            const size_t literal = encoded[i++];
            for (size_t n = 0; n < literal && i < encoded.size(); ++n)
                pixels.push_back(encoded[i++]);
        } else {
            if (i >= encoded.size())
                break;
            const uint8_t value = encoded[i++];
            const size_t run = std::min<size_t>(size_t(control) + 1, expected_pixels - pixels.size());
            pixels.insert(pixels.end(), run, value);
        }
    }

    return pixels;
}

// ---------------------------------------------------------------------------
// .goo (Elegoo Mars family), from libslic3r/src/libslic3r/Format/GooSLA.cpp GooSLARasterEncoder
// ---------------------------------------------------------------------------
// First byte: 0x55 (magic). Runs: each run starts with a type byte (high nibble = type, low nibble
// = length bits). Last byte: checksum (sum of all preceding bytes mod 256).
//
// Types:
//   0x0: black (0x00), run_len = low_nibble (1-15)
//   0x1: black, run_len = (low_nibble<<8) | next_byte (16-4095)
//   0x2: black, run_len = (low_nibble<<16) | next_byte<<8 | next_byte (4096-1048575)
//   0x3: black, run_len = (low_nibble<<24) | ... 4 bytes total
//   0x4: gray, run_len = low_nibble (1-15), followed by gray_val byte (1-14)
//   0x5: gray, run_len = (low_nibble<<8)|next_byte, followed by gray_val byte
//   0x6: gray, run_len = 3 bytes, followed by gray_val byte
//   0x7: gray, run_len = 4 bytes, followed by gray_val byte
//   0xC: white (0xFF), run_len = low_nibble (1-15)
//   0xD: white, run_len = (low_nibble<<8)|next_byte
//   0xE: white, run_len = 3 bytes
//   0xF: white, run_len = 4 bytes
//
// Gray values 1-14 map to pixel values 16,32,...,224 (gray_val * 16)
inline std::vector<uint8_t> decode_goo_layer(const std::vector<uint8_t>& encoded, size_t expected_pixels)
{
    std::vector<uint8_t> pixels;
    pixels.reserve(expected_pixels);

    size_t i = 0;
    // Skip leading 0x55 if present
    if (!encoded.empty() && encoded[0] == 0x55) i = 1;

    // Last byte is checksum, don't process it
    size_t end = encoded.size();
    if (end > i) end -= 1;

    while (i < end && pixels.size() < expected_pixels) {
        uint8_t byte = encoded[i++];
        uint8_t type = byte >> 4;
        uint8_t len_low = byte & 0x0F;

        uint32_t run_len = 0;
        uint8_t pixel_val = 0;

        if (type == 0x0 || type == 0xC) { // black or white, len <= 15
            run_len = len_low;
            pixel_val = (type == 0x0) ? 0x00 : 0xFF;
        } else if (type == 0x1 || type == 0xD) { // black or white, len <= 4095
            if (i >= end) break;
            run_len = (len_low << 8) | encoded[i++];
            pixel_val = (type == 0x1) ? 0x00 : 0xFF;
        } else if (type == 0x2 || type == 0xE) { // black or white, len <= 1M
            if (i + 1 >= end) break;
            run_len = (uint32_t(len_low) << 16) | (uint32_t(encoded[i]) << 8) | encoded[i + 1];
            i += 2;
            pixel_val = (type == 0x2) ? 0x00 : 0xFF;
        } else if (type == 0x3 || type == 0xF) { // black or white, len > 1M
            if (i + 2 >= end) break;
            run_len = (uint32_t(len_low) << 24) | (uint32_t(encoded[i]) << 16) | (uint32_t(encoded[i + 1]) << 8) | encoded[i + 2];
            i += 3;
            pixel_val = (type == 0x3) ? 0x00 : 0xFF;
        } else if (type == 0x4) { // gray, len <= 15
            run_len = len_low;
            if (i >= end) break;
            uint8_t gray_val = encoded[i++]; // 1-14
            pixel_val = gray_val * 16;
        } else if (type == 0x5) { // gray, len <= 4095
            if (i >= end) break;
            run_len = (len_low << 8) | encoded[i++];
            if (i >= end) break;
            uint8_t gray_val = encoded[i++];
            pixel_val = gray_val * 16;
        } else if (type == 0x6) { // gray, len <= 1M
            if (i + 1 >= end) break;
            run_len = (uint32_t(len_low) << 16) | (uint32_t(encoded[i]) << 8) | encoded[i + 1];
            i += 2;
            if (i >= end) break;
            uint8_t gray_val = encoded[i++];
            pixel_val = gray_val * 16;
        } else if (type == 0x7) { // gray, len > 1M
            if (i + 2 >= end) break;
            run_len = (uint32_t(len_low) << 24) | (uint32_t(encoded[i]) << 16) | (uint32_t(encoded[i + 1]) << 8) | encoded[i + 2];
            i += 3;
            if (i >= end) break;
            uint8_t gray_val = encoded[i++];
            pixel_val = gray_val * 16;
        } else {
            // Unknown type, skip
            continue;
        }

        // Clamp to expected pixels
        if (pixels.size() + run_len > expected_pixels) {
            run_len = expected_pixels - pixels.size();
        }
        pixels.insert(pixels.end(), run_len, pixel_val);
    }

    return pixels;
}

// ---------------------------------------------------------------------------
// Anycubic PW0 (.pwmo/.pwmx/.pwms, .pm5), from Format/AnycubicSLA.cpp AnycubicSLARasterEncoder
// ---------------------------------------------------------------------------
// Each run: high nibble = pixel value (0-15), low nibble = run length.
// For pixel value 0 (black) or 15 (white): run length = ((byte & 0x0F) << 8) | next_byte
// For other pixel values: run length = byte & 0x0F (1-15)
// Pixel values 0-15 map to 0-255 by *17
inline std::vector<uint8_t> decode_pw0_layer(const std::vector<uint8_t>& encoded, size_t expected_pixels)
{
    std::vector<uint8_t> pixels;
    pixels.reserve(expected_pixels);

    size_t i = 0;
    while (i < encoded.size() && pixels.size() < expected_pixels) {
        uint8_t byte = encoded[i++];
        uint8_t pixel_val = byte >> 4;
        uint8_t len_low = byte & 0x0F;

        uint32_t run_len = 0;
        if (pixel_val == 0 || pixel_val == 0xF) {
            if (i >= encoded.size()) break;
            run_len = (len_low << 8) | encoded[i++];
        } else {
            run_len = len_low;
        }

        uint8_t gray = pixel_val * 17; // 0->0, 15->255
        if (pixels.size() + run_len > expected_pixels) {
            run_len = expected_pixels - pixels.size();
        }
        pixels.insert(pixels.end(), run_len, gray);
    }

    return pixels;
}

} // namespace Slic3r::Test
