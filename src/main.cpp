// Installer-only entry point. No interpreter, benchmark server, or preview UI.
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include "studio_installer.h"
#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

static std::wstring wide(const std::string& s){
    if(s.empty())return {};
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);
    if(!n)throw std::runtime_error("Invalid UTF-8 response");
    std::wstring out(n,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),out.data(),n);return out;
}
static std::string quoted(const std::string& s){
    std::string out="\"";for(unsigned char c:s){if(c=='"'||c=='\\'){out+='\\';out+=char(c);}else if(c>=32)out+=char(c);else out+=' ';}return out+'"';
}
static void writeResult(const std::wstring& path,const std::string& s){
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Output must be a new writable file");
    const std::string data=s+'\n';DWORD written=0;
    const bool ok=WriteFile(file,data.data(),DWORD(data.size()),&written,nullptr)&&written==data.size();
    CloseHandle(file);if(!ok)throw std::runtime_error("Cannot write output file");
}
static bool field(const std::string& json,const char* name){return json.find(std::string("\"")+name+"\":true")!=std::string::npos;}
static void gui(const std::wstring& target){
    INITCOMMONCONTROLSEX init{sizeof(init),ICC_STANDARD_CLASSES};InitCommonControlsEx(&init);
    std::wstring message;
    for(;;){
        auto status=StudioInstaller::status(target);
        const bool supported=field(status,"supported"),installed=field(status,"installed");
        const bool canInstall=field(status,"canInstall"),canRestore=field(status,"canRestore");
        const bool canReuse=field(status,"canEnableBufferReuse"),reuse=field(status,"bufferReuse");
        std::wstring heading=installed?L"Studio patch installed":supported?L"Compatible Studio build":L"This Studio build is unsupported";
        std::wstring body=L"Native Luau library improvements for Studio 0.744.0.7441077.\n\n";
        if(!message.empty())body+=message+L"\n\n";
        if(!canInstall&&!canRestore&&supported)body+=L"Save and close Studio before installing or restoring. An installed patch also needs its original backup to restore.\n\n";
        body+=reuse?L"Transform buffer reuse is enabled.\n\n":L"Optional transform buffer reuse avoids repeated allocations. No general FPS improvement is established. It can retain up to 8 MiB per renderer manager until destruction.\n\n";
        body+=L"Changes invalidate Studio's executable signature. Studio updates replace the patch. The original executable is backed up before installation.";
        TASKDIALOG_BUTTON buttons[4];int count=0;
        if(canInstall)buttons[count++]={100,L"Install patch"};
        if(canInstall||canReuse)buttons[count++]={103,canReuse?L"Enable transform buffer reuse":L"Install with transform buffer reuse"};
        if(canRestore)buttons[count++]={101,L"Restore original"};
        buttons[count++]={102,L"Check again"};
        TASKDIALOGCONFIG cfg{};cfg.cbSize=sizeof(cfg);cfg.pszWindowTitle=L"Studio Luau Patch";
        cfg.pszMainInstruction=heading.c_str();cfg.pszContent=body.c_str();cfg.pszMainIcon=TD_INFORMATION_ICON;
        cfg.dwFlags=TDF_ALLOW_DIALOG_CANCELLATION|TDF_SIZE_TO_CONTENT;cfg.dwCommonButtons=TDCBF_CLOSE_BUTTON;
        cfg.cButtons=count;cfg.pButtons=buttons;cfg.nDefaultButton=IDCLOSE;
        int button=0;HRESULT hr=TaskDialogIndirect(&cfg,&button,nullptr,nullptr);
        if(FAILED(hr))throw std::runtime_error("Cannot open installer window");
        if(button==IDCLOSE||button==IDCANCEL)return;
        message.clear();
        try{
            if(button==100){StudioInstaller::install(target);message=L"Installation complete. Start Studio normally.";}
            if(button==103){StudioInstaller::install(target,true);message=L"Installation complete with transform buffer reuse. Start Studio normally.";}
            if(button==101){StudioInstaller::restore(target);message=L"The original Studio executable has been restored.";}
        }catch(const std::exception& e){message=wide(e.what());}
    }
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){
    std::wstring action,target,output;bool cli=false,reuse=false;
    try{
        int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(!argv)throw std::runtime_error("Arguments unavailable");
        struct Args{LPWSTR* p;~Args(){LocalFree(p);}} args{argv};
        for(int i=1;i<argc;i++){
            std::wstring a=argv[i];
            if(a==L"--studio-status"||a==L"--install-studio"||a==L"--restore-studio"){
                cli=true;if(!action.empty())throw std::runtime_error("Choose one Studio operation");action=a;
            }else if(a==L"--studio-path"&&i+1<argc)target=argv[++i];
            else if(a==L"--reuse-transform-buffer")reuse=true;
            else if(a==L"--output"&&i+1<argc)output=argv[++i];
            else throw std::runtime_error("Usage: LuauStudioPatch.exe [--studio-status | --install-studio [--reuse-transform-buffer] | --restore-studio] [--studio-path RobloxStudioBeta.exe] [--output result.json]");
        }
        if(reuse&&action!=L"--install-studio")throw std::runtime_error("--reuse-transform-buffer requires --install-studio");
        if(!action.empty()){
            if(output.empty())throw std::runtime_error("CLI operations require --output");
            if(std::filesystem::exists(output))throw std::runtime_error("Output file already exists");
            auto result=action==L"--studio-status"?StudioInstaller::status(target):action==L"--install-studio"?StudioInstaller::install(target,reuse):StudioInstaller::restore(target);
            writeResult(output,result);return 0;
        }
        if(!output.empty())throw std::runtime_error("--output requires a CLI operation");
        gui(target);return 0;
    }catch(const std::exception& e){
        if(cli&&!output.empty()){try{writeResult(output,"{\"error\":"+quoted(e.what())+"}");}catch(...) {}}
        else if(!cli)MessageBoxW(nullptr,wide(e.what()).c_str(),L"Studio Luau Patch",MB_OK|MB_ICONERROR);
        return 1;
    }
}
