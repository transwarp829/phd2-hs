#ifndef STAR_PROFILE_IMAGE_H_INCLUDED
#define STAR_PROFILE_IMAGE_H_INCLUDED

#include <wx/image.h>

// Keep the source crop unchanged: repainting or toggling raw mode can happen
// without another camera frame (and therefore without rebuilding the crop).
inline wxImage ScaleStarProfileCrop(const wxImage& crop, int width, bool rawMode)
{
    return crop.Scale(width, width, rawMode ? wxIMAGE_QUALITY_NEAREST : wxIMAGE_QUALITY_HIGH);
}

#endif
