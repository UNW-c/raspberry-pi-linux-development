#include "driver/jpeg_codec.h"

#include <jpeglib.h>

#include <csetjmp>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace prts::driver {
namespace {

struct ErrorHandler {
    jpeg_error_mgr base;
    std::jmp_buf jump;
    char message[JMSG_LENGTH_MAX];
};

void onFatalError(j_common_ptr info) {
    auto* handler = reinterpret_cast<ErrorHandler*>(info->err);
    (*info->err->format_message)(info, handler->message);
    std::longjmp(handler->jump, 1);
}

}  // namespace

bool jpegSize(const std::vector<std::uint8_t>& data, int& width, int& height) {
    width = 0;
    height = 0;
    if (data.size() < 4 || data[0] != 0xFF || data[1] != 0xD8) {
        return false;
    }
    std::size_t index = 2;
    while (index + 9 < data.size()) {
        if (data[index] != 0xFF) {
            ++index;
            continue;
        }
        const std::uint8_t marker = data[index + 1];
        if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
            index += 2;
            continue;
        }
        const std::size_t length =
            (static_cast<std::size_t>(data[index + 2]) << 8) | data[index + 3];
        if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
            height = (static_cast<int>(data[index + 5]) << 8) | data[index + 6];
            width = (static_cast<int>(data[index + 7]) << 8) | data[index + 8];
            return width > 0 && height > 0;
        }
        index += 2 + length;
    }
    return false;
}

bool scaleJpeg(const std::vector<std::uint8_t>& input, int scaleDenom, int quality,
               std::vector<std::uint8_t>& output, int& width, int& height, std::string& error) {
    output.clear();
    width = 0;
    height = 0;

    if (input.size() < 4 || input[0] != 0xFF || input[1] != 0xD8) {
        error = "not a jpeg";
        return false;
    }
    if (scaleDenom < 1) {
        scaleDenom = 1;
    }
    if (scaleDenom != 1 && scaleDenom != 2 && scaleDenom != 4 && scaleDenom != 8) {
        error = "scale must be 1, 2, 4 or 8";
        return false;
    }
    if (quality <= 0 || quality > 100) {
        quality = 70;
    }

    // longjmp may clobber ordinary locals, so values used across it are volatile.
    volatile int denominator = scaleDenom;
    volatile int jpegQuality = quality;

    jpeg_decompress_struct decompress{};
    ErrorHandler decompressError{};
    decompress.err = jpeg_std_error(&decompressError.base);
    decompressError.base.error_exit = onFatalError;
    if (setjmp(decompressError.jump) != 0) {
        error = decompressError.message;
        jpeg_destroy_decompress(&decompress);
        return false;
    }

    jpeg_create_decompress(&decompress);
    jpeg_mem_src(&decompress, input.data(), static_cast<unsigned long>(input.size()));
    jpeg_read_header(&decompress, TRUE);

    decompress.scale_num = 1;
    decompress.scale_denom = static_cast<unsigned int>(denominator);
    decompress.out_color_space = JCS_RGB;
    decompress.dct_method = JDCT_IFAST;

    jpeg_start_decompress(&decompress);
    const int decodedWidth = static_cast<int>(decompress.output_width);
    const int decodedHeight = static_cast<int>(decompress.output_height);
    if (decodedWidth <= 0 || decodedHeight <= 0) {
        error = "decoded size is zero";
        jpeg_destroy_decompress(&decompress);
        return false;
    }

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(decodedWidth) *
                                     static_cast<std::size_t>(decodedHeight) * 3U);
    while (decompress.output_scanline < decompress.output_height) {
        JSAMPROW row = pixels.data() +
                       static_cast<std::size_t>(decompress.output_scanline) *
                           static_cast<std::size_t>(decodedWidth) * 3U;
        jpeg_read_scanlines(&decompress, &row, 1);
    }
    jpeg_finish_decompress(&decompress);
    jpeg_destroy_decompress(&decompress);

    jpeg_compress_struct compress{};
    ErrorHandler compressError{};
    compress.err = jpeg_std_error(&compressError.base);
    compressError.base.error_exit = onFatalError;
    if (setjmp(compressError.jump) != 0) {
        error = compressError.message;
        jpeg_destroy_compress(&compress);
        return false;
    }

    unsigned char* buffer = nullptr;
    unsigned long bufferSize = 0;
    jpeg_create_compress(&compress);
    jpeg_mem_dest(&compress, &buffer, &bufferSize);
    compress.image_width = static_cast<JDIMENSION>(decodedWidth);
    compress.image_height = static_cast<JDIMENSION>(decodedHeight);
    compress.input_components = 3;
    compress.in_color_space = JCS_RGB;
    jpeg_set_defaults(&compress);
    jpeg_set_quality(&compress, jpegQuality, TRUE);
    jpeg_start_compress(&compress, TRUE);
    while (compress.next_scanline < compress.image_height) {
        JSAMPROW row = pixels.data() + static_cast<std::size_t>(compress.next_scanline) *
                                          static_cast<std::size_t>(decodedWidth) * 3U;
        jpeg_write_scanlines(&compress, &row, 1);
    }
    jpeg_finish_compress(&compress);

    output.assign(buffer, buffer + bufferSize);
    std::free(buffer);
    jpeg_destroy_compress(&compress);

    width = decodedWidth;
    height = decodedHeight;
    return true;
}

}  // namespace prts::driver
