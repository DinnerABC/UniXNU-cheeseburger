// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "NativeMetal540Render.h"

// Fixed actual 21H461 SpriteKit SolidColor AIR pair, not a system device.
// Runs once after the existing regressions. All seven mappings stay connection
// owned on failure; no import/free/retry or CPU expected-pixel upload.
class NativeMetal540RenderAIRProbe {
public:
    enum Stage : uint32_t {Begin=1,Query,Device,Allocate,MapGPU,MapCPU,Encode,Upload,Submit,Fence,Readback,Complete};
    enum {Invalid=-5480,Mismatch=-5481,AlreadyAttempted=-5482,Prerequisite=-5483};
    struct Record {
        Stage stage=Begin;int error=0;uint32_t resource=0,ibWords=0;
        uint64_t handle=0,iova=0,fence=0;
        Native540Render::Pixels pixels{};
        uint32_t clearTimestamp=0,colorTimestamp=0,ucheTimestamp=0;
        uint32_t inputVerified=0,inputGuardVerified=0,colorVerified=0,colorGuardVerified=0;
    };
    struct Transport {
        NativeMetal540Client::Call call=nullptr;
        int (*mapView)(void *,uint32_t,uint64_t &,uint64_t &)=nullptr;
        void (*order)(void *,bool)=nullptr;
        void (*record)(void *,const Record &)=nullptr;
        void *context=nullptr;
    };
    explicit NativeMetal540RenderAIRProbe(const Transport &t):transport_(t) {}
    int run(bool allPrerequisitesSucceeded);
    const Record &result()const{return record_;}
private:
    struct Bo{uint64_t handle=0,iova=0,address=0,bytes=0;uint32_t token=0;};
    Transport transport_;Record record_{};Bo bos_[7]{};bool attempted_=false;
    void stage(Stage,uint32_t=0);
    int fail(int);
    int request(uint32_t,const void *,size_t,void *,size_t);
};
