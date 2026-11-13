#pragma once
#include "core.h"
namespace eotf {
struct TrayRaster { int size; Bytes pixels; RECT ink; };
TrayRaster RasterizeTrayIcon(TransferCurve curve, int size);
HICON MakeTrayIcon(TransferCurve curve, int size);
TrayRaster RasterizeSdrTrayIcon(int size);
HICON MakeSdrTrayIcon(int size);
}
