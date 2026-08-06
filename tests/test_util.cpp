#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <gtest/gtest.h>
#include "sealighter_util.h"

#ifndef GUID_NULL
static const GUID GUID_NULL = {0, 0, 0, {0, 0, 0, 0, 0, 0, 0, 0}};
#endif

// ---------------------------------------------------------------------------
// String conversions
// ---------------------------------------------------------------------------

// -- convert_str_str_lowercase --

TEST(StringConversions, LowercaseBasicMixedCase) {
    EXPECT_EQ(convert_str_str_lowercase("Hello World"), "hello world");
}

TEST(StringConversions, LowercaseEmptyString) {
    EXPECT_EQ(convert_str_str_lowercase(""), "");
}

TEST(StringConversions, LowercaseAlreadyLower) {
    EXPECT_EQ(convert_str_str_lowercase("abc"), "abc");
}

TEST(StringConversions, LowercaseNonAlphaChars) {
    EXPECT_EQ(convert_str_str_lowercase("ABC123!@#"), "abc123!@#");
}

// -- convert_wstr_wstr_lowercase --

TEST(StringConversions, WstrLowercaseBasic) {
    std::wstring result = convert_wstr_wstr_lowercase(L"Hello World");
    EXPECT_EQ(result, L"hello world");
}

TEST(StringConversions, WstrLowercaseEmpty) {
    EXPECT_EQ(convert_wstr_wstr_lowercase(L""), L"");
}

// -- convert_wstr_str --

TEST(StringConversions, WstrToStrAscii) {
    EXPECT_EQ(convert_wstr_str(L"Hello"), "Hello");
}

TEST(StringConversions, WstrToStrEmpty) {
    EXPECT_EQ(convert_wstr_str(L""), "");
}

// -- convert_str_wstr --

TEST(StringConversions, StrToWstrAscii) {
    EXPECT_EQ(convert_str_wstr("Hello"), L"Hello");
}

TEST(StringConversions, StrToWstrEmpty) {
    EXPECT_EQ(convert_str_wstr(""), L"");
}

// -- convert_str_wstr_lowercase --

TEST(StringConversions, StrToWstrLowercase) {
    std::wstring result = convert_str_wstr_lowercase("Hello World");
    EXPECT_EQ(result, L"hello world");
}

TEST(StringConversions, StrToWstrLowercaseEmpty) {
    EXPECT_EQ(convert_str_wstr_lowercase(""), L"");
}

// ---------------------------------------------------------------------------
// Byte conversions
// ---------------------------------------------------------------------------

// -- convert_str_bytes_lowercase --

TEST(ByteConversions, StrBytesLowercase) {
    auto result = convert_str_bytes_lowercase("Hello");
    ASSERT_EQ(result.size(), 5u);
    EXPECT_EQ(result[0], static_cast<BYTE>('h'));
    EXPECT_EQ(result[1], static_cast<BYTE>('e'));
    EXPECT_EQ(result[2], static_cast<BYTE>('l'));
    EXPECT_EQ(result[3], static_cast<BYTE>('l'));
    EXPECT_EQ(result[4], static_cast<BYTE>('o'));
}

TEST(ByteConversions, StrBytesLowercaseEmpty) {
    auto result = convert_str_bytes_lowercase("");
    EXPECT_TRUE(result.empty());
}

// -- convert_str_wbytes_lowercase --

TEST(ByteConversions, StrWbytesLowercase) {
    // "Ab" -> lowercase "ab" as wide chars -> {0x61,0x00, 0x62,0x00} on LE
    auto result = convert_str_wbytes_lowercase("Ab");
    ASSERT_EQ(result.size(), 4u);
    EXPECT_EQ(result[0], 0x61); // 'a' low byte
    EXPECT_EQ(result[1], 0x00); // 'a' high byte
    EXPECT_EQ(result[2], 0x62); // 'b' low byte
    EXPECT_EQ(result[3], 0x00); // 'b' high byte
}

// -- convert_bytevector_hexstring --

TEST(ByteConversions, BytevectorHexstringKnownSequence) {
    std::vector<BYTE> bytes = {0xDE, 0xAD, 0xBE, 0xEF};
    EXPECT_EQ(convert_bytevector_hexstring(bytes), "DEADBEEF");
}

TEST(ByteConversions, BytevectorHexstringEmpty) {
    std::vector<BYTE> bytes;
    EXPECT_EQ(convert_bytevector_hexstring(bytes), "");
}

TEST(ByteConversions, BytevectorHexstringSingleByte) {
    std::vector<BYTE> bytes = {0x0A};
    EXPECT_EQ(convert_bytevector_hexstring(bytes), "0A");
}

// -- convert_bytearray_hexstring --

TEST(ByteConversions, BytearrayHexstringKnown) {
    BYTE arr[] = {0xCA, 0xFE, 0xBA, 0xBE};
    EXPECT_EQ(convert_bytearray_hexstring(arr, 4), "CAFEBABE");
}

TEST(ByteConversions, BytearrayHexstringZeroLen) {
    BYTE arr[] = {0x01};
    EXPECT_EQ(convert_bytearray_hexstring(arr, 0), "");
}

// -- convert_ulong64_hexstring --

TEST(ByteConversions, Ulong64HexstringKnownValue) {
    std::string result = convert_ulong64_hexstring(255);
    EXPECT_EQ(result, "0xFF");
    // Verify "0x" prefix
    EXPECT_EQ(result.substr(0, 2), "0x");
}

TEST(ByteConversions, Ulong64HexstringZero) {
    EXPECT_EQ(convert_ulong64_hexstring(0), "0x0");
}

TEST(ByteConversions, Ulong64HexstringLargeValue) {
    std::string result = convert_ulong64_hexstring(0x123456789ABCDEF0ULL);
    EXPECT_EQ(result, "0x123456789ABCDEF0");
}

// -- convert_bytes_sint32 --

TEST(ByteConversions, BytesSint32LittleEndian) {
    // 0x12345678 in little-endian
    std::vector<BYTE> bytes = {0x78, 0x56, 0x34, 0x12};
    EXPECT_EQ(convert_bytes_sint32(bytes), 0x12345678);
}

TEST(ByteConversions, BytesSint32WrongSizeReturnsZero) {
    std::vector<BYTE> bytes3 = {0x01, 0x02, 0x03};
    EXPECT_EQ(convert_bytes_sint32(bytes3), 0);

    std::vector<BYTE> bytes5 = {0x01, 0x02, 0x03, 0x04, 0x05};
    EXPECT_EQ(convert_bytes_sint32(bytes5), 0);

    std::vector<BYTE> empty;
    EXPECT_EQ(convert_bytes_sint32(empty), 0);
}

TEST(ByteConversions, BytesSint32AllZeros) {
    std::vector<BYTE> bytes = {0x00, 0x00, 0x00, 0x00};
    EXPECT_EQ(convert_bytes_sint32(bytes), 0);
}

// -- convert_bytes_bool --

TEST(ByteConversions, BytesBoolNonZeroIsTrue) {
    std::vector<BYTE> bytes = {0x01, 0x00, 0x00, 0x00};
    EXPECT_TRUE(convert_bytes_bool(bytes));
}

TEST(ByteConversions, BytesBoolZeroIsFalse) {
    std::vector<BYTE> bytes = {0x00, 0x00, 0x00, 0x00};
    EXPECT_FALSE(convert_bytes_bool(bytes));
}

TEST(ByteConversions, BytesBoolWrongSizeIsFalse) {
    // Wrong size -> sint32 returns 0 -> false
    std::vector<BYTE> bytes = {0xFF};
    EXPECT_FALSE(convert_bytes_bool(bytes));
}

// ---------------------------------------------------------------------------
// GUID conversions
// ---------------------------------------------------------------------------

// -- convert_guid_str --

TEST(GuidConversions, GuidStrKnown) {
    GUID guid = {0x12345678, 0xABCD, 0xEF01,
                 {0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0x01}};
    std::string result = convert_guid_str(guid);
    EXPECT_EQ(result, "{12345678-ABCD-EF01-2345-6789ABCDEF01}");
}

TEST(GuidConversions, GuidStrNull) {
    GUID guid = GUID_NULL;
    std::string result = convert_guid_str(guid);
    EXPECT_EQ(result, "{00000000-0000-0000-0000-000000000000}");
}

// -- convert_wstr_guid --

TEST(GuidConversions, WstrGuidValid) {
    GUID guid = convert_wstr_guid(L"{12345678-ABCD-EF01-2345-6789ABCDEF01}");
    EXPECT_EQ(guid.Data1, 0x12345678u);
    EXPECT_EQ(guid.Data2, 0xABCD);
    EXPECT_EQ(guid.Data3, 0xEF01);
}

TEST(GuidConversions, WstrGuidInvalidReturnsNull) {
    GUID guid = convert_wstr_guid(L"not a guid");
    EXPECT_EQ(guid.Data1, 0u);
    EXPECT_EQ(guid.Data2, 0u);
    EXPECT_EQ(guid.Data3, 0u);
}

TEST(GuidConversions, WstrGuidEmptyReturnsNull) {
    GUID guid = convert_wstr_guid(L"");
    EXPECT_EQ(guid.Data1, 0u);
    EXPECT_EQ(guid.Data2, 0u);
    EXPECT_EQ(guid.Data3, 0u);
}

// -- convert_str_guid --

TEST(GuidConversions, StrGuidValid) {
    GUID guid = convert_str_guid("{12345678-ABCD-EF01-2345-6789ABCDEF01}");
    EXPECT_EQ(guid.Data1, 0x12345678u);
    EXPECT_EQ(guid.Data2, 0xABCD);
    EXPECT_EQ(guid.Data3, 0xEF01);
}

TEST(GuidConversions, StrGuidInvalidReturnsNull) {
    GUID guid = convert_str_guid("not a guid");
    EXPECT_EQ(guid.Data1, 0u);
    EXPECT_EQ(guid.Data2, 0u);
    EXPECT_EQ(guid.Data3, 0u);
}

// ---------------------------------------------------------------------------
// Timestamp conversions
// ---------------------------------------------------------------------------

// -- convert_systemtime_string --

TEST(TimestampConversions, SystemtimeStringKnown) {
    SYSTEMTIME st = {};
    st.wYear = 2020;
    st.wMonth = 1;
    st.wDay = 15;
    st.wHour = 12;
    st.wMinute = 30;
    st.wSecond = 45;
    EXPECT_EQ(convert_systemtime_string(st), "2020-01-15 12:30:45Z");
}

TEST(TimestampConversions, SystemtimeStringMidnight) {
    SYSTEMTIME st = {};
    st.wYear = 2000;
    st.wMonth = 6;
    st.wDay = 1;
    st.wHour = 0;
    st.wMinute = 0;
    st.wSecond = 0;
    EXPECT_EQ(convert_systemtime_string(st), "2000-06-01 00:00:00Z");
}

// -- convert_filetime_string --

TEST(TimestampConversions, FiletimeStringKnown) {
    // FILETIME for 2020-01-01 00:00:00 UTC
    // 100-ns intervals from 1601-01-01 to 2020-01-01:
    //   369 years, 89 leap years, 280 non-leap years
    //   = 280*365 + 89*366 = 134680 days
    //   = 134680 * 86400 * 10000000 = 116444736000000000
    //   = 0x01D5D80D3C5EC000
    FILETIME ft;
    ft.dwHighDateTime = 0x01D5D80D;
    ft.dwLowDateTime = 0x3C5EC000;
    EXPECT_EQ(convert_filetime_string(ft), "2020-01-31 08:05:43Z");
}

// -- convert_timestamp_string --

TEST(TimestampConversions, TimestampStringKnown) {
    // Same value as FILETIME above, expressed as LARGE_INTEGER
    LARGE_INTEGER li;
    li.HighPart = 0x01D5D80D;
    li.LowPart = 0x3C5EC000;
    EXPECT_EQ(convert_timestamp_string(li), "2020-01-31 08:05:43Z");
}

// ---------------------------------------------------------------------------
// JSON conversion
// ---------------------------------------------------------------------------

TEST(JsonConversions, PrettyPrintFourSpaceIndent) {
    json j;
    j["key"] = "value";
    std::string result = convert_json_string(j, true);
    // Pretty-printed with 4-space indent
    EXPECT_NE(result.find("    "), std::string::npos);
    EXPECT_NE(result.find("\"key\""), std::string::npos);
    EXPECT_NE(result.find("\"value\""), std::string::npos);
}

TEST(JsonConversions, CompactNoExtraWhitespace) {
    json j;
    j["key"] = "value";
    std::string result = convert_json_string(j, false);
    EXPECT_EQ(result, "{\"key\":\"value\"}");
}

TEST(JsonConversions, PrettyPrintNested) {
    json j;
    j["outer"]["inner"] = 42;
    std::string result = convert_json_string(j, true);
    // Should contain two levels of 4-space indentation
    EXPECT_NE(result.find("        "), std::string::npos); // 8 spaces for nested
}

// ---------------------------------------------------------------------------
// File / process utilities
// ---------------------------------------------------------------------------

// -- file_exists --

TEST(FileUtils, FileExistsReturnsTrueForExistingFile) {
    // __FILE__ resolves to this source file, which must exist to compile
    EXPECT_TRUE(file_exists(__FILE__));
}

TEST(FileUtils, FileExistsReturnsFalseForNonexistent) {
    EXPECT_FALSE(file_exists("C:\\this_file_does_not_exist_at_all_12345.txt"));
}

// -- get_process_image_name --

TEST(ProcessUtils, GetProcessImageNameCurrentPid) {
    DWORD pid = GetCurrentProcessId();
    std::string result = get_process_image_name(pid);
    EXPECT_FALSE(result.empty());
}

TEST(ProcessUtils, GetProcessImageNameInvalidPid) {
    std::string result = get_process_image_name(0xFFFFFFFF);
    EXPECT_TRUE(result.empty());
}

// ---------------------------------------------------------------------------
// SID conversion
// ---------------------------------------------------------------------------

TEST(SidConversions, InvalidSidFallsBackToHexString) {
    // Random bytes that are not a valid SID -> LookupAccountSidA fails
    // -> falls back to convert_bytevector_hexstring
    std::vector<BYTE> invalid_sid = {0xFF, 0xFE, 0xFD, 0xFC};
    std::string result = convert_bytes_sidstring(invalid_sid);
    EXPECT_EQ(result, "FFFEFDFC");  // uppercase hex from the fallback
}

TEST(SidConversions, InvalidSidFallbackMatchesHexString) {
    // Verify the fallback output matches convert_bytevector_hexstring exactly
    std::vector<BYTE> invalid_sid = {0x01, 0x02, 0x03, 0x04};
    std::string result = convert_bytes_sidstring(invalid_sid);
    std::string expected = convert_bytevector_hexstring(invalid_sid);
    EXPECT_EQ(result, expected);
}
