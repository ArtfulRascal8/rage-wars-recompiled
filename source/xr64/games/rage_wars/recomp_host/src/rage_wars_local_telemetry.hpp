#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif
namespace xr64::rage_wars::recomp {
inline bool local_state_sample_enabled() {
    static const bool enabled=[]{const char* v=std::getenv("XR64_LOCAL_STATE_SAMPLE");return v && v[0]=='1' && v[1]==0;}();
    return enabled;
}
// Optional metadata-only publication for a normal owner play session. One
// presentation owner writes a bounded latest record; readers never touch RAM,
// input queues, game clocks, audio or user files. No history queue or disk I/O.
class LocalControlTelemetry {
#ifdef _WIN32
    struct Shared {alignas(8) volatile LONG64 sequence=0;DWORD bytes=0,magic=0;char text[2048]{};};
    HANDLE mapping_=nullptr;Shared* shared_=nullptr;
public:
    LocalControlTelemetry() {
        if(!local_state_sample_enabled())return;
        wchar_t name[96]{};swprintf_s(name,L"Local\\XR64_RageWars_LocalControls_%lu",GetCurrentProcessId());
        mapping_=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Shared),name);
        if(mapping_)shared_=static_cast<Shared*>(MapViewOfFile(mapping_,FILE_MAP_WRITE,0,0,sizeof(Shared)));
    }
    ~LocalControlTelemetry() {if(shared_)UnmapViewOfFile(shared_);if(mapping_)CloseHandle(mapping_);}
    void publish(const char* text,std::size_t bytes) {
        if(!shared_ || bytes>=sizeof(shared_->text))return;
        InterlockedIncrement64(&shared_->sequence);
        std::memcpy(shared_->text,text,bytes);shared_->text[bytes]=0;
        shared_->bytes=static_cast<DWORD>(bytes);shared_->magic=0x3157434c;
        InterlockedIncrement64(&shared_->sequence);
    }
#else
public:
    void publish(const char*,std::size_t) {}
#endif
};
}
