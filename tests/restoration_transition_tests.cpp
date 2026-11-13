#include "../src/restoration_cover.h"
#include <iostream>
#include <climits>
using namespace eotf;
static void Require(bool value,const char* message){if(!value)throw Error(message);}
struct Visual final : RestorationVisual {
    std::vector<int>& calls;
    bool available=true,active=false,throwShow=false,throwReveal=false;
    explicit Visual(std::vector<int>& events):calls(events){}
    bool Show() override { calls.push_back(1);active=true;if(throwShow)throw Error("Prepare failure");active=available;return available; }
    bool Active() const override{return active;}
    void PrepareReveal() override{Require(active,"Reveal prepared without a live cover");calls.push_back(5);if(throwReveal)throw Error("Reveal failure");}
    void Release() noexcept override{calls.push_back(3);active=false;}
};
int main(){
    try{
        std::vector<int> calls;Visual visual(calls);
        RestoreWithVisual(visual,[&](){Require(visual.Active(),"Recovery started before the old image was visible");calls.push_back(2);},[&](){calls.push_back(4);});
        Require(calls==std::vector<int>({1,2,5,3})&&!visual.Active(),"Successful cover was released before restoration/reveal composition or fallback ran");
        calls.clear();visual.throwReveal=true;
        RestoreWithVisual(visual,[&](){calls.push_back(2);},[&](){Require(!visual.Active(),"Failed reveal did not release its cover");calls.push_back(4);});
        Require(calls==std::vector<int>({1,2,5,3,4})&&!visual.Active(),"Reveal failure blocked restoration or skipped fallback");
        visual.throwReveal=false;
        calls.clear();visual.available=false;
        RestoreWithVisual(visual,[&](){calls.push_back(2);},[&](){Require(!visual.Active(),"Fallback ran behind a stale cover");calls.push_back(4);});
        Require(calls==std::vector<int>({1,2,3,4}),"Capture failure blocked restoration or skipped legacy refresh");
        calls.clear();visual.available=true;
        RestoreWithVisual(visual,[&](){calls.push_back(2);visual.active=false;},[&](){calls.push_back(4);});
        Require(calls==std::vector<int>({1,2,3,4}),"Expired cover did not fall back");
        calls.clear();bool threw=false;
        try{RestoreWithVisual(visual,[&](){calls.push_back(2);throw Error("Hook restoration failure");},[&](){calls.push_back(4);});}catch(const Error&){threw=true;}
        Require(threw&&calls==std::vector<int>({1,2,3})&&!visual.Active(),"Failed restoration left a frozen image or hid the error");
        calls.clear();visual.throwShow=true;
        RestoreWithVisual(visual,[&](){Require(!visual.Active(),"Partial preparation was not released before recovery");calls.push_back(2);},[&](){calls.push_back(4);});
        Require(calls==std::vector<int>({1,3,2,4})&&!visual.Active(),"Preparation exception blocked recovery or leaked its image");
        RECT bounds={-3840,0,0,2160};SnapshotDescription image={3840,2160,true,true,false,true};
        Require(UsableRestorationSnapshot(bounds,image),"Physical negative-origin HDR snapshot rejected");
        image.desktopUpdated=false;Require(!UsableRestorationSnapshot(bounds,image),"Pointer-only startup frame accepted as a desktop image");image.desktopUpdated=true;
        image.protectedContent=true;Require(!UsableRestorationSnapshot(bounds,image),"Protected blacked-out snapshot accepted");image.protectedContent=false;
        image.linearFp16=false;Require(!UsableRestorationSnapshot(bounds,image),"Tone-mapped SDR snapshot accepted as unchanged HDR");image.linearFp16=true;
        image.unrotated=false;Require(!UsableRestorationSnapshot(bounds,image),"Unconverted rotated snapshot accepted");image.unrotated=true;
        image.width=1920;Require(!UsableRestorationSnapshot(bounds,image),"Wrong-resolution snapshot accepted");
        RECT invalid={LONG_MAX,0,LONG_MIN,2160};Require(!UsableRestorationSnapshot(invalid,image),"Invalid/overflowing output bounds accepted");
        RECT oversized={LONG_MIN,0,LONG_MAX,2160};image.width=UINT_MAX;
        Require(!UsableRestorationSnapshot(oversized,image),"Dimensions too large for native window coordinates accepted");
        Require(RestorationCopyFits(3840,2160,{0,0},3840,2160),"Exact bitmap copy rejected");
        Require(RestorationCopyFits(3840,2160,{32,64},3872,2224),"Nonzero atlas offset rejected");
        Require(!RestorationCopyFits(3840,2160,{-1,64},3872,2224),"Negative atlas offset accepted");
        Require(!RestorationCopyFits(3840,2160,{32,64},3840,2160),"Out-of-bounds atlas copy accepted");
        Require(!RestorationCopyFits(UINT_MAX,2160,{32,0},UINT_MAX,2160),"Overflowing atlas copy accepted");
        Require(!RestorationCopyFits(0,2160,{0,0},3840,2160),"Empty copy accepted");
        IsolationConfig config={};config.protocol=IsolationProtocol;config.count=3;
        config.outputs[0]={{0,0,3840,2160},{},0,TRUE,TRUE,L"HDR"};
        config.outputs[1]={{-1920,0,0,1080},{},1,FALSE,FALSE,L"SDR"};
        config.outputs[2]={{3840,0,5760,1080},{},2,TRUE,FALSE,L"Prior HDR target"};
        IsolationStats stats={};stats.replacements[0]=12;stats.replacements[1]=99;stats.replacements[2]=4;
        Require(RestorationCoverMask(config,stats)==5,"Cover selected SDR or omitted a previously converted HDR output");
        stats.replacements[2]=0;Require(RestorationCoverMask(config,stats)==1,"Unmodified output was covered");
        config.count=MaxOutputs+1;Require(!RestorationCoverMask(config,stats),"Invalid topology was covered");
        std::cout<<"PASS: hold/restore/compose/reveal ordering; capture/reveal failure, expiry and exception cleanup; pointer-only/protected/SDR/rotated/wrong-size rejection; atlas bounds and HDR-only ownership. CPU-only: no window, capture, rendering, DWM injection or display setting calls.\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<"\n";return 1;}
}
