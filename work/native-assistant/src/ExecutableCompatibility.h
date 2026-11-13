#pragma once

#include "JobProbe.h"
#include <windows.h>
#include <wincrypt.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace aa::compatibility {
struct MapAddresses {uintptr_t draw, material, pack, packCall;};
using JobAddresses=aa::jobprobe::Addresses;

#ifndef AA_GAME_ATS
struct Fingerprint {
    const unsigned char* prefix;
    size_t prefixSize;
    size_t size;
    const uint16_t* relocations;
    size_t relocationCount;
    std::array<unsigned char,32> digest;
};

inline bool readImage(uintptr_t address,void* output,size_t size) {
    SIZE_T read=0;
    return address && size<=UINTPTR_MAX-address &&
        ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),output,size,&read) && read==size;
}

class CodeSection {
    uintptr_t start=0;
    std::vector<unsigned char> bytes;
public:
    explicit CodeSection(void* image) {
        const auto base=reinterpret_cast<uintptr_t>(image);
        IMAGE_DOS_HEADER dos{};
        IMAGE_NT_HEADERS64 nt{};
        if(!readImage(base,&dos,sizeof dos) || dos.e_magic!=IMAGE_DOS_SIGNATURE ||
           dos.e_lfanew<sizeof dos || dos.e_lfanew>65536 ||
           !readImage(base+dos.e_lfanew,&nt,sizeof nt) || nt.Signature!=IMAGE_NT_SIGNATURE ||
           nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64 || nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
           nt.FileHeader.SizeOfOptionalHeader!=sizeof nt.OptionalHeader || nt.FileHeader.NumberOfSections>96)return;
        const auto table=base+dos.e_lfanew+sizeof nt;
        for(unsigned i=0;i<nt.FileHeader.NumberOfSections;++i) {
            IMAGE_SECTION_HEADER section{};
            if(!readImage(table+i*sizeof section,&section,sizeof section))return;
            if(std::memcmp(section.Name,".text\0\0",8)!=0)continue;
            const auto size=section.Misc.VirtualSize;
            const auto imageSize=nt.OptionalHeader.SizeOfImage;
            if(!(section.Characteristics&IMAGE_SCN_MEM_EXECUTE) || !size || size>128*1024*1024 ||
               section.VirtualAddress>=imageSize || size>imageSize-section.VirtualAddress)return;
            bytes.resize(size);
            if(!readImage(base+section.VirtualAddress,bytes.data(),size)){bytes.clear();return;}
            start=base+section.VirtualAddress;
            return;
        }
    }

    // The fingerprint covers the function body. Only the explicitly listed
    // PC-relative addresses may vary between builds.
    uintptr_t find(const Fingerprint& rule) const {
        if(bytes.size()<rule.size || !rule.prefixSize || rule.prefixSize>rule.size)return 0;
        uintptr_t match=0;
        for(size_t offset=0;offset<=bytes.size()-rule.size;) {
            const auto* candidate=static_cast<const unsigned char*>(
                std::memchr(bytes.data()+offset,rule.prefix[0],bytes.size()-rule.size-offset+1));
            if(!candidate)break;
            offset=static_cast<size_t>(candidate-bytes.data())+1;
            if(std::memcmp(candidate,rule.prefix,rule.prefixSize)!=0)continue;
            std::vector<unsigned char> normalized(candidate,candidate+rule.size);
            for(size_t i=0;i<rule.relocationCount;++i) {
                if(size_t(rule.relocations[i])+4>rule.size)return 0;
                std::fill_n(normalized.data()+rule.relocations[i],4,0);
            }
            HCRYPTPROV provider=0;
            HCRYPTHASH hash=0;
            if(!CryptAcquireContextW(&provider,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT))return 0;
            std::array<unsigned char,32> digest{};
            DWORD size=static_cast<DWORD>(digest.size());
            const bool valid=CryptCreateHash(provider,CALG_SHA_256,0,0,&hash) &&
                CryptHashData(hash,normalized.data(),static_cast<DWORD>(normalized.size()),0) &&
                CryptGetHashParam(hash,HP_HASHVAL,digest.data(),&size,0) && size==digest.size();
            if(hash)CryptDestroyHash(hash);
            CryptReleaseContext(provider,0);
            if(!valid)return 0;
            if(digest!=rule.digest)continue;
            if(match)return 0;
            match=start+offset-1;
        }
        return match;
    }

    uintptr_t callTarget(uintptr_t address) const {
        if(address<start || address-start>bytes.size() || bytes.size()-(address-start)<5)return 0;
        const auto* instruction=bytes.data()+(address-start);
        if(*instruction!=0xe8)return 0;
        int32_t displacement=0;
        std::memcpy(&displacement,instruction+1,sizeof displacement);
        const auto target=int64_t(address-start)+5+displacement;
        return target>=0 && uint64_t(target)<bytes.size()?start+target:0;
    }
};

#endif
}

#ifndef AA_GAME_ATS
#include "ETS2CompatibilityData.h"
#endif
