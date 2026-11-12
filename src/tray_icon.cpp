#include "tray_icon.h"
#include <algorithm>
#include <cstring>

namespace eotf {
TrayRaster RasterizeTrayIcon(TransferCurve curve, int size) {
    if (!ValidCurve(curve) || size < 16 || size > 128) throw Error("Invalid tray icon parameters");
    // Render coverage at 8x, then center the visible ink rather than the font's line box.
    const int scale = 8, canvas = size * scale * 3, border = scale * 2;
    HDC dc = CreateCompatibleDC(nullptr); if (!dc) throw Error("Create icon DC failed");
    BITMAPINFO info = {}; info.bmiHeader = {sizeof(BITMAPINFOHEADER),canvas,-canvas,1,32,BI_RGB,0,0,0,0,0};
    void* raw = nullptr; HBITMAP bitmap = CreateDIBSection(dc,&info,DIB_RGB_COLORS,&raw,nullptr,0);
    if (!bitmap) { DeleteDC(dc); throw Error("Create icon bitmap failed"); }
    auto previous = SelectObject(dc,bitmap); ZeroMemory(raw,(size_t)canvas*canvas*4);
    HFONT font = CreateFontW(-size*scale*3/4,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    auto previousFont = SelectObject(dc,font); SetTextColor(dc,RGB(255,255,255)); SetBkMode(dc,TRANSPARENT);
    TextOutW(dc,border,border,CurveLabel(curve),(int)wcslen(CurveLabel(curve))); GdiFlush();
    const auto source = (const BYTE*)raw;
    RECT bounds{canvas,canvas,0,0};
    for (int y=0;y<canvas;++y) for (int x=0;x<canvas;++x) if (source[((size_t)y*canvas+x)*4]) {
        bounds.left=std::min<LONG>(bounds.left,x); bounds.top=std::min<LONG>(bounds.top,y);
        bounds.right=std::max<LONG>(bounds.right,x+1); bounds.bottom=std::max<LONG>(bounds.bottom,y+1);
    }
    const int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
    if (width<=0||height<=0) { SelectObject(dc,previousFont);DeleteObject(font);SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);throw Error("Empty icon text"); }
    // Fit sRGB's four letters without shrinking the other labels unnecessarily.
    double factor=std::min(1.0,std::min((double)(size-2)*scale/width,(double)(size-2)*scale/height));
    const int w=std::max(1,(int)(width*factor/scale+.5)),h=std::max(1,(int)(height*factor/scale+.5));
    const int left=(size-w)/2,top=(size-h)/2;
    TrayRaster raster{size,Bytes((size_t)size*size*4),{left,top,left+w,top+h}};
    const BYTE background[3] = {(BYTE)(curve==TransferCurve::SRGB?102:96),(BYTE)(curve==TransferCurve::SRGB?87:132),(BYTE)(curve==TransferCurve::SRGB?77:24)};
    for(int y=0;y<size;++y)for(int x=0;x<size;++x){
        unsigned coverage=0;
        if(x>=left&&x<left+w&&y>=top&&y<top+h){
            // Box filtering preserves the glyph strokes at small taskbar sizes.
            for(int sy=0;sy<scale;++sy)for(int sx=0;sx<scale;++sx){
                int px=bounds.left+((x-left)*scale+sx)*width/(w*scale);
                int py=bounds.top+((y-top)*scale+sy)*height/(h*scale);
                coverage+=source[((size_t)py*canvas+px)*4];
            }
            coverage=(coverage+scale*scale/2)/(scale*scale);
        }
        auto pixel=raster.pixels.data()+((size_t)y*size+x)*4;
        for(int k=0;k<3;++k)pixel[k]=(BYTE)(background[k]+((255-background[k])*coverage+127)/255);
        pixel[3]=255;
    }
    SelectObject(dc,previousFont);DeleteObject(font);SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);
    return raster;
}
HICON MakeTrayIcon(TransferCurve curve,int size) {
    auto raster=RasterizeTrayIcon(curve,size);
    BITMAPINFO info={};info.bmiHeader={sizeof(BITMAPINFOHEADER),size,-size,1,32,BI_RGB,0,0,0,0,0};
    void* raw=nullptr;HBITMAP color=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&raw,nullptr,0);
    HBITMAP mask=CreateBitmap(size,size,1,1,nullptr);
    if(!color||!mask){if(color)DeleteObject(color);if(mask)DeleteObject(mask);throw Error("Create tray icon failed");}
    memcpy(raw,raster.pixels.data(),raster.pixels.size());
    HDC dc=CreateCompatibleDC(nullptr);auto previous=SelectObject(dc,mask);PatBlt(dc,0,0,size,size,BLACKNESS);SelectObject(dc,previous);DeleteDC(dc);
    ICONINFO ii{TRUE,0,0,mask,color};HICON result=CreateIconIndirect(&ii);DeleteObject(mask);DeleteObject(color);
    if(!result)throw Error("Create tray icon failed");return result;
}
}
