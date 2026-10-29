#pragma once
#include <string>
namespace StudioInstaller {
// Empty target selects the registered Roblox.Place application, never a game file.
std::string status(const std::wstring& target = {});
std::string install(const std::wstring& target = {}, bool reuseTransformBuffer = false);
std::string restore(const std::wstring& target = {});
}
