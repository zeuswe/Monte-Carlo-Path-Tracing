#include "csrt/utils/image_io.hpp"

#include <algorithm>
#include <array>
#include <set>

#include <cmath>
#include <cstdio>
#include <tinyexr.h>
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
extern "C"
{
#include "stb_image.h"
#include "stb_image_resize2.h"
#include "stb_image_write.h"
}

#include "csrt/utils/misc.hpp"

namespace csrt
{

void image_io::Write(const float *data, const int width, const int height,
                     std::string filename)
{
    const uint32_t num_element = static_cast<uint32_t>(width) * height * 3;
    unsigned char *color = new unsigned char[num_element];
    for (uint32_t i = 0; i < num_element; ++i)
    {
        const float value =
            data[i] <= 0.0031308f
                ? (12.92f * data[i])
                : (1.055f * powf(data[i], 1.0f / 2.4f) - 0.055f);
        color[i] = static_cast<unsigned char>(
            static_cast<int>(value > 1.0f ? 255.0f : value * 255.0f));
    }

    int ret =
        stbi_write_png(filename.c_str(), width, height, 3, color, width * 3);
    delete[] color;
    color = nullptr;
    if (ret == 0)
    {
        fprintf(stderr, "[error] write image failed.\n");
    }
    else
    {
        fprintf(stderr, "[info] save result as image \"%s\".\n",
                filename.c_str());
    }
}

float *image_io::Read(const std::string &filename, const float gamma,
                      const int *width_max, int *width, int *height,
                      int *channel)
{
    const std::set<std::string> support_format = {
        "EXR", "exr", "jpg", "jpeg", "JPG", "JPEG", "png", "PNG",
        "tga", "TGA", "bmp", "BMP",  "psd", "PSD",  "gif", "GIF",
        "hdr", "HDR", "pic", "PIC",  "pgm", "PGM",  "ppm", "PPM"};
    std::string suffix = GetSuffix(filename);
    if (!support_format.count(suffix))
    {
        fprintf(stderr, "[error] unsupport input image format for image '%s\n'",
                filename.c_str());
        exit(1);
    }

    float *data = nullptr;

    switch (Hash(suffix.c_str()))
    {
    case "exr"_hash:
    case "EXR"_hash:
    {
        const char *err = nullptr;
        if (LoadEXR(&data, width, height, filename.c_str(), &err) !=
            TINYEXR_SUCCESS)
        {
            fprintf(stderr, "[error] load image '%s' failed.",
                    filename.c_str());
            if (err)
            {
                fprintf(stderr, "\t%s", err);
                FreeEXRErrorMessage(err);
            }
            exit(1);
        }
        if (gamma != 0.0f)
        {
            int num_component = *width * *height * *channel;
            for (int i = 0; i < num_component; ++i)
                data[i] = std::pow(data[i], gamma);
        }
        *channel = 4;
        break;
    }
    case "HDR"_hash:
    case "hdr"_hash:
    {
        data = stbi_loadf(filename.c_str(), width, height, channel, 0);
        if (data == nullptr)
        {
            fprintf(stderr, "[error] load image '%s' failed.",
                    filename.c_str());
            exit(1);
        }
        int num_component = *width * *height * *channel;
        if (gamma == -1.0f)
        {
            for (int i = 0; i < num_component; ++i)
            {
                data[i] = data[i] <= 0.04045f
                              ? data[i] / 12.92f
                              : std::pow((data[i] + 0.055f) / 1.055f, 2.4f);
            }
        }
        else if (gamma != 0.0f)
        {
            for (int i = 0; i < num_component; ++i)
                data[i] = std::pow(data[i], gamma);
        }
        break;
    }
    default:
    {
        stbi_uc *raw_data_uc =
            stbi_load(filename.c_str(), width, height, channel, 0);
        if (raw_data_uc == nullptr)
        {
            fprintf(stderr, "[error] load image '%s' failed.",
                    filename.c_str());
            exit(1);
        }
        int num_component = *width * *height * *channel;
        data = new float[num_component];
        for (int i = 0; i < num_component; ++i)
            data[i] = static_cast<int>(raw_data_uc[i]) / 255.0f;
        if (gamma == 0.0f || gamma == -1.0f)
        {
            for (int i = 0; i < num_component; ++i)
            {
                data[i] = data[i] <= 0.04045f
                              ? data[i] / 12.92f
                              : std::pow((data[i] + 0.055f) / 1.055f, 2.4f);
            }
        }
        else
        {
            for (int i = 0; i < num_component; ++i)
                data[i] = std::pow(data[i], gamma);
        }
        stbi_image_free(raw_data_uc);
        break;
    }
    }

    if (width_max != nullptr && *width > *width_max)
    {
        int height_target = *width_max * *height / *width;
        float *target_data = new float[*width_max * height_target * *channel];
        Resize(data, *width, *height, 0, target_data, *width_max, height_target,
               0, *channel);
        data = target_data;
        *width = *width_max;
        *height = height_target;
    }

    return data;
}

void image_io::Resize(const float *input_pixels, int input_w, int input_h,
                      int input_stride_in_bytes, float *output_pixels,
                      int output_w, int output_h, int output_stride_in_bytes,
                      int num_channels)
{
    stbir_resize_float_linear(input_pixels, input_w, input_h,
                              input_stride_in_bytes, output_pixels, output_w,
                              output_h, output_stride_in_bytes,
                              static_cast<stbir_pixel_layout>(num_channels));
}

} // namespace csrt