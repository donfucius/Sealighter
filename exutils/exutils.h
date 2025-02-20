#ifndef EXUTILS_H
#define EXUTILS_H

#include <algorithm>
#include <locale>
#include <ranges>
#include <string_view>
#include <vector>
#include <Windows.h>

namespace exutils {

std::wstring StringToWString(const UINT codePage, std::string_view str);
std::string WStringToString(const UINT codePage, std::wstring_view wstr);
std::string AnsiStringToUtf8String(std::string_view astr);


namespace detail {

struct PathSeparator {
    std::string_view value { "/\\" };
};

struct PathSeparatorW {
    std::wstring_view value { L"/\\" };
};

struct Dot {
    std::string_view value { "." };
};

struct DotW {
    std::wstring_view value { L"." };
};

} // namespace detail

template <typename CharT,
    typename PathSeparatorT =
    std::conditional_t<std::is_same_v<CharT, char>, detail::PathSeparator, detail::PathSeparatorW>>
inline std::basic_string<CharT> GetFileName(std::basic_string_view<CharT> path) noexcept
{
    const PathSeparatorT sep{};
    return { path.substr(path.find_last_of(sep.value) + 1).data() };
}

template <typename CharT,
    typename DotT =
    std::conditional_t<std::is_same_v<CharT, char>, detail::Dot, detail::DotW>>
inline std::basic_string<CharT> GetFileExtension(std::basic_string_view<CharT> path) noexcept
{
    const DotT dot{};
    return { path.substr(path.rfind(dot.value) + 1).data() };
}

template <typename CharT>
bool StringsEqualI(std::basic_string_view<CharT> str1, std::basic_string_view<CharT> str2) noexcept
{
    if (str1.length() != str2.length()) {
        return false;
    }

    return std::ranges::equal(str1.begin(), str1.end(), str2.begin(), str2.end(),
        [](const CharT ch1, const CharT ch2) {
            return std::tolower(ch1, std::locale()) == std::tolower(ch2, std::locale()); });
}

template <typename CharT>
constexpr std::basic_string<CharT> StringToLower(std::basic_string_view<CharT> src)
{
    std::basic_string<CharT> dst{ src };
    std::ranges::transform(dst.begin(), dst.end(), dst.begin(),
        [](CharT chr) { return std::tolower(chr, std::locale()); });
    return dst;
}

template<typename CharT>
inline size_t StrStrI(const std::basic_string<CharT>& str, const std::basic_string<CharT>& substr)
{
    auto charCmpI = [](CharT a, CharT b) { return std::tolower(a) == std::tolower(b); };
    auto itor = std::search(str.begin(), str.end(), substr.begin(),
            substr.end(), charCmpI);

    if (itor != str.end()) {
        return itor - str.begin();
    }

    return std::basic_string<CharT>::npos;
}

} // namespace utils

#endif // !UTILS_H
