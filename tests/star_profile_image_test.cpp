#include "star_profile_image.h"

#include <cstring>
#include <iostream>
#include <stdexcept>

static void Require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

static bool SamePixels(const wxImage& a, const wxImage& b)
{
    return a.GetSize() == b.GetSize() && std::memcmp(a.GetData(), b.GetData(), a.GetWidth() * a.GetHeight() * 3) == 0;
}

int main()
{
    try
    {
        // Sharp edges make interpolation distinguishable from real sensor pixels.
        wxImage crop(2, 2);
        const unsigned char pixels[] = { 0, 0, 0, 255, 255, 255, 255, 255, 255, 0, 0, 0 };
        std::memcpy(crop.GetData(), pixels, sizeof(pixels));
        const wxImage original = crop.Copy();

        const wxImage interpolated = ScaleStarProfileCrop(crop, 16, false);
        Require(SamePixels(crop, original), "interpolated paint changed the source crop");
        bool hasInterpolatedPixel = false;
        for (int i = 0; i < 16 * 16 * 3; ++i)
            hasInterpolatedPixel |= interpolated.GetData()[i] != 0 && interpolated.GetData()[i] != 255;
        Require(hasInterpolatedPixel, "fixture did not exercise interpolation");

        // No new frame between paints: this is the stopped-capture raw toggle.
        const wxImage raw = ScaleStarProfileCrop(crop, 16, true);
        for (int y = 0; y < 16; ++y)
        {
            for (int x = 0; x < 16; ++x)
            {
                const unsigned char expected = ((x / 8) != (y / 8)) ? 255 : 0;
                Require(raw.GetRed(x, y) == expected, "raw view contains previously interpolated pixels");
            }
        }
        Require(SamePixels(crop, original), "raw paint changed the source crop");

        // Resizing and repeated mode changes must always start from original pixels.
        for (int width : { 7, 32, 3, 16 })
        {
            ScaleStarProfileCrop(crop, width, false);
            ScaleStarProfileCrop(crop, width, true);
        }
        Require(SamePixels(crop, original), "repeated paints degraded the source crop");
        Require(SamePixels(ScaleStarProfileCrop(crop, 16, true), raw), "raw output depends on paint history");
        Require(SamePixels(ScaleStarProfileCrop(crop, 16, false), interpolated),
                "interpolated output depends on paint history");
        std::cout << "star profile image tests passed\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
