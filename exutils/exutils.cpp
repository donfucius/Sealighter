#include "exutils.h"
#include <system_error>

namespace exutils {

    std::wstring StringToWString(const UINT codePage, std::string_view str)
    {
        if (str.empty()) {
            return L"";
        }

        constexpr DWORD CONV_FLAG_ZERO = 0;
        constexpr int NULL_STRING = -1; // null-terminated string
        constexpr int ZERO_SIZE = 0;

        const auto sizeNeeded = ::MultiByteToWideChar(
            codePage, CONV_FLAG_ZERO, str.data(), NULL_STRING, nullptr, ZERO_SIZE);
        if (sizeNeeded <= 0) {
            throw std::system_error(GetLastError(), std::system_category(),
                "MultiByteToWideChar failed to calculate size");
        }

        std::wstring wstr(sizeNeeded - 1, L'\0'); // Reserve space without null terminator
        if (::MultiByteToWideChar(codePage, CONV_FLAG_ZERO, str.data(),
            NULL_STRING, wstr.data(), sizeNeeded) != sizeNeeded) {
            throw std::system_error(GetLastError(), std::system_category(),
                "MultiByteToWideChar failed to convert string");
        }

        return wstr;
    }

    std::string WStringToString(const UINT codePage, std::wstring_view wstr)
    {
        if (wstr.empty()) {
            return "";
        }

        constexpr DWORD CONV_FLAG_ZERO = 0;
        constexpr int NULL_STRING = -1; // null-terminated string
        constexpr int ZERO_SIZE = 0;

        const auto sizeNeeded = ::WideCharToMultiByte(
            codePage, CONV_FLAG_ZERO, wstr.data(), NULL_STRING,
            nullptr, ZERO_SIZE, nullptr, nullptr);
        if (sizeNeeded <= 0) {
            throw std::system_error(GetLastError(), std::system_category(),
                "WideCharToMultiByte failed to calculate size");
        }

        std::string str(sizeNeeded - 1, '\0'); // Reserve space without null terminator
        if (::WideCharToMultiByte(codePage, CONV_FLAG_ZERO, wstr.data(),
            NULL_STRING, str.data(), sizeNeeded, nullptr, nullptr) != sizeNeeded) {
            throw std::system_error(GetLastError(), std::system_category(),
                "WideCharToMultiByte failed to convert string");
        }

        return str;
    }

    std::string AnsiStringToUtf8String(std::string_view astr)
    {
        if (astr.empty()) {
            return "";
        }
        return WStringToString(CP_UTF8, StringToWString(CP_ACP, astr));
    }

} // namespace exutils
