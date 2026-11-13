#include "../src/tray_icon.h"
#include <iostream>
#include <algorithm>
#include <cstring>
using namespace eotf;
int main() {
    try {
        constexpr int width=640,height=560;
        Bytes sheet((size_t)width*height*4,255);
        int row=0;
        for(int size:{16,20,24,32}) {
            for(int mode=0;mode<5;++mode) {
                auto raster=mode==4?RasterizeSdrTrayIcon(size):RasterizeTrayIcon((TransferCurve)mode,size);
                if(mode==4){
                    auto srgb=RasterizeTrayIcon(TransferCurve::SRGB,size);
                    if(memcmp(raster.pixels.data(),srgb.pixels.data(),4)||raster.pixels==srgb.pixels)
                        throw Error("SDR and sRGB icons must have matching backgrounds and different labels");
                }
                RECT ink{size,size,0,0};const auto bg=raster.pixels.data();
                for(int y=0;y<size;++y)for(int x=0;x<size;++x){auto p=raster.pixels.data()+((size_t)y*size+x)*4;
                    if(p[0]!=bg[0]||p[1]!=bg[1]||p[2]!=bg[2]){ink.left=std::min<LONG>(ink.left,x);ink.top=std::min<LONG>(ink.top,y);ink.right=std::max<LONG>(ink.right,x+1);ink.bottom=std::max<LONG>(ink.bottom,y+1);}}
                std::cout<<"size="<<size<<" mode="<<mode<<" ink="<<ink.left<<","<<ink.top<<","<<ink.right<<","<<ink.bottom<<"\n";
                if(abs(ink.left+ink.right-size)>1||abs(ink.top+ink.bottom-size)>1||ink.left<1||ink.top<1||ink.right>=size||ink.bottom>=size)throw Error("Visible icon ink is not centered or is clipped");
                int factor=96/size,offsetX=mode*128+(128-size*factor)/2,offsetY=row*140+12;
                for(int y=0;y<size*factor;++y)for(int x=0;x<size*factor;++x)memcpy(sheet.data()+((size_t)(y+offsetY)*width+x+offsetX)*4,raster.pixels.data()+((size_t)(y/factor)*size+x/factor)*4,4);
                // Native size below the magnified image, for legibility inspection.
                for(int y=0;y<size;++y)for(int x=0;x<size;++x)memcpy(sheet.data()+((size_t)(row*140+108+y)*width+mode*128+(128-size)/2+x)*4,raster.pixels.data()+((size_t)y*size+x)*4,4);
                auto icon=mode==4?MakeSdrTrayIcon(size):MakeTrayIcon((TransferCurve)mode,size);DestroyIcon(icon);
            }
            ++row;
        }
        BITMAPFILEHEADER file={0x4d42,(DWORD)(sizeof(BITMAPFILEHEADER)+sizeof(BITMAPINFOHEADER)+sheet.size()),0,0,sizeof(BITMAPFILEHEADER)+sizeof(BITMAPINFOHEADER)};
        BITMAPINFOHEADER info={sizeof(info),width,-height,1,32,BI_RGB,(DWORD)sheet.size(),0,0,0,0};
        std::string out((char*)&file,sizeof(file));out.append((char*)&info,sizeof(info));out.append((char*)sheet.data(),sheet.size());WriteText(L"build\\tray-preview.bmp",out);
        std::cout<<"PASS: all five labels centered at 16, 20, 24 and 32 pixels; distinct sRGB/SDR labels with the same gray background; preview saved.\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
