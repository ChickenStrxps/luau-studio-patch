#include <windows.h>
#include <tlhelp32.h>
#include <bcrypt.h>
#include <compressapi.h>
#include "studio_installer.h"
#include "../profiles/studio_patch.h"
#include "../profiles/studio_native_profile.h"
#include "../profiles/studio_spill_profile.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cwctype>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace StudioInstaller {
namespace {
struct Handle {
    HANDLE value=INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE h):value(h){}
    ~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    Handle(const Handle&)=delete;
    Handle& operator=(const Handle&)=delete;
};
std::runtime_error failure(const char* action){return std::runtime_error(std::string(action)+" (Windows error "+std::to_string(GetLastError())+")");}
std::string utf8(const std::wstring& s){
    if(s.empty())return {};
    int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);
    if(!n)throw failure("Cannot encode path");
    std::string out(n,0);WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),int(s.size()),out.data(),n,nullptr,nullptr);return out;
}
std::string json(const std::string& s){
    std::string out="\"";
    for(unsigned char c:s){if(c=='"'||c=='\\'){out+='\\';out+=c;}else if(c<32){char b[7];sprintf_s(b,"\\u%04x",c);out+=b;}else out+=c;}
    return out+'"';
}
std::wstring clean(std::wstring s){if(s.rfind(L"\\\\?\\",0)==0)s.erase(0,4);return s;}
std::wstring key(std::wstring s){s=clean(s);std::transform(s.begin(),s.end(),s.begin(),[](wchar_t c){return wchar_t(towlower(c));});return s;}
std::wstring finalPath(HANDLE h){
    DWORD n=GetFinalPathNameByHandleW(h,nullptr,0,FILE_NAME_NORMALIZED);
    if(!n)throw failure("Cannot resolve Studio path");
    std::wstring path(n,0);DWORD count=GetFinalPathNameByHandleW(h,path.data(),n,FILE_NAME_NORMALIZED);
    if(!count||count>=n)throw failure("Cannot resolve Studio path");path.resize(count);return path;
}
std::wstring targetPath(const std::wstring& supplied){
    std::wstring path=supplied;
    if(path.empty()){
        std::array<wchar_t,32768> command{};DWORD size=DWORD(command.size()*sizeof(wchar_t));
        if(RegGetValueW(HKEY_CLASSES_ROOT,L"Roblox.Place\\shell\\open\\command",nullptr,RRF_RT_REG_SZ|RRF_RT_REG_EXPAND_SZ,nullptr,command.data(),&size)!=ERROR_SUCCESS)
            throw std::runtime_error("Studio file association was not found. Supply --studio-path with RobloxStudioBeta.exe.");
        std::wstring text=command.data();size_t end=text.find(L'"',1);
        if(text.empty()||text[0]!=L'"'||end==std::wstring::npos)throw std::runtime_error("Unrecognized Studio file association. Supply --studio-path.");
        path=text.substr(1,end-1);
    }
    Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
    if(file.value==INVALID_HANDLE_VALUE)throw failure("Cannot read Studio executable");
    path=finalPath(file.value);
    if(key(std::filesystem::path(path).filename().wstring())!=L"robloxstudiobeta.exe")throw std::runtime_error("Target must be named RobloxStudioBeta.exe.");
    return path;
}
std::string hashFile(const std::wstring& path){
    Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_SEQUENTIAL_SCAN,nullptr));
    if(file.value==INVALID_HANDLE_VALUE)throw failure("Cannot hash file");
    std::vector<unsigned char> object;
    struct Hash {
        BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE value=nullptr;
        ~Hash(){if(value)BCryptDestroyHash(value);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);}
    } hash;
    if(BCryptOpenAlgorithmProvider(&hash.algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("SHA256 unavailable");
    DWORD bytes=0,length=0;
    if(BCryptGetProperty(hash.algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&length),sizeof(length),&bytes,0)<0)throw std::runtime_error("SHA256 setup failed");
    object.resize(length);
    if(BCryptCreateHash(hash.algorithm,&hash.value,object.data(),DWORD(object.size()),nullptr,0,0)<0)throw std::runtime_error("SHA256 creation failed");
    std::array<unsigned char,131072> buffer{};
    for(;;){DWORD n=0;if(!ReadFile(file.value,buffer.data(),DWORD(buffer.size()),&n,nullptr))throw failure("Cannot read file");if(!n)break;if(BCryptHashData(hash.value,buffer.data(),n,0)<0)throw std::runtime_error("SHA256 update failed");}
    std::array<unsigned char,32> digest{};
    if(BCryptFinishHash(hash.value,digest.data(),DWORD(digest.size()),0)<0)throw std::runtime_error("SHA256 finish failed");
    std::ostringstream out;for(unsigned char b:digest)out<<std::hex<<std::setw(2)<<std::setfill('0')<<int(b);return out.str();
}
std::vector<DWORD> running(const std::wstring& path){
    Handle snap(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));if(snap.value==INVALID_HANDLE_VALUE)throw failure("Cannot check running processes");
    std::vector<DWORD> ids;PROCESSENTRY32W p={sizeof(p)};
    if(!Process32FirstW(snap.value,&p))throw failure("Cannot enumerate running processes");
    do{
        if(_wcsicmp(p.szExeFile,L"RobloxStudioBeta.exe")!=0)continue;
        Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,p.th32ProcessID));
        if(!process.value){if(GetLastError()==ERROR_INVALID_PARAMETER)continue;throw std::runtime_error("A Studio process could not be inspected. Close all Studio instances before installing.");}
        DWORD exitCode=0;if(!GetExitCodeProcess(process.value,&exitCode))throw failure("Cannot check Studio process state");
        if(exitCode!=STILL_ACTIVE)continue;
        std::wstring image(32768,0);DWORD length=DWORD(image.size());
        if(!QueryFullProcessImageNameW(process.value,0,image.data(),&length))throw failure("Cannot identify running Studio");
        image.resize(length);
        // The final path resolves junctions/short names, so aliases cannot hide an open target.
        Handle imageFile(CreateFileW(image.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
        if(imageFile.value==INVALID_HANDLE_VALUE)throw failure("Cannot resolve running Studio executable");
        if(key(finalPath(imageFile.value))==key(path))ids.push_back(p.th32ProcessID);
    }while(Process32NextW(snap.value,&p));return ids;
}
std::wstring backupPath(){
    std::wstring base(32768,0);DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",base.data(),DWORD(base.size()));
    if(!n||n>=base.size())throw std::runtime_error("LocalAppData is unavailable");base.resize(n);
    if(!std::filesystem::path(base).is_absolute())throw std::runtime_error("LocalAppData must be absolute");
    return (std::filesystem::path(base)/L"LuauRuntimeLab"/L"backups"/(std::wstring(StudioPatch::kOriginalSha256,StudioPatch::kOriginalSha256+64)+L".exe")).wstring();
}
bool backupValid(){auto backup=backupPath();if(!std::filesystem::exists(backup))return false;return hashFile(backup)==StudioPatch::kOriginalSha256;}
struct Snapshot {std::wstring path;std::string hash;std::vector<DWORD> pids;bool backup=false;};
Snapshot inspect(const std::wstring& supplied){Snapshot s;s.path=targetPath(supplied);s.hash=hashFile(s.path);s.pids=running(s.path);s.backup=backupValid();return s;}
std::string describe(const Snapshot& s){
    bool original=s.hash==StudioPatch::kOriginalSha256,base=s.hash==StudioNativeProfile::kPatchedSha256,reuse=s.hash==StudioSpillProfile::kPatchedSha256,patched=base||reuse,previous=s.hash==StudioNativeProfile::kPreviousSha256||s.hash==StudioNativeProfile::kPreviousSuiteSha256;
    std::ostringstream out;out<<"{\"path\":"<<json(utf8(clean(s.path)))<<",\"sha256\":"<<json(s.hash)
       <<",\"supported\":"<<(original||patched||previous?"true":"false")<<",\"installed\":"<<(patched?"true":"false")<<",\"upgradeAvailable\":"<<(previous?"true":"false")
       <<",\"bufferReuse\":"<<(reuse?"true":"false")<<",\"canEnableBufferReuse\":"<<(base&&s.backup&&s.pids.empty()?"true":"false")
       <<",\"backupValid\":"<<(s.backup?"true":"false")<<",\"runningPids\":[";
    for(size_t i=0;i<s.pids.size();i++){if(i)out<<',';out<<s.pids[i];}
    out<<"],\"canInstall\":"<<((original||(previous&&s.backup))&&s.pids.empty()?"true":"false")<<",\"canRestore\":"<<((patched||previous)&&s.backup&&s.pids.empty()?"true":"false")
       <<",\"supportedVersion\":"<<json(StudioPatch::kVersion)
       <<",\"scope\":\"Native filled table.create, unpack, dense move, byte ranges, locale-checked ASCII case, long plain search, default numeric sorting predicate and packed bits. Optional bounded transform spill-buffer reuse. No quality, window-count or remote-limit changes.\""
       <<",\"notice\":\"Experimental native code patch. Modifying Studio invalidates Roblox's executable signature. Studio updates replace the patch; every other build is refused. Save and close the target Studio before installing or restoring.\"}";return out.str();
}
struct Lock {
    Handle mutex;bool owned=false;
    Lock():mutex(CreateMutexW(nullptr,FALSE,L"Local\\LuauRuntimeLab.StudioPatch.020")){
        if(!mutex.value)throw failure("Cannot create installer lock");DWORD result=WaitForSingleObject(mutex.value,0);
        owned=result==WAIT_OBJECT_0||result==WAIT_ABANDONED;if(!owned)throw std::runtime_error("Another Studio installation operation is running");
    }
    ~Lock(){if(owned)ReleaseMutex(mutex.value);}
};
std::wstring uniqueSuffix(){
    unsigned char b[12];if(BCryptGenRandom(nullptr,b,sizeof(b),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)throw std::runtime_error("Cannot create temporary name");
    std::wostringstream out;out<<L".luau-lab-";for(auto x:b)out<<std::hex<<std::setw(2)<<std::setfill(L'0')<<int(x);return out.str();
}
struct Temp {
    std::wstring path;bool keep=false;
    explicit Temp(std::wstring p):path(std::move(p)){}
    ~Temp(){if(!keep)DeleteFileW(path.c_str());}
};
struct Edit {uint64_t offset=0,source=0;uint32_t length=0,kind=0;std::vector<unsigned char> bytes;};
std::vector<Edit> edits(bool reuse){
    const auto originalSize=reuse?StudioSpillProfile::kOriginalSize:StudioPatch::kSize;
    const auto patchedSize=reuse?StudioSpillProfile::kPatchedSize:StudioNativeProfile::kPatchedSize;
    const auto packedSize=reuse?StudioSpillProfile::kPackedSize:StudioNativeProfile::kPackedSize;
    auto resource=FindResourceW(nullptr,MAKEINTRESOURCEW(reuse?302:301),RT_RCDATA);if(!resource)throw failure("Patch resource missing");
    auto loaded=LoadResource(nullptr,resource);auto compressed=LockResource(loaded);auto size=SizeofResource(nullptr,resource);
    if(!compressed||!size)throw std::runtime_error("Patch resource unreadable");
    DECOMPRESSOR_HANDLE decompressor=nullptr;
    if(!CreateDecompressor(COMPRESS_ALGORITHM_XPRESS_HUFF,nullptr,&decompressor))throw failure("Cannot initialize patch decompressor");
    std::vector<unsigned char> decoded(static_cast<size_t>(packedSize));SIZE_T written=0;
    BOOL ok=Decompress(decompressor,compressed,size,decoded.data(),decoded.size(),&written);CloseDecompressor(decompressor);
    if(!ok||written!=decoded.size())throw std::runtime_error("Patch resource decompression failed");
    size_t cursor=0;auto take=[&](void* dest,size_t count){if(count>decoded.size()-cursor)throw std::runtime_error("Truncated patch resource");memcpy(dest,decoded.data()+cursor,count);cursor+=count;};
    char magic[8];uint64_t original=0,patched=0;uint32_t count=0;take(magic,8);take(&original,8);take(&patched,8);take(&count,4);
    if(memcmp(magic,"LABPAT02",8)||original!=originalSize||patched!=patchedSize||count>128)throw std::runtime_error("Unsupported patch resource");
    std::vector<Edit> result;uint64_t last=0;
    for(uint32_t i=0;i<count;i++){Edit edit{};take(&edit.offset,8);take(&edit.length,4);take(&edit.kind,4);if(edit.offset<last||edit.offset>patched||edit.length>patched-edit.offset||edit.kind>1)throw std::runtime_error("Invalid patch edit range");if(edit.kind){take(&edit.source,8);if(edit.offset<original||edit.source>original||edit.length>original-edit.source)throw std::runtime_error("Invalid source-copy range");}else{if(edit.length>decoded.size()-cursor)throw std::runtime_error("Invalid patch payload range");edit.bytes.resize(edit.length);take(edit.bytes.data(),edit.length);}last=edit.offset+edit.length;result.push_back(std::move(edit));}
    if(cursor!=decoded.size()||result.empty()||last!=patched)throw std::runtime_error("Incomplete patch resource");return result;
}
void stagePatch(const std::wstring& source,const std::wstring& destination,bool reuse=false){
    const auto originalSize=reuse?StudioSpillProfile::kOriginalSize:StudioPatch::kSize;
    const auto patchedSize=reuse?StudioSpillProfile::kPatchedSize:StudioNativeProfile::kPatchedSize;
    Handle in(CreateFileW(source.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr));
    if(in.value==INVALID_HANDLE_VALUE)throw failure("Cannot open original Studio");
    LARGE_INTEGER size;if(!GetFileSizeEx(in.value,&size)||size.QuadPart<0||uint64_t(size.QuadPart)!=originalSize)throw std::runtime_error("Unsupported executable size");
    Handle out(CreateFileW(destination.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
    if(out.value==INVALID_HANDLE_VALUE)throw failure("Cannot create staged executable");
    const auto changes=edits(reuse);
    std::array<unsigned char,65536> buffer{};uint64_t offset=0;
    for(;;){DWORD n=0;if(!ReadFile(in.value,buffer.data(),DWORD(buffer.size()),&n,nullptr))throw failure("Cannot read original Studio");if(!n)break;
        for(const auto& change:changes){if(change.kind)continue;auto begin=std::max(offset,change.offset),end=std::min(offset+n,change.offset+change.bytes.size());if(begin<end)memcpy(buffer.data()+size_t(begin-offset),change.bytes.data()+size_t(begin-change.offset),size_t(end-begin));}
        DWORD written=0;if(!WriteFile(out.value,buffer.data(),n,&written,nullptr)||written!=n)throw failure("Cannot write staged executable");offset+=n;
    }
    if(offset!=originalSize)throw std::runtime_error("Incomplete original file");
    for(const auto& tail:changes){if(tail.offset<originalSize)continue;if(tail.offset!=offset)throw std::runtime_error("Noncontiguous appended section");if(!tail.kind){DWORD written=0;if(!WriteFile(out.value,tail.bytes.data(),tail.length,&written,nullptr)||written!=tail.length)throw failure("Cannot append patch section");}else{LARGE_INTEGER start;start.QuadPart=tail.source;if(!SetFilePointerEx(in.value,start,nullptr,FILE_BEGIN))throw failure("Cannot seek original metadata");uint64_t remaining=tail.length;while(remaining){DWORD n=0,written=0,want=DWORD(std::min<uint64_t>(remaining,buffer.size()));if(!ReadFile(in.value,buffer.data(),want,&n,nullptr)||n!=want)throw failure("Cannot read original metadata");if(!WriteFile(out.value,buffer.data(),n,&written,nullptr)||written!=n)throw failure("Cannot copy original metadata");remaining-=n;}}offset+=tail.length;}
    if(offset!=patchedSize)throw std::runtime_error("Incomplete patched file");
    if(!FlushFileBuffers(out.value))throw failure("Cannot flush staged executable");
}
void ensureBackup(const std::wstring& original){
    auto backup=backupPath();std::filesystem::create_directories(std::filesystem::path(backup).parent_path());
    if(std::filesystem::exists(backup)){if(hashFile(backup)!=StudioPatch::kOriginalSha256)throw std::runtime_error("Existing backup is corrupt; refusing to overwrite it");return;}
    Temp temp(backup+uniqueSuffix());if(!CopyFileW(original.c_str(),temp.path.c_str(),TRUE))throw failure("Cannot create original backup");
    if(hashFile(temp.path)!=StudioPatch::kOriginalSha256)throw std::runtime_error("Original backup verification failed");
    if(!MoveFileExW(temp.path.c_str(),backup.c_str(),MOVEFILE_WRITE_THROUGH))throw failure("Cannot publish original backup");
}
std::string change(const std::wstring& supplied,bool restore,bool reuse=false){
    Lock lock;auto s=inspect(supplied);
    const bool original=s.hash==StudioPatch::kOriginalSha256,oldPatch=s.hash==StudioNativeProfile::kPreviousSha256||s.hash==StudioNativeProfile::kPreviousSuiteSha256,base=s.hash==StudioNativeProfile::kPatchedSha256,patched=base||s.hash==StudioSpillProfile::kPatchedSha256,baseUpgrade=base&&reuse;
    const std::string expected=s.hash;
    const std::string wanted=restore?StudioPatch::kOriginalSha256:reuse?StudioSpillProfile::kPatchedSha256:StudioNativeProfile::kPatchedSha256;
    if(restore?!(patched||oldPatch):!(original||oldPatch||baseUpgrade))throw std::runtime_error("Target hash is not the exact expected "+std::string(restore?"patched":"original or earlier supported patched")+" Studio build. No changes made.");
    if(!s.pids.empty())throw std::runtime_error("Target Studio is running. Save your work and close it before installing or restoring. No changes made.");
    if((restore||oldPatch||baseUpgrade)&&!s.backup)throw std::runtime_error("Verified original backup is missing. No changes made.");
    if(!restore&&original)ensureBackup(s.path);
    Temp staged(s.path+uniqueSuffix());
    if(restore){if(!CopyFileW(backupPath().c_str(),staged.path.c_str(),TRUE))throw failure("Cannot stage original backup");}
    else if(baseUpgrade)stagePatch(s.path,staged.path,true);
    else if(reuse){
        Temp baseStage(s.path+uniqueSuffix());stagePatch(oldPatch?backupPath():s.path,baseStage.path);
        if(hashFile(baseStage.path)!=StudioNativeProfile::kPatchedSha256)throw std::runtime_error("Base patch verification failed. Target unchanged.");
        stagePatch(baseStage.path,staged.path,true);
    }else stagePatch(oldPatch?backupPath():s.path,staged.path);
    if(hashFile(staged.path)!=wanted)throw std::runtime_error("Staged executable hash verification failed. Target unchanged.");
    if(!running(s.path).empty())throw std::runtime_error("Studio opened during installation. Target unchanged; close it and retry.");
    if(hashFile(s.path)!=expected)throw std::runtime_error("Studio changed during installation. Target unchanged.");
    // ReplaceFile preserves the target ACL. Keep its exact previous file until the swap is verified.
    Temp previous(s.path+uniqueSuffix()+L".previous");
    if(!ReplaceFileW(s.path.c_str(),staged.path.c_str(),previous.path.c_str(),0,nullptr,nullptr)){
        previous.keep=std::filesystem::exists(previous.path);
        throw failure("Atomic replacement failed; any recovery copy was preserved beside Studio");
    }
    previous.keep=true;
    if(hashFile(previous.path)!=expected)throw std::runtime_error("Concurrent target replacement detected. Previous file preserved beside Studio; manual recovery required.");
    if(hashFile(s.path)!=wanted){
        // Never overwrite a third party's unexpected replacement during automatic recovery.
        throw std::runtime_error("Post-install verification failed. Original backup and previous file preserved; manual recovery required.");
    }
    previous.keep=false;
    return "{\"ok\":true,\"operation\":"+json(restore?"restore":"install")+",\"status\":"+describe(inspect(s.path))+"}";
}
}
std::string status(const std::wstring& target){return describe(inspect(target));}
std::string install(const std::wstring& target,bool reuseTransformBuffer){return change(target,false,reuseTransformBuffer);}
std::string restore(const std::wstring& target){return change(target,true);}
}
