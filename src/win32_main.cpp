#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>

#include "CharacterSections.hpp"
#include "DataFilePaths.hpp"
#include "GameDataCore.hpp"
#include "HashDatabase.hpp"
#include "LogicalFamilies.hpp"
#include "RelationshipMap.hpp"
#include "SectionNames.hpp"
#include "ValueDecoder.hpp"
#include "version.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")

#if defined(_MSC_VER)
// Extend MSVC's single linker-generated manifest with Common Controls v6.
// Do not add RT_MANIFEST to the resource script: that creates a duplicate
// MANIFEST resource. The linker-generated UAC defaults are asInvoker and
// uiAccess=false; /MANIFESTUAC is unsupported through #pragma comment(linker).
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

namespace {

constexpr wchar_t kWindowClass[] = L"GBFRToolCpp17Window";
constexpr int kToolbarHeight = 42;
constexpr int kActionBarHeight = 34;
constexpr int kStatusHeightFallback = 24;
constexpr int kMargin = 6;
constexpr std::size_t kPageSize = gdtv::kDefaultPageSize;

constexpr UINT ID_FILE_OPEN_PRIMARY = 1001;
constexpr UINT ID_FILE_OPEN_COMPARE = 1002;
constexpr UINT ID_FILE_LOAD_MAP = 1003;
constexpr UINT ID_FILE_EXPORT_PAYLOAD = 1004;
constexpr UINT ID_FILE_EXIT = 1005;
constexpr UINT ID_FILE_SAVE_PRIMARY_AS = 1006;
constexpr UINT ID_FILE_SAVE_COMPARE_AS = 1007;
constexpr UINT ID_FILE_LOAD_CHARACTER_SECTIONS = 1009;
constexpr UINT ID_FILE_LOAD_SECTION_NAMES = 1010;
constexpr UINT ID_EDIT_CURRENT_VALUE = 1008;
constexpr UINT ID_HASH_LOAD_DATABASE = 1301;
constexpr UINT ID_HASH_SAVE_DATABASE = 1303;
constexpr UINT ID_HASH_ADD = 1304;
constexpr UINT ID_HASH_RELOAD = 1305;
constexpr UINT ID_HASH_EXPORT_UNRESOLVED = 1306;
constexpr UINT ID_VIEW_EXPAND = 1101;
constexpr UINT ID_VIEW_COLLAPSE = 1102;
constexpr UINT ID_VIEW_REFRESH = 1103;
constexpr UINT ID_VIEW_DETAILED_LOGICAL_INFO = 1104;
constexpr UINT ID_VIEW_RELOAD_CHARACTER_SECTIONS = 1105;
constexpr UINT ID_VIEW_RELOAD_SECTION_NAMES = 1106;
constexpr UINT ID_VIEW_LOGICAL_ONLY = 1107;
constexpr UINT ID_HELP_ABOUT = 1201;
constexpr UINT ID_BTN_OPEN_PRIMARY = 2001;
constexpr UINT ID_BTN_OPEN_COMPARE = 2002;
constexpr UINT ID_BTN_LOAD_MAP = 2003;
constexpr UINT ID_EDIT_SEARCH = 2004;
constexpr UINT ID_BTN_SEARCH = 2005;
constexpr UINT ID_BTN_CLEAR_SEARCH = 2006;
constexpr UINT ID_BTN_COPY_LOCATOR = 2007;
constexpr UINT ID_BTN_EXPORT = 2008;
constexpr UINT ID_BTN_ADD_HASH = 2009;
constexpr UINT ID_BTN_EDIT_VALUE = 2010;
constexpr UINT ID_TREE = 2101;
constexpr UINT ID_TAB = 2102;
constexpr UINT ID_DETAILS = 2103;
constexpr UINT ID_HEX = 2104;
constexpr UINT ID_STATUS = 2105;
constexpr UINT ID_VALUES = 2106;
constexpr UINT WM_APP_SUMMON_MOD_CLOSED = WM_APP + 1U;
constexpr UINT WM_APP_MASTERY_MOD_CLOSED = WM_APP + 2U;
constexpr UINT WM_APP_LOGICAL_MOD_CLOSED = WM_APP + 3U;

HMENU controlId(UINT id) noexcept {
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id));
}

std::wstring utf8ToWide(const std::string& text) {
    if (text.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (count <= 0) return std::wstring(text.begin(), text.end());
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), count);
    return result;
}

std::string wideToUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0,
                                          nullptr, nullptr);
    if (count <= 0) {
        std::string fallback;
        fallback.reserve(text.size());
        for (const wchar_t value : text) {
            fallback.push_back(value >= 0 && value <= 0x7F ? static_cast<char>(value) : '?');
        }
        return fallback;
    }
    std::string result(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), count,
                        nullptr, nullptr);
    return result;
}

std::wstring numberW(std::uint64_t value) { return utf8ToWide(gdtv::formatNumber(value)); }

std::wstring hexW(std::uint64_t value, int width = 8) {
    std::wostringstream out;
    out << L"0x" << std::uppercase << std::hex << std::setw(width) << std::setfill(L'0') << value;
    return out.str();
}

std::wstring legacyLocatorW(std::uint32_t key) {
    std::wostringstream out;
    out << L"FF" << std::uppercase << std::hex << std::setfill(L'0')
        << std::setw(2) << (key & 0xFFU)
        << std::setw(2) << ((key >> 8U) & 0xFFU)
        << L"0000";
    return out.str();
}

std::wstring logicalUnitSummaryW(const gdtv::LogicalFamilyDefinition& family,
                                     std::uint32_t unitId) {
    const auto address = gdtv::decodeLogicalUnitId(family, unitId);
    if (!address.valid) return utf8ToWide(std::string(family.slotLabel)) + L" " + numberW(unitId);
    switch (family.grouping) {
    case gdtv::LogicalGroupingKind::CurrentTraitsCharacter:
        return L"Character " + numberW(address.characterGroup) + L" / Namespace " +
               numberW(address.nameSpace) + L" / Position " + numberW(address.position) +
               L" / Slot " + numberW(address.slot);
    case gdtv::LogicalGroupingKind::OverMasteryCharacter:
        return L"Character " + numberW(address.characterGroup) + L" / Slot " +
               numberW(address.slot);
    case gdtv::LogicalGroupingKind::MasteryCharacter:
        if (address.shared) return L"Shared / Global Slot " + numberW(address.slot);
        return L"Character " + numberW(address.characterGroup) + L" / Slot " +
               numberW(address.slot);
    case gdtv::LogicalGroupingKind::Flat:
        return utf8ToWide(std::string(family.slotLabel)) + L" " + numberW(address.slot);
    }
    return utf8ToWide(std::string(family.slotLabel)) + L" " + numberW(unitId);
}


template <typename To, typename From>
To bitCopy(From value) noexcept {
    static_assert(sizeof(To) == sizeof(From));
    To result{};
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

std::wstring rawLittleEndianW(std::uint64_t value, std::size_t size) {
    std::wostringstream out;
    out << std::uppercase << std::hex << std::setfill(L'0');
    for (std::size_t index = 0; index < size; ++index) {
        out << std::setw(2) << ((value >> (index * 8U)) & 0xFFU);
    }
    return out.str();
}

std::wstring trimWide(std::wstring value) {
    const auto first = value.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return {};
    const auto last = value.find_last_not_of(L" \t\r\n");
    return value.substr(first, last - first + 1U);
}

std::wstring getWindowTextString(HWND hwnd) {
    const int length = GetWindowTextLengthW(hwnd);
    if (length <= 0) return {};
    std::wstring text(static_cast<std::size_t>(length) + 1U, L'\0');
    GetWindowTextW(hwnd, text.data(), length + 1);
    text.resize(static_cast<std::size_t>(length));
    return text;
}

std::filesystem::path executableDirectory() {
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
}

std::optional<std::filesystem::path> openFileDialog(HWND owner, const wchar_t* title,
                                                    const wchar_t* filter = L"All files\0*.*\0\0") {
    std::wstring buffer(32768, L'\0');
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrTitle = title;
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (!GetOpenFileNameW(&dialog)) return std::nullopt;
    return std::filesystem::path(buffer.c_str());
}

std::optional<std::filesystem::path> saveFileDialog(
    HWND owner, const wchar_t* title, const std::wstring& initialName,
    const wchar_t* filter = L"Binary files (*.bin)\0*.bin\0All files\0*.*\0\0",
    const wchar_t* defaultExtension = L"bin") {
    std::wstring buffer(32768, L'\0');
    std::copy(initialName.begin(), initialName.end(), buffer.begin());
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrTitle = title;
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.lpstrDefExt = defaultExtension;
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (!GetSaveFileNameW(&dialog)) return std::nullopt;
    return std::filesystem::path(buffer.c_str());
}

void showError(HWND owner, const std::wstring& message) {
    MessageBoxW(owner, message.c_str(), GDTV_APP_NAME_W, MB_OK | MB_ICONERROR);
}

void setClipboardText(HWND owner, const std::wstring& text) {
    if (!OpenClipboard(owner)) return;
    EmptyClipboard();
    const auto bytes = (text.size() + 1U) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory) {
        if (void* destination = GlobalLock(memory)) {
            std::memcpy(destination, text.c_str(), bytes);
            GlobalUnlock(memory);
            if (!SetClipboardData(CF_UNICODETEXT, memory)) GlobalFree(memory);
        } else {
            GlobalFree(memory);
        }
    }
    CloseClipboard();
}

struct HashDialogContext {
    HWND owner{};
    HWND window{};
    HWND hashEdit{};
    HWND idEdit{};
    HWND nameEdit{};
    HWND categoryEdit{};
    HWND versionEdit{};
    HWND notesEdit{};
    HWND computedLabel{};
    HFONT font{};
    bool done{};
    bool accepted{};
    std::optional<std::uint32_t> suggestedHash;
    gdtv::HashEntry result;
};

constexpr UINT ID_HASH_DIALOG_HASH = 3101;
constexpr UINT ID_HASH_DIALOG_ID = 3102;
constexpr UINT ID_HASH_DIALOG_NAME = 3103;
constexpr UINT ID_HASH_DIALOG_CATEGORY = 3104;
constexpr UINT ID_HASH_DIALOG_VERSION = 3105;
constexpr UINT ID_HASH_DIALOG_NOTES = 3106;
constexpr UINT ID_HASH_DIALOG_CALCULATE = 3107;

void setDialogFont(HWND control, HFONT font) {
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

HWND addDialogControl(HWND parent, const wchar_t* className, const wchar_t* text, DWORD style,
                      int x, int y, int width, int height, UINT id, HFONT font,
                      DWORD exStyle = 0) {
    HWND control = CreateWindowExW(exStyle, className, text, WS_CHILD | WS_VISIBLE | style,
                                   x, y, width, height, parent, controlId(id),
                                   GetModuleHandleW(nullptr), nullptr);
    if (control) setDialogFont(control, font);
    return control;
}

void updateHashDialogPreview(HashDialogContext& context) {
    const auto id = wideToUtf8(getWindowTextString(context.idEdit));
    std::optional<std::uint32_t> hash;
    if (!id.empty()) hash = gdtv::xxHash32Custom(id);
    if (!hash) {
        const auto text = wideToUtf8(getWindowTextString(context.hashEdit));
        hash = gdtv::parseHashValue(text, false);
    }
    if (!hash) {
        SetWindowTextW(context.computedLabel, L"Enter an ID or an 8-digit hash. Prefix raw save bytes with LE:.");
        return;
    }
    const auto message = L"Canonical: 0x" + utf8ToWide(gdtv::hashHex(*hash)) +
                         L"    Raw little-endian: " + utf8ToWide(gdtv::hashRawLittleEndian(*hash));
    SetWindowTextW(context.computedLabel, message.c_str());
}

LRESULT CALLBACK hashDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* context = reinterpret_cast<HashDialogContext*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        context = static_cast<HashDialogContext*>(create->lpCreateParams);
        context->window = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(context));
    }
    if (!context) return DefWindowProcW(hwnd, message, wParam, lParam);

    switch (message) {
    case WM_CREATE: {
        context->font = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                    DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        addDialogControl(hwnd, L"STATIC", L"Hash (canonical, or LE:raw bytes):", SS_LEFT, 14, 15, 230, 22, 0, context->font);
        context->hashEdit = addDialogControl(hwnd, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL,
                                             250, 12, 275, 26, ID_HASH_DIALOG_HASH, context->font, WS_EX_CLIENTEDGE);
        addDialogControl(hwnd, L"STATIC", L"Internal name:", SS_LEFT, 14, 50, 230, 22, 0, context->font);
        context->idEdit = addDialogControl(hwnd, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL,
                                           250, 47, 275, 26, ID_HASH_DIALOG_ID, context->font, WS_EX_CLIENTEDGE);
        addDialogControl(hwnd, L"STATIC", L"In-game name:", SS_LEFT, 14, 85, 230, 22, 0, context->font);
        context->nameEdit = addDialogControl(hwnd, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL,
                                             250, 82, 275, 26, ID_HASH_DIALOG_NAME, context->font, WS_EX_CLIENTEDGE);
        addDialogControl(hwnd, L"STATIC", L"Type:", SS_LEFT, 14, 120, 230, 22, 0, context->font);
        context->categoryEdit = addDialogControl(hwnd, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL,
                                                 250, 117, 275, 26, ID_HASH_DIALOG_CATEGORY, context->font, WS_EX_CLIENTEDGE);
        addDialogControl(hwnd, L"STATIC", L"Version:", SS_LEFT, 14, 155, 230, 22, 0, context->font);
        context->versionEdit = addDialogControl(hwnd, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL,
                                                250, 152, 275, 26, ID_HASH_DIALOG_VERSION, context->font, WS_EX_CLIENTEDGE);
        addDialogControl(hwnd, L"STATIC", L"Notes:", SS_LEFT, 14, 190, 230, 22, 0, context->font);
        context->notesEdit = addDialogControl(hwnd, L"EDIT", L"", WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
                                              250, 187, 275, 78, ID_HASH_DIALOG_NOTES, context->font, WS_EX_CLIENTEDGE);
        context->computedLabel = addDialogControl(hwnd, L"STATIC", L"", SS_LEFT, 14, 273, 511, 42, 0, context->font);
        addDialogControl(hwnd, L"BUTTON", L"Calculate from ID", WS_TABSTOP | BS_PUSHBUTTON,
                         14, 320, 140, 30, ID_HASH_DIALOG_CALCULATE, context->font);
        addDialogControl(hwnd, L"BUTTON", L"Save Mapping", WS_TABSTOP | BS_DEFPUSHBUTTON,
                         325, 320, 96, 30, IDOK, context->font);
        addDialogControl(hwnd, L"BUTTON", L"Cancel", WS_TABSTOP | BS_PUSHBUTTON,
                         429, 320, 96, 30, IDCANCEL, context->font);
        if (context->suggestedHash) {
            SetWindowTextW(context->hashEdit, utf8ToWide(gdtv::hashHex(*context->suggestedHash)).c_str());
        }
        updateHashDialogPreview(*context);
        return 0;
    }
    case WM_COMMAND: {
        const auto id = LOWORD(wParam);
        if (id == ID_HASH_DIALOG_CALCULATE) {
            const auto gameId = wideToUtf8(getWindowTextString(context->idEdit));
            if (gameId.empty()) {
                MessageBoxW(hwnd, L"Enter the internal name first.", L"Hash Mapping", MB_OK | MB_ICONINFORMATION);
            } else {
                const auto hash = gdtv::xxHash32Custom(gameId);
                SetWindowTextW(context->hashEdit, utf8ToWide(gdtv::hashHex(hash)).c_str());
                updateHashDialogPreview(*context);
            }
            return 0;
        }
        if (id == IDOK) {
            const auto gameId = wideToUtf8(getWindowTextString(context->idEdit));
            const auto hashText = wideToUtf8(getWindowTextString(context->hashEdit));
            std::optional<std::uint32_t> hash;
            if (!hashText.empty()) hash = gdtv::parseHashValue(hashText, false);
            if (!hash && !gameId.empty()) hash = gdtv::xxHash32Custom(gameId);
            if (!hash) {
                MessageBoxW(hwnd, L"Enter a valid hash or internal name.", L"Hash Mapping", MB_OK | MB_ICONERROR);
                return 0;
            }
            if (!gameId.empty() && gdtv::xxHash32Custom(gameId) != *hash) {
                if (MessageBoxW(hwnd,
                    L"The entered hash does not match the custom XXHash32 of the internal name.\n\n"
                    L"Save the entered hash anyway?", L"Hash Mismatch", MB_YESNO | MB_ICONWARNING) != IDYES) {
                    return 0;
                }
            }
            context->result.hash = *hash;
            context->result.id = gameId;
            context->result.displayName = wideToUtf8(getWindowTextString(context->nameEdit));
            context->result.category = wideToUtf8(getWindowTextString(context->categoryEdit));
            context->result.version = wideToUtf8(getWindowTextString(context->versionEdit));
            context->result.source = "Unified Hash Database";
            context->result.notes = wideToUtf8(getWindowTextString(context->notesEdit));
            context->result.userDefined = true;
            context->accepted = true;
            DestroyWindow(hwnd);
            return 0;
        }
        if (id == IDCANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
        if (HIWORD(wParam) == EN_CHANGE && (id == ID_HASH_DIALOG_HASH || id == ID_HASH_DIALOG_ID)) {
            updateHashDialogPreview(*context);
        }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (context->font) DeleteObject(context->font);
        context->done = true;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

std::optional<gdtv::HashEntry> showHashMappingDialog(HWND owner, std::optional<std::uint32_t> suggestedHash) {
    constexpr wchar_t className[] = L"GBFRToolHashMappingDialog";
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = hashDialogProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = className;
        registered = RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }
    if (!registered) return std::nullopt;

    HashDialogContext context;
    context.owner = owner;
    context.suggestedHash = suggestedHash;
    RECT ownerRect{};
    GetWindowRect(owner, &ownerRect);
    const int width = 555;
    const int height = 400;
    const int ownerLeft = static_cast<int>(ownerRect.left);
    const int ownerTop = static_cast<int>(ownerRect.top);
    const int ownerWidth = static_cast<int>(ownerRect.right - ownerRect.left);
    const int ownerHeight = static_cast<int>(ownerRect.bottom - ownerRect.top);
    const int x = ownerLeft + std::max(0, (ownerWidth - width) / 2);
    const int y = ownerTop + std::max(0, (ownerHeight - height) / 2);
    EnableWindow(owner, FALSE);
    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME, className, L"Add / Update Hash Mapping",
                                  WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, width, height,
                                  owner, nullptr, GetModuleHandleW(nullptr), &context);
    if (!dialog) {
        EnableWindow(owner, TRUE);
        return std::nullopt;
    }
    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);
    MSG message{};
    while (!context.done && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dialog, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    if (context.accepted) return context.result;
    return std::nullopt;
}


struct ValueEditDialogContext {
    HWND owner{};
    HWND window{};
    HWND valueEdit{};
    HFONT font{};
    bool done{};
    bool accepted{};
    std::wstring heading;
    std::wstring current;
    std::wstring instructions;
    std::wstring result;
};

constexpr UINT ID_VALUE_DIALOG_EDIT = 3201;

LRESULT CALLBACK valueEditDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* context = reinterpret_cast<ValueEditDialogContext*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        context = static_cast<ValueEditDialogContext*>(create->lpCreateParams);
        context->window = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(context));
    }
    if (!context) return DefWindowProcW(hwnd, message, wParam, lParam);

    switch (message) {
    case WM_CREATE:
        context->font = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                    DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        addDialogControl(hwnd, L"STATIC", context->heading.c_str(), SS_LEFT,
                         14, 15, 526, 44, 0, context->font);
        addDialogControl(hwnd, L"STATIC", L"Current value:", SS_LEFT,
                         14, 67, 120, 22, 0, context->font);
        addDialogControl(hwnd, L"STATIC", context->current.c_str(), SS_LEFT,
                         140, 67, 400, 42, 0, context->font);
        addDialogControl(hwnd, L"STATIC", L"New value:", SS_LEFT,
                         14, 116, 120, 22, 0, context->font);
        context->valueEdit = addDialogControl(hwnd, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL,
                                               140, 112, 400, 28, ID_VALUE_DIALOG_EDIT,
                                               context->font, WS_EX_CLIENTEDGE);
        addDialogControl(hwnd, L"STATIC", context->instructions.c_str(), SS_LEFT,
                         14, 153, 526, 78, 0, context->font);
        addDialogControl(hwnd, L"BUTTON", L"Apply Edit", WS_TABSTOP | BS_DEFPUSHBUTTON,
                         340, 242, 96, 30, IDOK, context->font);
        addDialogControl(hwnd, L"BUTTON", L"Cancel", WS_TABSTOP | BS_PUSHBUTTON,
                         444, 242, 96, 30, IDCANCEL, context->font);
        SetWindowTextW(context->valueEdit, context->current.c_str());
        SendMessageW(context->valueEdit, EM_SETSEL, 0, -1);
        SetFocus(context->valueEdit);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            context->result = getWindowTextString(context->valueEdit);
            if (context->result.empty()) {
                MessageBoxW(hwnd, L"Enter a new value.", L"Edit Current Value", MB_OK | MB_ICONERROR);
                return 0;
            }
            context->accepted = true;
            DestroyWindow(hwnd);
            return 0;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (context->font) DeleteObject(context->font);
        context->done = true;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

std::optional<std::wstring> showValueEditDialog(HWND owner, const std::wstring& heading,
                                                const std::wstring& current,
                                                const std::wstring& instructions) {
    constexpr wchar_t className[] = L"GBFRToolValueEditDialog";
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = valueEditDialogProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = className;
        registered = RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }
    if (!registered) return std::nullopt;

    ValueEditDialogContext context;
    context.owner = owner;
    context.heading = heading;
    context.current = current;
    context.instructions = instructions;

    RECT ownerRect{};
    GetWindowRect(owner, &ownerRect);
    const int width = 570;
    const int height = 325;
    const int x = static_cast<int>(ownerRect.left) +
                  std::max(0, (static_cast<int>(ownerRect.right - ownerRect.left) - width) / 2);
    const int y = static_cast<int>(ownerRect.top) +
                  std::max(0, (static_cast<int>(ownerRect.bottom - ownerRect.top) - height) / 2);

    EnableWindow(owner, FALSE);
    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME, className, L"Edit Current Value",
                                  WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, width, height,
                                  owner, nullptr, GetModuleHandleW(nullptr), &context);
    if (!dialog) {
        EnableWindow(owner, TRUE);
        return std::nullopt;
    }
    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);
    MSG message{};
    while (!context.done && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dialog, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    if (context.accepted) return context.result;
    return std::nullopt;
}


struct HashListDialogContext {
    HWND owner{};
    HWND window{};
    HWND searchEdit{};
    HWND typeCombo{};
    HWND listBox{};
    HWND countLabel{};
    HFONT font{};
    bool done{};
    bool accepted{};
    std::vector<gdtv::HashEntry> entries;
    std::vector<std::wstring> typeOptions;
    std::vector<std::size_t> visibleEntries;
    std::uint32_t result{};
};

constexpr UINT ID_HASH_LIST_SEARCH = 3301;
constexpr UINT ID_HASH_LIST_BOX = 3302;
constexpr UINT ID_HASH_LIST_TYPE = 3303;

std::wstring lowerWide(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return value;
}

std::wstring hashEntryText(const gdtv::HashEntry& entry) {
    std::wstring text;
    if (!entry.displayName.empty()) text += utf8ToWide(entry.displayName);
    if (!entry.id.empty()) {
        if (!text.empty()) text += L" | ";
        text += utf8ToWide(entry.id);
    }
    if (text.empty()) text = L"<unnamed>";
    text += L" | 0x" + utf8ToWide(gdtv::hashHex(entry.hash));
    if (!entry.category.empty()) text += L" | " + utf8ToWide(entry.category);
    return text;
}

void refreshHashListDialog(HashListDialogContext& context) {
    const auto query = lowerWide(trimWide(getWindowTextString(context.searchEdit)));
    std::wstring selectedType;
    const auto typeSelection = static_cast<int>(
        SendMessageW(context.typeCombo, CB_GETCURSEL, 0, 0));
    if (typeSelection > 0 && static_cast<std::size_t>(typeSelection) < context.typeOptions.size()) {
        selectedType = context.typeOptions[static_cast<std::size_t>(typeSelection)];
    }

    SendMessageW(context.listBox, WM_SETREDRAW, FALSE, 0);
    SendMessageW(context.listBox, LB_RESETCONTENT, 0, 0);
    context.visibleEntries.clear();

    constexpr std::size_t kMaximumVisible = 5000U;
    std::size_t matchingEntries = 0U;
    for (std::size_t index = 0; index < context.entries.size(); ++index) {
        const auto& entry = context.entries[index];
        if (!selectedType.empty() && !gdtv::isGlobalEmptySlotHash(entry.hash)) {
            const auto entryType = entry.category.empty() ? L"(No Type)" : utf8ToWide(entry.category);
            if (entryType != selectedType) continue;
        }
        const auto text = hashEntryText(entry);
        if (!query.empty() && lowerWide(text).find(query) == std::wstring::npos) continue;
        ++matchingEntries;
        if (context.visibleEntries.size() >= kMaximumVisible) continue;
        const auto row = static_cast<int>(SendMessageW(context.listBox, LB_ADDSTRING, 0,
                                                        reinterpret_cast<LPARAM>(text.c_str())));
        if (row >= 0) {
            SendMessageW(context.listBox, LB_SETITEMDATA, static_cast<WPARAM>(row),
                         static_cast<LPARAM>(index));
            context.visibleEntries.push_back(index);
        }
    }
    SendMessageW(context.listBox, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(context.listBox, nullptr, TRUE);

    std::wstring count = numberW(context.visibleEntries.size()) + L" shown";
    if (matchingEntries != context.visibleEntries.size()) {
        count += L" of " + numberW(matchingEntries) + L" matches (search or select a Type to narrow)";
    }
    SetWindowTextW(context.countLabel, count.c_str());
    if (!context.visibleEntries.empty()) SendMessageW(context.listBox, LB_SETCURSEL, 0, 0);
}

bool acceptHashListSelection(HashListDialogContext& context) {
    const auto selection = static_cast<int>(SendMessageW(context.listBox, LB_GETCURSEL, 0, 0));
    if (selection == LB_ERR) {
        MessageBoxW(context.window, L"Select a hash entry first.", L"Hash List",
                    MB_OK | MB_ICONINFORMATION);
        return false;
    }
    const auto index = static_cast<std::size_t>(
        SendMessageW(context.listBox, LB_GETITEMDATA, static_cast<WPARAM>(selection), 0));
    if (index >= context.entries.size()) return false;
    context.result = context.entries[index].hash;
    context.accepted = true;
    DestroyWindow(context.window);
    return true;
}

LRESULT CALLBACK hashListDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* context = reinterpret_cast<HashListDialogContext*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        context = static_cast<HashListDialogContext*>(create->lpCreateParams);
        context->window = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(context));
    }
    if (!context) return DefWindowProcW(hwnd, message, wParam, lParam);

    switch (message) {
    case WM_CREATE:
        context->font = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                    DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        addDialogControl(hwnd, L"STATIC", L"Search by in-game name, internal ID, category, or hash:",
                         SS_LEFT, 14, 15, 610, 22, 0, context->font);
        context->searchEdit = addDialogControl(hwnd, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL,
                                               14, 42, 610, 28, ID_HASH_LIST_SEARCH,
                                               context->font, WS_EX_CLIENTEDGE);
        addDialogControl(hwnd, L"STATIC", L"Visible Type:", SS_LEFT,
                         14, 82, 90, 22, 0, context->font);
        context->typeCombo = addDialogControl(hwnd, L"COMBOBOX", L"",
                                              WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                                              108, 77, 300, 320, ID_HASH_LIST_TYPE,
                                              context->font, WS_EX_CLIENTEDGE);
        for (const auto& type : context->typeOptions) {
            SendMessageW(context->typeCombo, CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(type.c_str()));
        }
        SendMessageW(context->typeCombo, CB_SETCURSEL, 0, 0);
        context->listBox = addDialogControl(hwnd, L"LISTBOX", L"",
                                            WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                                            14, 112, 610, 390, ID_HASH_LIST_BOX,
                                            context->font, WS_EX_CLIENTEDGE);
        context->countLabel = addDialogControl(hwnd, L"STATIC", L"", SS_LEFT,
                                               14, 510, 390, 22, 0, context->font);
        addDialogControl(hwnd, L"BUTTON", L"Use Selected", WS_TABSTOP | BS_DEFPUSHBUTTON,
                         414, 506, 100, 30, IDOK, context->font);
        addDialogControl(hwnd, L"BUTTON", L"Cancel", WS_TABSTOP | BS_PUSHBUTTON,
                         524, 506, 100, 30, IDCANCEL, context->font);
        refreshHashListDialog(*context);
        SetFocus(context->searchEdit);
        return 0;
    case WM_COMMAND: {
        const auto id = LOWORD(wParam);
        if (id == ID_HASH_LIST_SEARCH && HIWORD(wParam) == EN_CHANGE) {
            refreshHashListDialog(*context);
            return 0;
        }
        if (id == ID_HASH_LIST_TYPE && HIWORD(wParam) == CBN_SELCHANGE) {
            refreshHashListDialog(*context);
            return 0;
        }
        if (id == ID_HASH_LIST_BOX && HIWORD(wParam) == LBN_DBLCLK) {
            acceptHashListSelection(*context);
            return 0;
        }
        if (id == IDOK) {
            acceptHashListSelection(*context);
            return 0;
        }
        if (id == IDCANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (context->font) DeleteObject(context->font);
        context->done = true;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

std::optional<std::uint32_t> showHashListDialog(HWND owner, const gdtv::HashDatabase& database,
                                                    std::string_view categoryFilter = {},
                                                    const wchar_t* title = L"Hash List",
                                                    bool categoryPrefix = false) {
    constexpr wchar_t className[] = L"GBFRToolHashListDialog";
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = hashListDialogProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = className;
        registered = RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }
    if (!registered) return std::nullopt;

    HashListDialogContext context;
    context.owner = owner;
    context.entries = database.allEntries();

    // Present one canonical Global Empty Slot row even when the external
    // database still contains an older removed-slot label for the same hash.
    const auto* globalEmpty = database.preferred(gdtv::kGlobalEmptySlotHash);
    context.entries.erase(
        std::remove_if(context.entries.begin(), context.entries.end(),
                       [](const gdtv::HashEntry& entry) {
                           return gdtv::isGlobalEmptySlotHash(entry.hash);
                       }),
        context.entries.end());
    if (globalEmpty) context.entries.push_back(*globalEmpty);

    if (!categoryFilter.empty()) {
        const std::string category(categoryFilter);
        context.entries.erase(
            std::remove_if(context.entries.begin(), context.entries.end(),
                           [&category, categoryPrefix](const gdtv::HashEntry& entry) {
                               if (gdtv::isGlobalEmptySlotHash(entry.hash)) return false;
                               if (categoryPrefix) {
                                   return entry.category.size() < category.size() ||
                                          entry.category.compare(0U, category.size(), category) != 0;
                               }
                               return entry.category != category;
                           }),
            context.entries.end());
    }
    std::sort(context.entries.begin(), context.entries.end(), [](const auto& left, const auto& right) {
        const bool leftGlobal = gdtv::isGlobalEmptySlotHash(left.hash);
        const bool rightGlobal = gdtv::isGlobalEmptySlotHash(right.hash);
        if (leftGlobal != rightGlobal) return leftGlobal;
        const auto leftText = left.displayName.empty() ? left.id : left.displayName;
        const auto rightText = right.displayName.empty() ? right.id : right.displayName;
        if (leftText != rightText) return leftText < rightText;
        if (left.id != right.id) return left.id < right.id;
        return left.hash < right.hash;
    });

    context.typeOptions.push_back(L"All Types");
    std::set<std::wstring> availableTypes;
    for (const auto& entry : context.entries) {
        availableTypes.insert(entry.category.empty() ? L"(No Type)" : utf8ToWide(entry.category));
    }
    context.typeOptions.insert(context.typeOptions.end(), availableTypes.begin(), availableTypes.end());

    RECT ownerRect{};
    GetWindowRect(owner, &ownerRect);
    constexpr int width = 655;
    constexpr int height = 590;
    const int x = static_cast<int>(ownerRect.left) +
                  std::max(0, (static_cast<int>(ownerRect.right - ownerRect.left) - width) / 2);
    const int y = static_cast<int>(ownerRect.top) +
                  std::max(0, (static_cast<int>(ownerRect.bottom - ownerRect.top) - height) / 2);

    EnableWindow(owner, FALSE);
    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME, className, title,
                                  WS_POPUP | WS_CAPTION | WS_SYSMENU,
                                  x, y, width, height, owner, nullptr,
                                  GetModuleHandleW(nullptr), &context);
    if (!dialog) {
        EnableWindow(owner, TRUE);
        return std::nullopt;
    }
    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);
    MSG message{};
    while (!context.done && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dialog, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    if (context.accepted) return context.result;
    return std::nullopt;
}

struct SummonSlotModDialogContext {
    HWND owner{};
    HWND window{};
    HWND titleLabel{};
    HWND statusLabel{};
    HFONT font{};
    gdtv::SaveData* save{};
    const gdtv::HashDatabase* hashDatabase{};
    std::uint32_t unitId{};
    bool changed{};
    bool suppressCloseNotification{};
    std::array<HWND, 7> edits{};
    std::array<HWND, 7> resolvedLabels{};
};

constexpr UINT ID_SUMMON_EDIT_BASE = 3400;
constexpr UINT ID_SUMMON_MOD_BASE = 3500;
constexpr UINT ID_SUMMON_HASH_BASE = 3600;

std::wstring summonEditSeed(const SummonSlotModDialogContext& context, std::size_t fieldIndex) {
    const auto& field = gdtv::summonInventoryFamily().fields[fieldIndex];
    const auto bits = context.save->elementBits(field.key, context.unitId, field.elementIndex);
    if (field.kind == gdtv::LogicalValueKind::Hash) return hexW(bits, 8);
    if (field.kind == gdtv::LogicalValueKind::Signed) {
        return std::to_wstring(bitCopy<std::int32_t>(static_cast<std::uint32_t>(bits)));
    }
    return std::to_wstring(bits);
}

std::wstring summonHashResolution(const SummonSlotModDialogContext& context, std::size_t fieldIndex) {
    const auto& field = gdtv::summonInventoryFamily().fields[fieldIndex];
    if (field.kind != gdtv::LogicalValueKind::Hash || !context.hashDatabase) return {};
    const auto value = static_cast<std::uint32_t>(
        context.save->elementBits(field.key, context.unitId, field.elementIndex));
    const auto* entry = context.hashDatabase->preferred(value);
    if (!entry) return L"Unknown hash";
    std::wstring result;
    if (!entry->displayName.empty()) result += utf8ToWide(entry->displayName);
    if (!entry->id.empty()) {
        if (!result.empty()) result += L" | ";
        result += utf8ToWide(entry->id);
    }
    return result.empty() ? L"Known hash" : result;
}

void refreshSummonSlotRow(SummonSlotModDialogContext& context, std::size_t fieldIndex) {
    SetWindowTextW(context.edits[fieldIndex], summonEditSeed(context, fieldIndex).c_str());
    SetWindowTextW(context.resolvedLabels[fieldIndex], summonHashResolution(context, fieldIndex).c_str());
}

void selectSummonSlotInModWindow(SummonSlotModDialogContext& context,
                                 gdtv::SaveData& save,
                                 std::uint32_t unitId) {
    context.save = &save;
    context.unitId = unitId;
    const auto title = L"Summon Inventory Slot " + numberW(unitId) + L"  -  MOD";
    SetWindowTextW(context.window, title.c_str());
    if (context.titleLabel) SetWindowTextW(context.titleLabel, title.c_str());
    for (std::size_t index = 0; index < gdtv::summonInventoryFamily().fieldCount; ++index) {
        refreshSummonSlotRow(context, index);
    }
    if (context.statusLabel) {
        SetWindowTextW(context.statusLabel,
                       L"Values are edited in memory until Save Edited ... As is used.");
    }
    ShowWindow(context.window, SW_SHOW);
    SetForegroundWindow(context.window);
}

std::optional<std::uint64_t> parseSummonModValue(const std::wstring& inputText,
                                                 const gdtv::LogicalFieldDefinition& field,
                                                 const gdtv::HashDatabase& database,
                                                 std::wstring& error) {
    const auto input = trimWide(inputText);
    if (input.empty()) {
        error = L"Enter a value first.";
        return std::nullopt;
    }

    if (field.kind == gdtv::LogicalValueKind::Hash) {
        auto text = wideToUtf8(input);
        bool raw = false;
        if (text.size() >= 4U) {
            std::string prefix = text.substr(0, 4U);
            std::transform(prefix.begin(), prefix.end(), prefix.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            if (prefix == "raw:") {
                raw = true;
                text.erase(0, 4U);
            }
        }
        if (const auto parsed = gdtv::parseHashValue(text, raw)) return *parsed;
        const auto loweredText = lowerWide(utf8ToWide(text));
        if (loweredText == L"global empty slot" || loweredText == L"empty slot") {
            return gdtv::kGlobalEmptySlotHash;
        }

        const auto matches = database.hashesForText(text);
        if (matches.size() == 1U) return matches.front();
        if (matches.size() > 1U) {
            error = L"That name or ID matches more than one hash. Use Hash List to choose the exact entry.";
            return std::nullopt;
        }

        // A typed internal game ID is valid even when it is not in the current
        // database. Hash it using the game's custom XXHash32 implementation.
        return gdtv::xxHash32Custom(text);
    }

    try {
        if (input.size() > 4U && lowerWide(input.substr(0, 4U)) == L"raw:") {
            const auto parsed = gdtv::parseHashValue(wideToUtf8(input.substr(4U)), true);
            if (!parsed) throw std::invalid_argument("raw value");
            return *parsed;
        }
        if (input.size() > 2U && input[0] == L'0' && (input[1] == L'x' || input[1] == L'X')) {
            std::size_t used = 0;
            const auto value = std::stoull(input.substr(2U), &used, 16);
            if (used != input.size() - 2U || value > 0xFFFFFFFFULL) throw std::out_of_range("hex");
            return value;
        }
        std::size_t used = 0;
        if (field.kind == gdtv::LogicalValueKind::Signed ||
            field.kind == gdtv::LogicalValueKind::Bitfield) {
            const auto value = std::stoll(input, &used, 10);
            if (used != input.size() || value < std::numeric_limits<std::int32_t>::min() ||
                value > std::numeric_limits<std::int32_t>::max()) throw std::out_of_range("signed");
            return static_cast<std::uint32_t>(static_cast<std::int32_t>(value));
        }
        const auto value = std::stoull(input, &used, 10);
        if (used != input.size() || value > 0xFFFFFFFFULL) throw std::out_of_range("unsigned");
        return value;
    } catch (...) {
        error = L"Enter decimal, 0x canonical hexadecimal, or raw:XXXXXXXX little-endian bytes.";
        return std::nullopt;
    }
}

bool applySummonSlotField(SummonSlotModDialogContext& context, std::size_t fieldIndex) {
    const auto& field = gdtv::summonInventoryFamily().fields[fieldIndex];
    std::wstring error;
    const auto value = parseSummonModValue(getWindowTextString(context.edits[fieldIndex]),
                                           field, *context.hashDatabase, error);
    if (!value) {
        MessageBoxW(context.window, error.c_str(), L"Summon Slot MOD", MB_OK | MB_ICONERROR);
        return false;
    }
    try {
        context.save->setElementBits(field.key, context.unitId, field.elementIndex, *value);
        context.changed = true;
        refreshSummonSlotRow(context, fieldIndex);
        const auto message = utf8ToWide(std::string(field.locator)) + L" modified in memory. Use File > Save Edited ... As.";
        SetWindowTextW(context.statusLabel, message.c_str());
        return true;
    } catch (const std::exception& exception) {
        MessageBoxW(context.window, utf8ToWide(exception.what()).c_str(), L"Summon Slot MOD",
                    MB_OK | MB_ICONERROR);
        return false;
    }
}

LRESULT CALLBACK summonSlotModDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* context = reinterpret_cast<SummonSlotModDialogContext*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        context = static_cast<SummonSlotModDialogContext*>(create->lpCreateParams);
        context->window = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(context));
    }
    if (!context) return DefWindowProcW(hwnd, message, wParam, lParam);

    switch (message) {
    case WM_CREATE: {
        context->font = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                    DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        const auto title = L"Summon Inventory Slot " + numberW(context->unitId) + L"  -  MOD";
        SetWindowTextW(hwnd, title.c_str());
        context->titleLabel = addDialogControl(hwnd, L"STATIC", title.c_str(), SS_LEFT,
                                               16, 14, 660, 28, 0, context->font);

        const auto& family = gdtv::summonInventoryFamily();
        for (std::size_t index = 0; index < family.fieldCount; ++index) {
            const auto& field = family.fields[index];
            const int y = 52 + static_cast<int>(index) * 64;
            const auto label = utf8ToWide(std::string(field.locator)) + L" - " +
                               utf8ToWide(std::string(field.label));
            addDialogControl(hwnd, L"STATIC", label.c_str(), SS_LEFT,
                             18, y, 276, 24, 0, context->font);
            context->edits[index] = addDialogControl(
                hwnd, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL,
                300, y - 3, 178, 28, ID_SUMMON_EDIT_BASE + static_cast<UINT>(index),
                context->font, WS_EX_CLIENTEDGE);
            addDialogControl(hwnd, L"BUTTON", L"MOD", WS_TABSTOP | BS_PUSHBUTTON,
                             488, y - 3, 68, 28,
                             ID_SUMMON_MOD_BASE + static_cast<UINT>(index), context->font);
            if (field.kind == gdtv::LogicalValueKind::Hash) {
                addDialogControl(hwnd, L"BUTTON", L"Hash List", WS_TABSTOP | BS_PUSHBUTTON,
                                 566, y - 3, 96, 28,
                                 ID_SUMMON_HASH_BASE + static_cast<UINT>(index), context->font);
            }
            context->resolvedLabels[index] = addDialogControl(
                hwnd, L"STATIC", L"", SS_LEFT,
                300, y + 28, 362, 22, 0, context->font);
            refreshSummonSlotRow(*context, index);
        }

        context->statusLabel = addDialogControl(
            hwnd, L"STATIC", L"Values are edited in memory until Save Edited ... As is used.",
            SS_LEFT, 18, 506, 520, 38, 0, context->font);
        addDialogControl(hwnd, L"BUTTON", L"Close", WS_TABSTOP | BS_DEFPUSHBUTTON,
                         574, 512, 88, 30, IDCANCEL, context->font);
        return 0;
    }
    case WM_COMMAND: {
        const auto id = LOWORD(wParam);
        if (id >= ID_SUMMON_MOD_BASE && id < ID_SUMMON_MOD_BASE + 7U) {
            applySummonSlotField(*context, id - ID_SUMMON_MOD_BASE);
            return 0;
        }
        if (id >= ID_SUMMON_HASH_BASE && id < ID_SUMMON_HASH_BASE + 7U) {
            const auto fieldIndex = static_cast<std::size_t>(id - ID_SUMMON_HASH_BASE);
            std::optional<std::uint32_t> selected;
            EnableWindow(context->owner, FALSE);
            if (fieldIndex == 1U) {
                selected = showHashListDialog(hwnd, *context->hashDatabase,
                                              "Summons", L"Summon Hash List");
            } else if (fieldIndex == 3U) {
                selected = showHashListDialog(hwnd, *context->hashDatabase,
                                              "Summon Base Bonuses", L"Summon Base Bonus Hash List");
            } else {
                selected = showHashListDialog(hwnd, *context->hashDatabase);
            }
            EnableWindow(context->owner, TRUE);
            SetForegroundWindow(hwnd);
            if (selected) {
                SetWindowTextW(context->edits[fieldIndex], hexW(*selected, 8).c_str());
                SendMessageW(context->edits[fieldIndex], EM_SETSEL, 0, -1);
                SetFocus(context->edits[fieldIndex]);
            }
            return 0;
        }
        if (id == IDCANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (context->font) {
            DeleteObject(context->font);
            context->font = nullptr;
        }
        context->window = nullptr;
        if (!context->suppressCloseNotification && context->owner) {
            PostMessageW(context->owner, WM_APP_SUMMON_MOD_CLOSED,
                         context->changed ? 1U : 0U, 0);
        }
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

HWND createSummonSlotModWindow(HWND owner, SummonSlotModDialogContext& context) {
    constexpr wchar_t className[] = L"GBFRToolSummonSlotModDialog";
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = summonSlotModDialogProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = className;
        registered = RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }
    if (!registered) return nullptr;

    RECT ownerRect{};
    GetWindowRect(owner, &ownerRect);
    constexpr int width = 700;
    constexpr int height = 590;
    const int x = static_cast<int>(ownerRect.left) +
                  std::max(0, (static_cast<int>(ownerRect.right - ownerRect.left) - width) / 2);
    const int y = static_cast<int>(ownerRect.top) +
                  std::max(0, (static_cast<int>(ownerRect.bottom - ownerRect.top) - height) / 2);

    context.owner = owner;
    HWND dialog = CreateWindowExW(WS_EX_TOOLWINDOW, className, L"Summon Inventory Slot MOD",
                                  WS_POPUP | WS_CAPTION | WS_SYSMENU,
                                  x, y, width, height, owner, nullptr,
                                  GetModuleHandleW(nullptr), &context);
    if (!dialog) return nullptr;
    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);
    return dialog;
}


struct MasteryTreeModDialogContext {
    HWND owner{};
    HWND window{};
    HWND titleLabel{};
    HWND statusLabel{};
    HFONT font{};
    gdtv::SaveData* save{};
    const gdtv::HashDatabase* hashDatabase{};
    std::uint32_t unitId{};
    bool changed{};
    bool suppressCloseNotification{};
    std::array<HWND, 3> edits{};
    std::array<HWND, 3> modButtons{};
    std::array<HWND, 3> hashButtons{};
    std::array<HWND, 3> resolvedLabels{};
};

constexpr UINT ID_MASTERY_EDIT_BASE = 3700;
constexpr UINT ID_MASTERY_MOD_BASE = 3800;
constexpr UINT ID_MASTERY_HASH_BASE = 3900;

std::wstring masteryEditSeed(const MasteryTreeModDialogContext& context, std::size_t fieldIndex) {
    const auto& field = gdtv::masteryTreeFamily().fields[fieldIndex];
    if (!gdtv::logicalFieldAvailable(*context.save, field, context.unitId)) return L"<not present>";
    const auto bits = context.save->elementBits(field.key, context.unitId, field.elementIndex);
    if (field.kind == gdtv::LogicalValueKind::Hash) return hexW(bits, 8);
    if (field.kind == gdtv::LogicalValueKind::Signed ||
        field.kind == gdtv::LogicalValueKind::Bitfield) {
        return std::to_wstring(bitCopy<std::int32_t>(static_cast<std::uint32_t>(bits)));
    }
    return std::to_wstring(bits);
}

std::wstring masteryHashResolution(const MasteryTreeModDialogContext& context,
                                   std::size_t fieldIndex) {
    const auto& field = gdtv::masteryTreeFamily().fields[fieldIndex];
    if (field.kind != gdtv::LogicalValueKind::Hash || !context.hashDatabase ||
        !gdtv::logicalFieldAvailable(*context.save, field, context.unitId)) return {};
    const auto value = static_cast<std::uint32_t>(
        context.save->elementBits(field.key, context.unitId, field.elementIndex));
    const auto* entry = context.hashDatabase->preferred(value);
    if (!entry) return L"Unknown mastery-tree hash";
    std::wstring result;
    if (!entry->displayName.empty()) result += utf8ToWide(entry->displayName);
    if (!entry->id.empty()) {
        if (!result.empty()) result += L" | ";
        result += utf8ToWide(entry->id);
    }
    return result.empty() ? L"Known hash" : result;
}

std::wstring masteryBitfieldSummary(const MasteryTreeModDialogContext& context,
                                    std::size_t fieldIndex) {
    const auto& field = gdtv::masteryTreeFamily().fields[fieldIndex];
    if (field.kind != gdtv::LogicalValueKind::Bitfield ||
        !gdtv::logicalFieldAvailable(*context.save, field, context.unitId)) return {};
    const auto value = static_cast<std::uint32_t>(
        context.save->elementBits(field.key, context.unitId, field.elementIndex));
    unsigned count = 0U;
    auto remaining = value;
    while (remaining != 0U) {
        count += remaining & 1U;
        remaining >>= 1U;
    }
    std::wstring result = L"Hex " + hexW(value, 8) + L" | " + numberW(count) + L" bit(s) set";
    if (value == 0U) result += L" (not activated)";
    return result;
}

void refreshMasteryTreeRow(MasteryTreeModDialogContext& context, std::size_t fieldIndex) {
    const auto& field = gdtv::masteryTreeFamily().fields[fieldIndex];
    const bool available = gdtv::logicalFieldAvailable(*context.save, field, context.unitId);
    SetWindowTextW(context.edits[fieldIndex], masteryEditSeed(context, fieldIndex).c_str());
    std::wstring resolved = masteryHashResolution(context, fieldIndex);
    if (resolved.empty()) resolved = masteryBitfieldSummary(context, fieldIndex);
    if (!available && field.optional) resolved = L"Optional legacy section is not present in this save.";
    SetWindowTextW(context.resolvedLabels[fieldIndex], resolved.c_str());
    EnableWindow(context.edits[fieldIndex], available ? TRUE : FALSE);
    EnableWindow(context.modButtons[fieldIndex], available ? TRUE : FALSE);
    if (context.hashButtons[fieldIndex]) EnableWindow(context.hashButtons[fieldIndex], available ? TRUE : FALSE);
}

void selectMasteryTreeEntryInModWindow(MasteryTreeModDialogContext& context,
                                       gdtv::SaveData& save,
                                       std::uint32_t unitId) {
    context.save = &save;
    context.unitId = unitId;
    const auto title = L"Mastery Tree - " + logicalUnitSummaryW(gdtv::masteryTreeFamily(), unitId) + L"  -  MOD";
    SetWindowTextW(context.window, title.c_str());
    if (context.titleLabel) SetWindowTextW(context.titleLabel, title.c_str());
    for (std::size_t index = 0; index < gdtv::masteryTreeFamily().fieldCount; ++index) {
        refreshMasteryTreeRow(context, index);
    }
    if (context.statusLabel) {
        SetWindowTextW(context.statusLabel,
                       L"Values are edited in memory until Save Edited ... As is used.");
    }
    ShowWindow(context.window, SW_SHOW);
    SetForegroundWindow(context.window);
}

bool applyMasteryTreeField(MasteryTreeModDialogContext& context, std::size_t fieldIndex) {
    const auto& field = gdtv::masteryTreeFamily().fields[fieldIndex];
    if (!gdtv::logicalFieldAvailable(*context.save, field, context.unitId)) return false;
    std::wstring error;
    const auto value = parseSummonModValue(getWindowTextString(context.edits[fieldIndex]),
                                           field, *context.hashDatabase, error);
    if (!value) {
        MessageBoxW(context.window, error.c_str(), L"Mastery Tree MOD", MB_OK | MB_ICONERROR);
        return false;
    }
    try {
        context.save->setElementBits(field.key, context.unitId, field.elementIndex, *value);
        context.changed = true;
        refreshMasteryTreeRow(context, fieldIndex);
        const auto message = legacyLocatorW(field.key) +
            L" modified in memory. Use File > Save Edited ... As.";
        SetWindowTextW(context.statusLabel, message.c_str());
        return true;
    } catch (const std::exception& exception) {
        MessageBoxW(context.window, utf8ToWide(exception.what()).c_str(), L"Mastery Tree MOD",
                    MB_OK | MB_ICONERROR);
        return false;
    }
}

LRESULT CALLBACK masteryTreeModDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* context = reinterpret_cast<MasteryTreeModDialogContext*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        context = static_cast<MasteryTreeModDialogContext*>(create->lpCreateParams);
        context->window = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(context));
    }
    if (!context) return DefWindowProcW(hwnd, message, wParam, lParam);

    switch (message) {
    case WM_CREATE: {
        context->font = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                    DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        const auto title = L"Mastery Tree - " + logicalUnitSummaryW(gdtv::masteryTreeFamily(), context->unitId) + L"  -  MOD";
        SetWindowTextW(hwnd, title.c_str());
        context->titleLabel = addDialogControl(hwnd, L"STATIC", title.c_str(), SS_LEFT,
                                               16, 14, 680, 28, 0, context->font);

        const auto& family = gdtv::masteryTreeFamily();
        for (std::size_t index = 0; index < family.fieldCount; ++index) {
            const auto& field = family.fields[index];
            const int y = 52 + static_cast<int>(index) * 72;
            const auto label = legacyLocatorW(field.key) + L" - " +
                               utf8ToWide(std::string(field.label));
            addDialogControl(hwnd, L"STATIC", label.c_str(), SS_LEFT,
                             18, y, 326, 24, 0, context->font);
            context->edits[index] = addDialogControl(
                hwnd, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL,
                348, y - 3, 168, 28, ID_MASTERY_EDIT_BASE + static_cast<UINT>(index),
                context->font, WS_EX_CLIENTEDGE);
            context->modButtons[index] = addDialogControl(
                hwnd, L"BUTTON", L"MOD", WS_TABSTOP | BS_PUSHBUTTON,
                526, y - 3, 68, 28,
                ID_MASTERY_MOD_BASE + static_cast<UINT>(index), context->font);
            if (field.kind == gdtv::LogicalValueKind::Hash) {
                context->hashButtons[index] = addDialogControl(
                    hwnd, L"BUTTON", L"Hash List", WS_TABSTOP | BS_PUSHBUTTON,
                    604, y - 3, 96, 28,
                    ID_MASTERY_HASH_BASE + static_cast<UINT>(index), context->font);
            }
            context->resolvedLabels[index] = addDialogControl(
                hwnd, L"STATIC", L"", SS_LEFT,
                348, y + 29, 352, 30, 0, context->font);
            refreshMasteryTreeRow(*context, index);
        }

        context->statusLabel = addDialogControl(
            hwnd, L"STATIC", L"Values are edited in memory until Save Edited ... As is used.",
            SS_LEFT, 18, 282, 520, 38, 0, context->font);
        addDialogControl(hwnd, L"BUTTON", L"Close", WS_TABSTOP | BS_DEFPUSHBUTTON,
                         612, 288, 88, 30, IDCANCEL, context->font);
        return 0;
    }
    case WM_COMMAND: {
        const auto id = LOWORD(wParam);
        if (id >= ID_MASTERY_MOD_BASE && id < ID_MASTERY_MOD_BASE + 3U) {
            applyMasteryTreeField(*context, id - ID_MASTERY_MOD_BASE);
            return 0;
        }
        if (id >= ID_MASTERY_HASH_BASE && id < ID_MASTERY_HASH_BASE + 3U) {
            const auto fieldIndex = static_cast<std::size_t>(id - ID_MASTERY_HASH_BASE);
            EnableWindow(context->owner, FALSE);
            const auto selected = showHashListDialog(hwnd, *context->hashDatabase,
                                                     "Masteries", L"Mastery Tree Hash List", true);
            EnableWindow(context->owner, TRUE);
            SetForegroundWindow(hwnd);
            if (selected) {
                SetWindowTextW(context->edits[fieldIndex], hexW(*selected, 8).c_str());
                SendMessageW(context->edits[fieldIndex], EM_SETSEL, 0, -1);
                SetFocus(context->edits[fieldIndex]);
            }
            return 0;
        }
        if (id == IDCANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (context->font) {
            DeleteObject(context->font);
            context->font = nullptr;
        }
        context->window = nullptr;
        if (!context->suppressCloseNotification && context->owner) {
            PostMessageW(context->owner, WM_APP_MASTERY_MOD_CLOSED,
                         context->changed ? 1U : 0U, 0);
        }
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

HWND createMasteryTreeModWindow(HWND owner, MasteryTreeModDialogContext& context) {
    constexpr wchar_t className[] = L"GBFRToolMasteryTreeModDialog";
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = masteryTreeModDialogProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = className;
        registered = RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }
    if (!registered) return nullptr;

    RECT ownerRect{};
    GetWindowRect(owner, &ownerRect);
    constexpr int width = 740;
    constexpr int height = 370;
    const int x = static_cast<int>(ownerRect.left) +
                  std::max(0, (static_cast<int>(ownerRect.right - ownerRect.left) - width) / 2);
    const int y = static_cast<int>(ownerRect.top) +
                  std::max(0, (static_cast<int>(ownerRect.bottom - ownerRect.top) - height) / 2);

    context.owner = owner;
    HWND dialog = CreateWindowExW(WS_EX_TOOLWINDOW, className, L"Mastery Tree MOD",
                                  WS_POPUP | WS_CAPTION | WS_SYSMENU,
                                  x, y, width, height, owner, nullptr,
                                  GetModuleHandleW(nullptr), &context);
    if (!dialog) return nullptr;
    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);
    return dialog;
}


constexpr std::size_t kMaxSharedLogicalFields = 8U;

struct LogicalFamilyModDialogContext {
    HWND owner{};
    HWND window{};
    HWND titleLabel{};
    HWND statusLabel{};
    HFONT font{};
    gdtv::SaveData* save{};
    const gdtv::HashDatabase* hashDatabase{};
    const gdtv::LogicalFamilyDefinition* family{};
    std::uint32_t unitId{};
    bool changed{};
    bool suppressCloseNotification{};
    std::array<HWND, kMaxSharedLogicalFields> edits{};
    std::array<HWND, kMaxSharedLogicalFields> modButtons{};
    std::array<HWND, kMaxSharedLogicalFields> hashButtons{};
    std::array<HWND, kMaxSharedLogicalFields> resolvedLabels{};
};

constexpr UINT ID_LOGICAL_EDIT_BASE = 4000;
constexpr UINT ID_LOGICAL_MOD_BASE = 4100;
constexpr UINT ID_LOGICAL_HASH_BASE = 4200;

std::wstring logicalFamilyWindowTitle(const LogicalFamilyModDialogContext& context) {
    if (!context.family) return L"Logical Record MOD";
    return utf8ToWide(std::string(context.family->name)) + L" - " +
           logicalUnitSummaryW(*context.family, context.unitId) + L"  -  MOD";
}

std::wstring logicalFamilyEditSeed(const LogicalFamilyModDialogContext& context,
                                   std::size_t fieldIndex) {
    if (!context.family || fieldIndex >= context.family->fieldCount) return {};
    const auto& field = context.family->fields[fieldIndex];
    if (!gdtv::logicalFieldAvailable(*context.save, field, context.unitId)) return L"<not present>";
    const auto bits = context.save->elementBits(field.key, context.unitId, field.elementIndex);
    if (field.kind == gdtv::LogicalValueKind::Hash) return hexW(bits, 8);
    if (field.kind == gdtv::LogicalValueKind::Signed ||
        field.kind == gdtv::LogicalValueKind::Bitfield) {
        return std::to_wstring(bitCopy<std::int32_t>(static_cast<std::uint32_t>(bits)));
    }
    return std::to_wstring(bits);
}

std::wstring logicalFamilyValueSummary(const LogicalFamilyModDialogContext& context,
                                       std::size_t fieldIndex) {
    if (!context.family || fieldIndex >= context.family->fieldCount) return {};
    const auto& field = context.family->fields[fieldIndex];
    if (!gdtv::logicalFieldAvailable(*context.save, field, context.unitId)) {
        return field.optional ? L"Optional section is not present in this save." : L"Required field is missing.";
    }
    const auto value = static_cast<std::uint32_t>(
        context.save->elementBits(field.key, context.unitId, field.elementIndex));
    if (field.kind == gdtv::LogicalValueKind::Hash) {
        if (!context.hashDatabase) return {};
        const auto category = !field.hashCategoryFilter.empty()
            ? field.hashCategoryFilter : context.family->hashCategoryFilter;
        const auto prefix = !field.hashCategoryFilter.empty()
            ? field.hashCategoryPrefix : context.family->hashCategoryPrefix;
        const auto* entry = category.empty()
            ? context.hashDatabase->preferred(value)
            : context.hashDatabase->preferredMatching(value, category, {});
        (void)prefix;
        if (!entry) return category.empty() ? L"Unknown hash" : L"Unknown or unexpected hash";
        std::wstring result;
        if (!entry->displayName.empty()) result += utf8ToWide(entry->displayName);
        if (!entry->id.empty()) {
            if (!result.empty()) result += L" | ";
            result += utf8ToWide(entry->id);
        }
        return result.empty() ? L"Known hash" : result;
    }
    if (field.kind == gdtv::LogicalValueKind::Bitfield ||
        field.kind == gdtv::LogicalValueKind::State) {
        unsigned count = 0U;
        auto remaining = value;
        while (remaining != 0U) {
            count += remaining & 1U;
            remaining >>= 1U;
        }
        return L"Hex " + hexW(value, 8) + L" | raw " + rawLittleEndianW(value, 4U) +
               L" | " + numberW(count) + L" bit(s) set";
    }
    return L"Hex " + hexW(value, 8) + L" | raw " + rawLittleEndianW(value, 4U);
}

void refreshLogicalFamilyRow(LogicalFamilyModDialogContext& context,
                             std::size_t fieldIndex) {
    if (!context.family || fieldIndex >= context.family->fieldCount) return;
    const auto& field = context.family->fields[fieldIndex];
    const bool available = gdtv::logicalFieldAvailable(*context.save, field, context.unitId);
    SetWindowTextW(context.edits[fieldIndex], logicalFamilyEditSeed(context, fieldIndex).c_str());
    SetWindowTextW(context.resolvedLabels[fieldIndex],
                   logicalFamilyValueSummary(context, fieldIndex).c_str());
    EnableWindow(context.edits[fieldIndex], available ? TRUE : FALSE);
    EnableWindow(context.modButtons[fieldIndex], available ? TRUE : FALSE);
    if (context.hashButtons[fieldIndex]) {
        EnableWindow(context.hashButtons[fieldIndex], available ? TRUE : FALSE);
    }
}

void selectLogicalFamilyEntryInModWindow(LogicalFamilyModDialogContext& context,
                                         gdtv::SaveData& save,
                                         std::uint32_t unitId) {
    context.save = &save;
    context.unitId = unitId;
    const auto title = logicalFamilyWindowTitle(context);
    SetWindowTextW(context.window, title.c_str());
    if (context.titleLabel) SetWindowTextW(context.titleLabel, title.c_str());
    if (context.family) {
        for (std::size_t index = 0; index < context.family->fieldCount; ++index) {
            refreshLogicalFamilyRow(context, index);
        }
    }
    if (context.statusLabel) {
        SetWindowTextW(context.statusLabel,
                       L"Values are edited in memory until Save Edited ... As is used.");
    }
    ShowWindow(context.window, SW_SHOW);
    SetForegroundWindow(context.window);
}

bool applyLogicalFamilyField(LogicalFamilyModDialogContext& context,
                             std::size_t fieldIndex) {
    if (!context.family || fieldIndex >= context.family->fieldCount) return false;
    const auto& field = context.family->fields[fieldIndex];
    if (!gdtv::logicalFieldAvailable(*context.save, field, context.unitId)) return false;
    std::wstring error;
    const auto value = parseSummonModValue(getWindowTextString(context.edits[fieldIndex]),
                                           field, *context.hashDatabase, error);
    const auto dialogTitle = utf8ToWide(std::string(context.family->name)) + L" MOD";
    if (!value) {
        MessageBoxW(context.window, error.c_str(), dialogTitle.c_str(), MB_OK | MB_ICONERROR);
        return false;
    }
    if (field.kind == gdtv::LogicalValueKind::Hash) {
        const auto hash = static_cast<std::uint32_t>(*value);
        const auto category = !field.hashCategoryFilter.empty()
            ? field.hashCategoryFilter : context.family->hashCategoryFilter;
        const bool sentinel = hash == 0U || hash == 0xFFFFFFFFU || gdtv::isGlobalEmptySlotHash(hash);
        if (!category.empty() && !sentinel && context.hashDatabase->find(hash) &&
            !context.hashDatabase->hasMatchingEntry(hash, category, {})) {
            const auto warning = L"The selected hash is known, but it is not categorized as " +
                utf8ToWide(std::string(category)) +
                L". Continue writing it to this logical field?";
            if (MessageBoxW(context.window, warning.c_str(), dialogTitle.c_str(),
                            MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
                return false;
            }
        }
    }
    try {
        context.save->setElementBits(field.key, context.unitId, field.elementIndex, *value);
        context.changed = true;
        refreshLogicalFamilyRow(context, fieldIndex);
        const auto message = legacyLocatorW(field.key) +
            L" modified in memory. Use File > Save Edited ... As.";
        SetWindowTextW(context.statusLabel, message.c_str());
        return true;
    } catch (const std::exception& exception) {
        MessageBoxW(context.window, utf8ToWide(exception.what()).c_str(), dialogTitle.c_str(),
                    MB_OK | MB_ICONERROR);
        return false;
    }
}

LRESULT CALLBACK logicalFamilyModDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* context = reinterpret_cast<LogicalFamilyModDialogContext*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        context = static_cast<LogicalFamilyModDialogContext*>(create->lpCreateParams);
        context->window = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(context));
    }
    if (!context) return DefWindowProcW(hwnd, message, wParam, lParam);

    switch (message) {
    case WM_CREATE: {
        if (!context->family || context->family->fieldCount > kMaxSharedLogicalFields) return -1;
        context->font = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                    DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        const auto title = logicalFamilyWindowTitle(*context);
        SetWindowTextW(hwnd, title.c_str());
        context->titleLabel = addDialogControl(hwnd, L"STATIC", title.c_str(), SS_LEFT,
                                               16, 14, 700, 28, 0, context->font);

        for (std::size_t index = 0; index < context->family->fieldCount; ++index) {
            const auto& field = context->family->fields[index];
            const int y = 52 + static_cast<int>(index) * 72;
            const auto label = legacyLocatorW(field.key) + L" - " +
                               utf8ToWide(std::string(field.label));
            addDialogControl(hwnd, L"STATIC", label.c_str(), SS_LEFT,
                             18, y, 338, 24, 0, context->font);
            context->edits[index] = addDialogControl(
                hwnd, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL,
                360, y - 3, 170, 28, ID_LOGICAL_EDIT_BASE + static_cast<UINT>(index),
                context->font, WS_EX_CLIENTEDGE);
            context->modButtons[index] = addDialogControl(
                hwnd, L"BUTTON", L"MOD", WS_TABSTOP | BS_PUSHBUTTON,
                540, y - 3, 68, 28,
                ID_LOGICAL_MOD_BASE + static_cast<UINT>(index), context->font);
            if (field.kind == gdtv::LogicalValueKind::Hash) {
                context->hashButtons[index] = addDialogControl(
                    hwnd, L"BUTTON", L"Hash List", WS_TABSTOP | BS_PUSHBUTTON,
                    618, y - 3, 100, 28,
                    ID_LOGICAL_HASH_BASE + static_cast<UINT>(index), context->font);
            }
            context->resolvedLabels[index] = addDialogControl(
                hwnd, L"STATIC", L"", SS_LEFT,
                360, y + 29, 358, 30, 0, context->font);
            refreshLogicalFamilyRow(*context, index);
        }

        const int statusY = 62 + static_cast<int>(context->family->fieldCount) * 72;
        context->statusLabel = addDialogControl(
            hwnd, L"STATIC", L"Values are edited in memory until Save Edited ... As is used.",
            SS_LEFT, 18, statusY, 548, 38, 0, context->font);
        addDialogControl(hwnd, L"BUTTON", L"Close", WS_TABSTOP | BS_DEFPUSHBUTTON,
                         630, statusY + 6, 88, 30, IDCANCEL, context->font);
        return 0;
    }
    case WM_COMMAND: {
        const auto id = LOWORD(wParam);
        if (id >= ID_LOGICAL_MOD_BASE &&
            id < ID_LOGICAL_MOD_BASE + static_cast<UINT>(context->family->fieldCount)) {
            applyLogicalFamilyField(*context, id - ID_LOGICAL_MOD_BASE);
            return 0;
        }
        if (id >= ID_LOGICAL_HASH_BASE &&
            id < ID_LOGICAL_HASH_BASE + static_cast<UINT>(context->family->fieldCount)) {
            const auto fieldIndex = static_cast<std::size_t>(id - ID_LOGICAL_HASH_BASE);
            const auto& field = context->family->fields[fieldIndex];
            const auto title = utf8ToWide(std::string(field.label)) + L" - Hash List";
            const auto category = !field.hashCategoryFilter.empty()
                ? field.hashCategoryFilter : context->family->hashCategoryFilter;
            const auto categoryPrefix = !field.hashCategoryFilter.empty()
                ? field.hashCategoryPrefix : context->family->hashCategoryPrefix;
            EnableWindow(context->owner, FALSE);
            const auto selected = showHashListDialog(
                hwnd, *context->hashDatabase, category,
                title.c_str(), categoryPrefix);
            EnableWindow(context->owner, TRUE);
            SetForegroundWindow(hwnd);
            if (selected) {
                SetWindowTextW(context->edits[fieldIndex], hexW(*selected, 8).c_str());
                SendMessageW(context->edits[fieldIndex], EM_SETSEL, 0, -1);
                SetFocus(context->edits[fieldIndex]);
            }
            return 0;
        }
        if (id == IDCANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (context->font) {
            DeleteObject(context->font);
            context->font = nullptr;
        }
        context->window = nullptr;
        if (!context->suppressCloseNotification && context->owner) {
            PostMessageW(context->owner, WM_APP_LOGICAL_MOD_CLOSED,
                         context->changed ? 1U : 0U, 0);
        }
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

HWND createLogicalFamilyModWindow(HWND owner, LogicalFamilyModDialogContext& context) {
    constexpr wchar_t className[] = L"GBFRToolLogicalFamilyModDialog";
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = logicalFamilyModDialogProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = className;
        registered = RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }
    if (!registered || !context.family || context.family->fieldCount > kMaxSharedLogicalFields) {
        return nullptr;
    }

    RECT ownerRect{};
    GetWindowRect(owner, &ownerRect);
    constexpr int width = 760;
    const int height = 170 + static_cast<int>(context.family->fieldCount) * 72;
    const int x = static_cast<int>(ownerRect.left) +
                  std::max(0, (static_cast<int>(ownerRect.right - ownerRect.left) - width) / 2);
    const int y = static_cast<int>(ownerRect.top) +
                  std::max(0, (static_cast<int>(ownerRect.bottom - ownerRect.top) - height) / 2);

    context.owner = owner;
    const auto title = utf8ToWide(std::string(context.family->name)) + L" MOD";
    HWND dialog = CreateWindowExW(WS_EX_TOOLWINDOW, className, title.c_str(),
                                  WS_POPUP | WS_CAPTION | WS_SYSMENU,
                                  x, y, width, height, owner, nullptr,
                                  GetModuleHandleW(nullptr), &context);
    if (!dialog) return nullptr;
    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);
    return dialog;
}

enum class Role { Primary, Compare };
enum class NodeKind {
    Dummy,
    Message,
    SaveRoot,
    RootInfo,
    VectorsRoot,
    Vector,
    Key,
    Page,
    Record,
    LinkedRoot,
    LinkedCluster,
    ComparisonRoot,
    ComparisonCategory,
    ComparisonKey,
    SearchRoot,
    SearchKey,
    HashRoot,
    HashFriendlyRoot,
    HashUserRoot,
    HashEntry,
    HashOccurrence,
    LogicalRoot,
    LogicalFamily,
    LogicalCharacter,
    LogicalPage,
    LogicalSlot,
    LogicalField,
    CurioEntriesRoot,
    CurioSlot,
    RelationshipRoot,
    RelationshipPhysicalRoot,
    RelationshipPhysicalPage,
    RelationshipPhysicalSection,
    RelationshipFamiliesRoot,
    RelationshipFamily,
    RelationshipMember,
    RelationshipReferencesRoot,
    RelationshipReferenceSection,
    RelationshipReferenceCategory,
    MasteryRoot,
    MasteryCharacter,
    MasteryPage,
    MasteryEntry,
    MasteryField
};
enum class CompareCategory { Changed = 0, PrimaryOnly = 1, CompareOnly = 2, Unchanged = 3 };

struct NodeData {
    NodeKind kind{NodeKind::Message};
    Role role{Role::Primary};
    gdtv::SaveData* save{};
    std::uint32_t vectorNumber{};
    std::uint32_t key{};
    std::size_t start{};
    std::size_t end{};
    std::size_t recordOrdinal{};
    std::size_t clusterIndex{};
    CompareCategory category{CompareCategory::Changed};
    std::uint32_t hashValue{};
    std::uint32_t unitId{};
    std::uint32_t logicalFieldIndex{};
    std::uint32_t logicalFamilyAnchor{};
    std::uint32_t characterGroup{};
    std::uint32_t logicalNamespace{};
    bool sharedGroup{};
    std::size_t relationshipIndex{};
    std::size_t relationshipSubIndex{};
};

struct RelationshipCache {
    std::vector<gdtv::PhysicalSectionInfo> physical;
    std::vector<gdtv::RelationshipFamilyInfo> families;
    std::vector<gdtv::ReferenceSectionInfo> references;
    bool referencesBuilt{};
};

class MainWindow {
public:
    explicit MainWindow(HINSTANCE instance) : instance_(instance) {}

    bool create() {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = &MainWindow::windowProc;
        windowClass.hInstance = instance_;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(1));
        if (!windowClass.hIcon) windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        windowClass.hIconSm = windowClass.hIcon;
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        windowClass.lpszClassName = kWindowClass;
        if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

        const std::wstring title = std::wstring(GDTV_APP_NAME_W) + L" v" + GDTV_APP_VERSION_W;
        hwnd_ = CreateWindowExW(0, kWindowClass, title.c_str(), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                CW_USEDEFAULT, CW_USEDEFAULT, 1440, 880, nullptr, nullptr, instance_, this);
        return hwnd_ != nullptr;
    }

    HWND hwnd() const noexcept { return hwnd_; }
    HWND summonModWindow() const noexcept { return summonModWindow_; }
    HWND masteryModWindow() const noexcept { return masteryModWindow_; }

    void show(int command) {
        ShowWindow(hwnd_, command);
        UpdateWindow(hwnd_);
    }

    void loadStartupArguments() {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (!argv) return;
        std::filesystem::path mapPath;
        std::filesystem::path characterMapPath;
        std::filesystem::path sectionNamesPath;
        std::vector<std::filesystem::path> saves;
        for (int i = 1; i < argc; ++i) {
            const std::wstring arg = argv[i];
            if (arg == L"--map" && i + 1 < argc) mapPath = argv[++i];
            else if (arg == L"--character-map" && i + 1 < argc) characterMapPath = argv[++i];
            else if (arg == L"--section-names" && i + 1 < argc) sectionNamesPath = argv[++i];
            else if (arg != L"--scan-only") saves.emplace_back(arg);
        }
        LocalFree(argv);

        if (!mapPath.empty()) loadSectionMap(mapPath, false);
        if (!characterMapPath.empty()) {
            try {
                characterSections_.load(characterMapPath, true);
                characterSectionsPath_ = characterMapPath;
            } catch (const std::exception&) {
                // Startup continues with fallback Character Group labels.
            }
        }
        if (!sectionNamesPath.empty()) {
            try {
                sectionNames_.load(sectionNamesPath, true);
                sectionNamesPath_ = sectionNamesPath;
            } catch (const std::exception&) {
                // Startup continues with the structural section map names.
            }
        }
        if (!saves.empty()) openSave(saves[0], Role::Primary);
        if (saves.size() >= 2) openSave(saves[1], Role::Compare);
    }

private:
    HINSTANCE instance_{};
    HWND hwnd_{};
    HWND openPrimaryButton_{};
    HWND openCompareButton_{};
    HWND loadMapButton_{};
    HWND searchEdit_{};
    HWND searchButton_{};
    HWND clearSearchButton_{};
    HWND tree_{};
    HWND copyLocatorButton_{};
    HWND exportButton_{};
    HWND addHashButton_{};
    HWND editValueButton_{};
    HWND tabs_{};
    HWND detailsEdit_{};
    HWND hexEdit_{};
    HWND valuesEdit_{};
    HWND status_{};
    HWND summonModWindow_{};
    HWND masteryModWindow_{};
    HWND logicalModWindow_{};
    HFONT uiFont_{};
    HFONT monoFont_{};

    std::unique_ptr<gdtv::SaveData> primary_;
    std::unique_ptr<gdtv::SaveData> compare_;
    std::unique_ptr<SummonSlotModDialogContext> summonModContext_;
    std::unique_ptr<MasteryTreeModDialogContext> masteryModContext_;
    std::unique_ptr<LogicalFamilyModDialogContext> logicalModContext_;
    gdtv::SectionMap sectionMap_;
    gdtv::SectionNameMap sectionNames_;
    gdtv::CharacterSectionMap characterSections_;
    gdtv::HashDatabase hashDatabase_;
    std::filesystem::path hashDatabasePath_;
    std::filesystem::path characterSectionsPath_;
    std::filesystem::path sectionNamesPath_;
    std::vector<std::unique_ptr<NodeData>> nodes_;
    std::vector<std::pair<Role, std::uint32_t>> searchResults_;
    std::array<std::vector<std::uint32_t>, 4> compareKeys_;
    std::map<const gdtv::SaveData*, RelationshipCache> relationshipCaches_;
    bool showDetailedLogicalInfo_{};
    bool logicalOnlyView_{true};

    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        MainWindow* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<MainWindow*>(create->lpCreateParams);
            self->hwnd_ = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        return self ? self->handleMessage(message, wParam, lParam)
                    : DefWindowProcW(hwnd, message, wParam, lParam);
    }

    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_CREATE:
            createControls();
            createMenuBar();
            DragAcceptFiles(hwnd_, TRUE);
            loadBundledSectionMap();
            loadBundledSectionNames();
            loadBundledCharacterSections();
            loadBundledHashDatabases();
            rebuildTree();
            setStatus(L"Drop one save to open it as Primary, or drop two saves for Primary + Comparison.");
            return 0;
        case WM_SIZE:
            layoutControls(LOWORD(lParam), HIWORD(lParam));
            return 0;
        case WM_COMMAND:
            handleCommand(LOWORD(wParam));
            return 0;
        case WM_NOTIFY:
            return handleNotify(reinterpret_cast<NMHDR*>(lParam));
        case WM_DROPFILES:
            handleDroppedFiles(reinterpret_cast<HDROP>(wParam));
            return 0;
        case WM_APP_SUMMON_MOD_CLOSED: {
            const bool changed = wParam != 0U;
            summonModWindow_ = nullptr;
            summonModContext_.reset();
            if (changed) {
                rebuildTree();
                setStatus(L"Summon slot edits are in memory - use File > Save Edited ... As");
            }
            return 0;
        }
        case WM_APP_MASTERY_MOD_CLOSED: {
            const bool changed = wParam != 0U;
            masteryModWindow_ = nullptr;
            masteryModContext_.reset();
            if (changed) {
                rebuildTree();
                setStatus(L"Mastery-tree edits are in memory - use File > Save Edited ... As");
            }
            return 0;
        }
        case WM_APP_LOGICAL_MOD_CLOSED: {
            const bool changed = wParam != 0U;
            logicalModWindow_ = nullptr;
            logicalModContext_.reset();
            if (changed) {
                rebuildTree();
                setStatus(L"Logical-record edits are in memory - use File > Save Edited ... As");
            }
            return 0;
        }
        case WM_SETFOCUS:
            SetFocus(tree_);
            return 0;
        case WM_CLOSE:
            if (confirmDiscardAllEdits()) DestroyWindow(hwnd_);
            return 0;
        case WM_DESTROY:
            closeSummonModWindow();
            closeMasteryModWindow();
            closeLogicalModWindow();
            DragAcceptFiles(hwnd_, FALSE);
            if (uiFont_) DeleteObject(uiFont_);
            if (monoFont_) DeleteObject(monoFont_);
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd_, message, wParam, lParam);
        }
    }

    void createControls() {
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_TREEVIEW_CLASSES | ICC_TAB_CLASSES |
                                                       ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
        InitCommonControlsEx(&controls);

        uiFont_ = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        monoFont_ = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                FIXED_PITCH | FF_MODERN, L"Consolas");

        openPrimaryButton_ = createControl(L"BUTTON", L"Open Save", WS_TABSTOP | BS_PUSHBUTTON,
                                           ID_BTN_OPEN_PRIMARY);
        openCompareButton_ = createControl(L"BUTTON", L"Open Compare", WS_TABSTOP | BS_PUSHBUTTON,
                                           ID_BTN_OPEN_COMPARE);
        loadMapButton_ = createControl(L"BUTTON", L"Load Section Map", WS_TABSTOP | BS_PUSHBUTTON,
                                      ID_BTN_LOAD_MAP);
        createControl(L"STATIC", L"Find:", SS_LEFT, 0);
        searchEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                      ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd_, controlId(ID_EDIT_SEARCH),
                                      instance_, nullptr);
        searchButton_ = createControl(L"BUTTON", L"Search", WS_TABSTOP | BS_PUSHBUTTON, ID_BTN_SEARCH);
        clearSearchButton_ = createControl(L"BUTTON", L"Clear Results", WS_TABSTOP | BS_PUSHBUTTON,
                                           ID_BTN_CLEAR_SEARCH);

        tree_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASBUTTONS | TVS_HASLINES |
                                TVS_LINESATROOT | TVS_SHOWSELALWAYS | TVS_DISABLEDRAGDROP,
                                0, 0, 0, 0, hwnd_, controlId(ID_TREE), instance_, nullptr);
        copyLocatorButton_ = createControl(L"BUTTON", L"Copy Stable Locator", WS_TABSTOP | BS_PUSHBUTTON,
                                           ID_BTN_COPY_LOCATOR);
        exportButton_ = createControl(L"BUTTON", L"Export Payload", WS_TABSTOP | BS_PUSHBUTTON,
                                      ID_BTN_EXPORT);
        addHashButton_ = createControl(L"BUTTON", L"Add Hash Mapping", WS_TABSTOP | BS_PUSHBUTTON,
                                       ID_BTN_ADD_HASH);
        editValueButton_ = createControl(L"BUTTON", L"Edit Current Value", WS_TABSTOP | BS_PUSHBUTTON,
                                         ID_BTN_EDIT_VALUE);
        tabs_ = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP,
                                0, 0, 0, 0, hwnd_, controlId(ID_TAB), instance_, nullptr);
        TCITEMW tab{};
        tab.mask = TCIF_TEXT;
        tab.pszText = const_cast<wchar_t*>(L"Details");
        TabCtrl_InsertItem(tabs_, 0, &tab);
        tab.pszText = const_cast<wchar_t*>(L"Payload Hex");
        TabCtrl_InsertItem(tabs_, 1, &tab);
        tab.pszText = const_cast<wchar_t*>(L"Decoded Values / Hashes");
        TabCtrl_InsertItem(tabs_, 2, &tab);

        detailsEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                      WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_MULTILINE |
                                      ES_AUTOVSCROLL | ES_READONLY,
                                      0, 0, 0, 0, hwnd_, controlId(ID_DETAILS), instance_, nullptr);
        hexEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                  WS_CHILD | WS_VSCROLL | WS_HSCROLL | ES_LEFT | ES_MULTILINE |
                                  ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_READONLY,
                                  0, 0, 0, 0, hwnd_, controlId(ID_HEX), instance_, nullptr);
        valuesEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                     WS_CHILD | WS_VSCROLL | WS_HSCROLL | ES_LEFT | ES_MULTILINE |
                                     ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_READONLY,
                                     0, 0, 0, 0, hwnd_, controlId(ID_VALUES), instance_, nullptr);
        status_ = CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
                                  0, 0, 0, 0, hwnd_, controlId(ID_STATUS), instance_, nullptr);

        for (const HWND control : {openPrimaryButton_, openCompareButton_, loadMapButton_, searchEdit_,
                                   searchButton_, clearSearchButton_, tree_, copyLocatorButton_, exportButton_,
                                   addHashButton_, editValueButton_, tabs_, detailsEdit_, status_}) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
        }
        SendMessageW(hexEdit_, WM_SETFONT, reinterpret_cast<WPARAM>(monoFont_), TRUE);
        SendMessageW(valuesEdit_, WM_SETFONT, reinterpret_cast<WPARAM>(monoFont_), TRUE);
        SendMessageW(detailsEdit_, EM_SETLIMITTEXT, 0x7FFFFFFE, 0);
        SendMessageW(hexEdit_, EM_SETLIMITTEXT, 0x7FFFFFFE, 0);
        SendMessageW(valuesEdit_, EM_SETLIMITTEXT, 0x7FFFFFFE, 0);
    }

    HWND createControl(const wchar_t* className, const wchar_t* text, DWORD style, UINT id) {
        return CreateWindowExW(0, className, text, WS_CHILD | WS_VISIBLE | style,
                               0, 0, 0, 0, hwnd_, controlId(id),
                               instance_, nullptr);
    }

    void createMenuBar() {
        HMENU menu = CreateMenu();
        HMENU file = CreatePopupMenu();
        AppendMenuW(file, MF_STRING, ID_FILE_OPEN_PRIMARY, L"&Open Primary Save...\tCtrl+O");
        AppendMenuW(file, MF_STRING, ID_FILE_OPEN_COMPARE, L"Open &Comparison Save...\tCtrl+Shift+O");
        AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(file, MF_STRING, ID_FILE_SAVE_PRIMARY_AS, L"Save Edited &Primary As...");
        AppendMenuW(file, MF_STRING, ID_FILE_SAVE_COMPARE_AS, L"Save Edited C&omparison As...");
        AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(file, MF_STRING, ID_FILE_LOAD_MAP, L"Load Section &Map CSV...");
        AppendMenuW(file, MF_STRING, ID_FILE_LOAD_SECTION_NAMES, L"Load Section &Names Text File...");
        AppendMenuW(file, MF_STRING, ID_FILE_LOAD_CHARACTER_SECTIONS, L"Load Character Names Text File...");
        AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(file, MF_STRING, ID_FILE_EXPORT_PAYLOAD, L"&Export Selected Payload...");
        AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(file, MF_STRING, ID_FILE_EXIT, L"E&xit");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"&File");

        HMENU edit = CreatePopupMenu();
        AppendMenuW(edit, MF_STRING, ID_EDIT_CURRENT_VALUE, L"&Edit Current Value...	Enter");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(edit), L"&Edit");

        HMENU view = CreatePopupMenu();
        AppendMenuW(view, MF_STRING, ID_VIEW_EXPAND, L"&Expand Selected");
        AppendMenuW(view, MF_STRING, ID_VIEW_COLLAPSE, L"&Collapse Selected");
        AppendMenuW(view, MF_STRING, ID_VIEW_REFRESH, L"&Refresh Tree");
        AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(view, MF_STRING, ID_VIEW_DETAILED_LOGICAL_INFO,
                    L"Show Detailed Logical Field &Info");
        AppendMenuW(view, MF_STRING | MF_CHECKED, ID_VIEW_LOGICAL_ONLY,
                    L"Show Only &Logical Save Records");
        AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(view, MF_STRING, ID_VIEW_RELOAD_SECTION_NAMES,
                    L"Reload Bundled Section &Names");
        AppendMenuW(view, MF_STRING, ID_VIEW_RELOAD_CHARACTER_SECTIONS,
                    L"Reload Bundled Character Names");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(view), L"&View");

        HMENU hashes = CreatePopupMenu();
        AppendMenuW(hashes, MF_STRING, ID_HASH_LOAD_DATABASE, L"Load &Hash Database Text File...");
        AppendMenuW(hashes, MF_STRING, ID_HASH_RELOAD, L"&Reload Bundled Hash Database");
        AppendMenuW(hashes, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hashes, MF_STRING, ID_HASH_ADD, L"&Add / Update Mapping...");
        AppendMenuW(hashes, MF_STRING, ID_HASH_SAVE_DATABASE, L"&Save Hash Database");
        AppendMenuW(hashes, MF_STRING, ID_HASH_EXPORT_UNRESOLVED, L"Export &Unresolved UInt Candidates...");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(hashes), L"&Hashes");

        HMENU help = CreatePopupMenu();
        AppendMenuW(help, MF_STRING, ID_HELP_ABOUT, L"&About");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(help), L"&Help");
        SetMenu(hwnd_, menu);
    }

    void layoutControls(int clientWidth, int clientHeight) {
        SendMessageW(status_, WM_SIZE, 0, 0);
        RECT statusRect{};
        GetWindowRect(status_, &statusRect);
        const int measuredStatusHeight = static_cast<int>(statusRect.bottom - statusRect.top);
        const int statusHeight = std::max(measuredStatusHeight, kStatusHeightFallback);
        const int contentBottom = std::max(kToolbarHeight, clientHeight - statusHeight);

        int x = kMargin;
        const int buttonY = 7;
        MoveWindow(openPrimaryButton_, x, buttonY, 92, 28, TRUE); x += 96;
        MoveWindow(openCompareButton_, x, buttonY, 108, 28, TRUE); x += 112;
        MoveWindow(loadMapButton_, x, buttonY, 132, 28, TRUE); x += 142;

        HWND findLabel = GetWindow(hwnd_, GW_CHILD);
        while (findLabel && (GetDlgCtrlID(findLabel) != 0 || std::wstring(L"STATIC") != className(findLabel))) {
            findLabel = GetWindow(findLabel, GW_HWNDNEXT);
        }
        if (findLabel) MoveWindow(findLabel, x, 11, 36, 22, TRUE);
        x += 38;
        const int remaining = std::max(180, clientWidth - x - 190);
        MoveWindow(searchEdit_, x, buttonY, remaining, 28, TRUE); x += remaining + 4;
        MoveWindow(searchButton_, x, buttonY, 72, 28, TRUE); x += 76;
        MoveWindow(clearSearchButton_, x, buttonY, 106, 28, TRUE);

        const int contentY = kToolbarHeight;
        const int contentHeight = std::max(0, contentBottom - contentY);
        const int leftWidth = std::clamp(clientWidth * 44 / 100, 360, std::max(360, clientWidth - 480));
        MoveWindow(tree_, kMargin, contentY, std::max(0, leftWidth - kMargin * 2),
                   std::max(0, contentHeight - kMargin), TRUE);

        const int rightX = leftWidth + kMargin;
        const int rightWidth = std::max(0, clientWidth - rightX - kMargin);
        MoveWindow(copyLocatorButton_, rightX, contentY, 150, 28, TRUE);
        MoveWindow(exportButton_, rightX + 156, contentY, 120, 28, TRUE);
        MoveWindow(addHashButton_, rightX + 282, contentY, 140, 28, TRUE);
        MoveWindow(editValueButton_, rightX + 428, contentY, 138, 28, TRUE);
        MoveWindow(tabs_, rightX, contentY + kActionBarHeight, rightWidth,
                   std::max(0, contentHeight - kActionBarHeight - kMargin), TRUE);

        RECT page{0, 0, rightWidth, std::max(0, contentHeight - kActionBarHeight - kMargin)};
        TabCtrl_AdjustRect(tabs_, FALSE, &page);
        const int pageX = rightX + static_cast<int>(page.left);
        const int pageY = contentY + kActionBarHeight + static_cast<int>(page.top);
        const int pageWidth = std::max(0, static_cast<int>(page.right - page.left));
        const int pageHeight = std::max(0, static_cast<int>(page.bottom - page.top));
        MoveWindow(detailsEdit_, pageX, pageY, pageWidth, pageHeight, TRUE);
        MoveWindow(hexEdit_, pageX, pageY, pageWidth, pageHeight, TRUE);
        MoveWindow(valuesEdit_, pageX, pageY, pageWidth, pageHeight, TRUE);
        updateTabVisibility();
    }

    static std::wstring className(HWND hwnd) {
        wchar_t buffer[64]{};
        GetClassNameW(hwnd, buffer, static_cast<int>(std::size(buffer)));
        return buffer;
    }

    void updateTabVisibility() {
        const int selected = TabCtrl_GetCurSel(tabs_);
        ShowWindow(detailsEdit_, selected == 0 ? SW_SHOW : SW_HIDE);
        ShowWindow(hexEdit_, selected == 1 ? SW_SHOW : SW_HIDE);
        ShowWindow(valuesEdit_, selected == 2 ? SW_SHOW : SW_HIDE);
    }

    void handleCommand(UINT id) {
        switch (id) {
        case ID_FILE_OPEN_PRIMARY:
        case ID_BTN_OPEN_PRIMARY:
            chooseSave(Role::Primary); break;
        case ID_FILE_OPEN_COMPARE:
        case ID_BTN_OPEN_COMPARE:
            chooseSave(Role::Compare); break;
        case ID_FILE_SAVE_PRIMARY_AS:
            saveEditedSave(Role::Primary); break;
        case ID_FILE_SAVE_COMPARE_AS:
            saveEditedSave(Role::Compare); break;
        case ID_EDIT_CURRENT_VALUE:
        case ID_BTN_EDIT_VALUE:
            editSelectedValue(); break;
        case ID_FILE_LOAD_MAP:
        case ID_BTN_LOAD_MAP:
            chooseSectionMap(); break;
        case ID_FILE_LOAD_SECTION_NAMES:
            chooseSectionNames(); break;
        case ID_FILE_LOAD_CHARACTER_SECTIONS:
            chooseCharacterSections(); break;
        case ID_FILE_EXPORT_PAYLOAD:
        case ID_BTN_EXPORT:
            exportPayload(); break;
        case ID_BTN_COPY_LOCATOR:
            copyStableLocator(); break;
        case ID_HASH_LOAD_DATABASE:
            chooseHashDatabase(); break;
        case ID_HASH_RELOAD:
            loadBundledHashDatabases(true); rebuildTree(); break;
        case ID_HASH_SAVE_DATABASE:
            saveHashDatabase(); break;
        case ID_HASH_EXPORT_UNRESOLVED:
            exportUnresolvedHashCandidates(); break;
        case ID_HASH_ADD:
        case ID_BTN_ADD_HASH:
            addHashMapping(); break;
        case ID_BTN_SEARCH:
            runSearch(); break;
        case ID_BTN_CLEAR_SEARCH:
            clearSearch(); break;
        case ID_VIEW_EXPAND:
            if (const auto selected = TreeView_GetSelection(tree_)) TreeView_Expand(tree_, selected, TVE_EXPAND);
            break;
        case ID_VIEW_COLLAPSE:
            if (const auto selected = TreeView_GetSelection(tree_)) TreeView_Expand(tree_, selected, TVE_COLLAPSE);
            break;
        case ID_VIEW_REFRESH:
            rebuildTree(); break;
        case ID_VIEW_RELOAD_SECTION_NAMES:
            loadBundledSectionNames(true); rebuildTree(); showSelectedNode(); break;
        case ID_VIEW_RELOAD_CHARACTER_SECTIONS:
            loadBundledCharacterSections(true); rebuildTree(); break;
        case ID_VIEW_DETAILED_LOGICAL_INFO:
            showDetailedLogicalInfo_ = !showDetailedLogicalInfo_;
            CheckMenuItem(GetMenu(hwnd_), ID_VIEW_DETAILED_LOGICAL_INFO,
                          MF_BYCOMMAND | (showDetailedLogicalInfo_ ? MF_CHECKED : MF_UNCHECKED));
            showSelectedNode();
            break;
        case ID_VIEW_LOGICAL_ONLY:
            logicalOnlyView_ = !logicalOnlyView_;
            CheckMenuItem(GetMenu(hwnd_), ID_VIEW_LOGICAL_ONLY,
                          MF_BYCOMMAND | (logicalOnlyView_ ? MF_CHECKED : MF_UNCHECKED));
            rebuildTree();
            break;
        case ID_HELP_ABOUT:
            MessageBoxW(hwnd_,
                        GDTV_APP_NAME_W L" v" GDTV_APP_VERSION_W L"\n"
                        GDTV_APP_SHORT_NAME_W L"\n\n"
                        L"Native C++17 TreeView for FlatBuffers-style GameData saves.\n\n"
                        L"Features typed SaveData decoding, editable section-name metadata, subsystem grouping,\n"
                        L"save relationship mapping, section-aware hash validation, logical editors, comparison, and export.\n\n"
                        L"Hash algorithm/database logic adapted from GBFRDataTools (MIT).",
                        L"About", MB_OK | MB_ICONINFORMATION);
            break;
        case ID_FILE_EXIT:
            DestroyWindow(hwnd_); break;
        default:
            break;
        }
    }

    LRESULT handleNotify(const NMHDR* notification) {
        if (!notification) return 0;
        if (notification->hwndFrom == tree_) {
            if (notification->code == TVN_ITEMEXPANDINGW) {
                const auto* expanding = reinterpret_cast<const NMTREEVIEWW*>(notification);
                if (expanding->action == TVE_EXPAND) populateIfNeeded(expanding->itemNew.hItem);
            } else if (notification->code == TVN_SELCHANGEDW) {
                showSelectedNode();
            } else if (notification->code == NM_CLICK) {
                handleTreeClick();
            } else if (notification->code == NM_DBLCLK) {
                handleTreeDoubleClick();
            }
        } else if (notification->hwndFrom == tabs_ && notification->code == TCN_SELCHANGE) {
            updateTabVisibility();
        }
        return 0;
    }

    void setStatus(const std::wstring& text) {
        SendMessageW(status_, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(text.c_str()));
    }

    void setBusy(bool busy, const std::wstring& text = {}) {
        if (!text.empty()) setStatus(text);
        SetCursor(LoadCursorW(nullptr, busy ? IDC_WAIT : IDC_ARROW));
        EnableWindow(openPrimaryButton_, !busy);
        EnableWindow(openCompareButton_, !busy);
        EnableWindow(loadMapButton_, !busy);
        EnableWindow(addHashButton_, !busy);
        EnableWindow(editValueButton_, !busy);
        RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    }

    void loadBundledSectionMap() {
        const auto path = gdtv::locateBundledDataFile(
            executableDirectory(), L"GameData-Section-Cross-Reference.csv");
        if (std::filesystem::exists(path)) loadSectionMap(path, false);
    }

    void loadBundledSectionNames(bool notify = false) {
        const auto path = gdtv::locateBundledDataFile(
            executableDirectory(), L"GBFR-Section-Names.txt");
        try {
            std::size_t count = 0U;
            if (std::filesystem::exists(path)) {
                count = sectionNames_.load(path, true);
                sectionNamesPath_ = path;
            }
            if (notify) {
                const auto message = L"Section names: " + numberW(count) + L" entries loaded";
                MessageBoxW(hwnd_, message.c_str(), L"Section Names", MB_OK | MB_ICONINFORMATION);
            }
        } catch (const std::exception& error) {
            if (notify) showError(hwnd_, L"Could not load section names:\n\n" +
                                   utf8ToWide(error.what()));
        }
    }

    void chooseSectionNames() {
        const auto path = openFileDialog(hwnd_, L"Open GBFR section names",
                                         L"Text files (*.txt)\0*.txt\0All files\0*.*\0\0");
        if (!path) return;
        try {
            const auto count = sectionNames_.load(*path, true);
            sectionNamesPath_ = *path;
            rebuildTree();
            showSelectedNode();
            setStatus(L"Loaded " + numberW(count) + L" section names");
        } catch (const std::exception& error) {
            showError(hwnd_, L"Could not load section names:\n\n" + utf8ToWide(error.what()));
        }
    }

    void loadBundledCharacterSections(bool notify = false) {
        const auto path = gdtv::locateBundledDataFile(
            executableDirectory(), L"GBFR-Character-Sections.txt");
        try {
            std::size_t count = 0U;
            if (std::filesystem::exists(path)) {
                count = characterSections_.load(path, true);
                characterSectionsPath_ = path;
            }
            if (notify) {
                const auto message = L"Character section names: " + numberW(count) + L" groups loaded";
                MessageBoxW(hwnd_, message.c_str(), L"Character Names", MB_OK | MB_ICONINFORMATION);
            }
        } catch (const std::exception& error) {
            if (notify) showError(hwnd_, L"Could not load character section names:\n\n" +
                                   utf8ToWide(error.what()));
        }
    }

    void chooseCharacterSections() {
        const auto path = openFileDialog(hwnd_, L"Open GBFR character section names",
                                         L"Text files (*.txt)\0*.txt\0All files\0*.*\0\0");
        if (!path) return;
        try {
            const auto count = characterSections_.load(*path, true);
            characterSectionsPath_ = *path;
            rebuildTree();
            showSelectedNode();
            setStatus(L"Loaded " + numberW(count) + L" character section names");
        } catch (const std::exception& error) {
            showError(hwnd_, L"Could not load character section names:\n\n" + utf8ToWide(error.what()));
        }
    }

    void loadBundledHashDatabases(bool notify = false) {
        const auto path = gdtv::locateBundledDataFile(
            executableDirectory(), L"GBFR-Hash-Database.txt");
        try {
            std::size_t count = 0;
            if (std::filesystem::exists(path)) {
                count = hashDatabase_.loadDatabase(path, true);
                hashDatabasePath_ = path;
            }
            const auto message = L"Hash database: " + numberW(count) + L" rows, " +
                                 numberW(hashDatabase_.uniqueHashCount()) + L" unique hashes, " +
                                 numberW(hashDatabase_.friendlyEntryCount()) + L" in-game names";
            setStatus(message);
            if (notify) MessageBoxW(hwnd_, message.c_str(), L"Hash Database", MB_OK | MB_ICONINFORMATION);
        } catch (const std::exception& error) {
            if (notify) showError(hwnd_, L"Could not load hash database:\n\n" + utf8ToWide(error.what()));
            else setStatus(L"Hash database could not be loaded: " + utf8ToWide(error.what()));
        }
    }

    void chooseHashDatabase() {
        const auto path = openFileDialog(hwnd_, L"Open unified GBFR hash database",
                                         L"Text files (*.txt)\0*.txt\0All files\0*.*\0\0");
        if (!path) return;
        try {
            const auto count = hashDatabase_.loadDatabase(*path, true);
            hashDatabasePath_ = *path;
            rebuildTree();
            showSelectedNode();
            setStatus(L"Loaded " + numberW(count) + L" hash database rows");
        } catch (const std::exception& error) {
            showError(hwnd_, L"Could not load hash database:\n\n" + utf8ToWide(error.what()));
        }
    }

    void saveHashDatabase() {
        if (hashDatabasePath_.empty()) {
            hashDatabasePath_ = gdtv::preferredBundledDataFile(
                executableDirectory(), L"GBFR-Hash-Database.txt");
        }
        try {
            hashDatabase_.saveDatabase(hashDatabasePath_);
            setStatus(L"Saved " + numberW(hashDatabase_.databaseEntryCount()) + L" rows to " +
                      hashDatabasePath_.filename().wstring());
        } catch (const std::exception& error) {
            showError(hwnd_, L"Could not save hash database:\n\n" + utf8ToWide(error.what()));
        }
    }

    void exportUnresolvedHashCandidates() {
        if (!primary_ && !compare_) {
            showError(hwnd_, L"Open at least one save before scanning unresolved UInt candidates.");
            return;
        }
        const auto path = saveFileDialog(
            hwnd_, L"Export unresolved UInt/hash candidates", L"GBFR-Unresolved-Hash-Candidates.csv",
            L"CSV files (*.csv)\0*.csv\0All files\0*.*\0\0", L"csv");
        if (!path) return;

        struct CombinedCandidate {
            std::uint64_t occurrences{};
            std::vector<std::string> examples;
        };
        std::map<std::uint32_t, CombinedCandidate> candidates;
        const auto collect = [&](const gdtv::SaveData* save, std::string_view role) {
            if (!save) return;
            const auto values = save->collectUIntValues(6);
            for (const auto& [value, summary] : values) {
                if (value == 0U || value == 0xFFFFFFFFU || hashDatabase_.find(value)) continue;
                auto& combined = candidates[value];
                combined.occurrences += summary.occurrences;
                for (const auto& occurrence : summary.examples) {
                    if (combined.examples.size() >= 10U) break;
                    const auto* group = save->findGroup(occurrence.key);
                    std::ostringstream location;
                    location << role << " V" << (group ? group->vectorNumber : 7U)
                             << ":0x" << std::uppercase << std::hex << std::setw(4) << std::setfill('0')
                             << occurrence.key << std::dec << ':' << occurrence.unitId
                             << '[' << occurrence.elementIndex << ']';
                    combined.examples.push_back(location.str());
                }
            }
        };
        collect(primary_.get(), "Primary");
        collect(compare_.get(), "Comparison");

        try {
            std::ofstream output(*path, std::ios::binary | std::ios::trunc);
            if (!output) throw std::runtime_error("could not create candidate CSV");
            output << "Hash Hex,Raw Little Endian,ID,Display Name,Category,Source,Notes,Occurrences,Example Locations\n";
            for (const auto& [value, candidate] : candidates) {
                std::ostringstream examples;
                for (std::size_t i = 0; i < candidate.examples.size(); ++i) {
                    if (i) examples << " | ";
                    examples << candidate.examples[i];
                }
                output << gdtv::hashHex(value) << ',' << gdtv::hashRawLittleEndian(value)
                       << ",,,,Unresolved UInt scan,"
                       << "\"Review before naming; UInt values can also be counters, flags, or bitfields.\"," 
                       << candidate.occurrences << ",\"" << examples.str() << "\"\n";
            }
            if (!output) throw std::runtime_error("could not write complete candidate CSV");
            setStatus(L"Exported " + numberW(candidates.size()) + L" unresolved UInt candidates to " +
                      path->filename().wstring());
            MessageBoxW(hwnd_,
                        (L"Exported " + numberW(candidates.size()) +
                         L" unresolved UInt candidates.\n\nFill in ID and/or Display Name for confirmed hashes, "
                         L"then load the CSV through Hashes > Load User Hash Map.")
                            .c_str(),
                        L"Unresolved Hash Candidates", MB_OK | MB_ICONINFORMATION);
        } catch (const std::exception& error) {
            showError(hwnd_, L"Could not export unresolved hash candidates:\n\n" + utf8ToWide(error.what()));
        }
    }

    std::optional<std::uint32_t> suggestedHashFromSelection() const {
        if (const auto* data = nodeData(TreeView_GetSelection(tree_)); data && data->kind == NodeKind::HashOccurrence) {
            return data->hashValue;
        }
        const auto recordSelection = selectedRecord();
        if (!recordSelection) return std::nullopt;
        auto* save = std::get<1>(*recordSelection);
        const auto* group = std::get<2>(*recordSelection);
        const auto* record = std::get<3>(*recordSelection);
        if (!save || !group || group->valueType() != gdtv::ValueType::UInt || record->elementCount == 0) {
            return std::nullopt;
        }
        const auto payload = save->payloadView(*record);
        if (payload.size() < 4U) return std::nullopt;
        const auto* bytes = reinterpret_cast<const unsigned char*>(payload.data());
        return static_cast<std::uint32_t>(bytes[0]) |
               (static_cast<std::uint32_t>(bytes[1]) << 8U) |
               (static_cast<std::uint32_t>(bytes[2]) << 16U) |
               (static_cast<std::uint32_t>(bytes[3]) << 24U);
    }

    void addHashMapping() {
        const auto mapping = showHashMappingDialog(hwnd_, suggestedHashFromSelection());
        if (!mapping) return;
        hashDatabase_.addOrUpdateUser(*mapping);
        saveHashDatabase();
        rebuildTree();
        showSelectedNode();
        setStatus(L"Mapped 0x" + utf8ToWide(gdtv::hashHex(mapping->hash)) + L" to " +
                  utf8ToWide(mapping->displayName.empty() ? mapping->id : mapping->displayName));
    }


    bool confirmDiscardRole(Role role) const {
        const auto* save = role == Role::Primary ? primary_.get() : compare_.get();
        if (!save || !save->dirty()) return true;
        const auto roleName = role == Role::Primary ? L"primary" : L"comparison";
        const auto message = std::wstring(L"The ") + roleName +
            L" save has unsaved edits. Opening another file will discard them.\r\n\r\nContinue?";
        return MessageBoxW(hwnd_, message.c_str(), L"Unsaved Edits", MB_YESNO | MB_ICONWARNING) == IDYES;
    }

    bool confirmDiscardAllEdits() const {
        const bool primaryDirty = primary_ && primary_->dirty();
        const bool compareDirty = compare_ && compare_->dirty();
        if (!primaryDirty && !compareDirty) return true;
        std::wstring message = L"There are unsaved edits in ";
        if (primaryDirty && compareDirty) message += L"the primary and comparison saves";
        else if (primaryDirty) message += L"the primary save";
        else message += L"the comparison save";
        message += L".\r\n\r\nClose and discard those edits?";
        return MessageBoxW(hwnd_, message.c_str(), L"Unsaved Edits", MB_YESNO | MB_ICONWARNING) == IDYES;
    }

    void handleDroppedFiles(HDROP drop) {
        if (!drop) return;

        const UINT droppedCount = DragQueryFileW(drop, 0xFFFFFFFFU, nullptr, 0);
        std::vector<std::filesystem::path> paths;
        paths.reserve(std::min<UINT>(droppedCount, 2U));
        for (UINT index = 0; index < droppedCount && index < 2U; ++index) {
            const UINT length = DragQueryFileW(drop, index, nullptr, 0);
            std::wstring path(static_cast<std::size_t>(length) + 1U, L'\0');
            if (DragQueryFileW(drop, index, path.data(), length + 1U) != 0U) {
                path.resize(length);
                paths.emplace_back(path);
            }
        }
        DragFinish(drop);

        if (paths.empty()) return;
        if (paths.size() == 1U) {
            openSave(paths.front(), Role::Primary);
            return;
        }

        // Confirm both replacements before changing either side. This avoids a
        // half-loaded pair when one side contains unsaved edits.
        if (!confirmDiscardRole(Role::Primary) || !confirmDiscardRole(Role::Compare)) return;
        const bool primaryLoaded = openSave(paths[0], Role::Primary, false);
        const bool compareLoaded = primaryLoaded && openSave(paths[1], Role::Compare, false);
        if (droppedCount > 2U) {
            setStatus((compareLoaded ? L"Loaded the first two dropped saves. "
                                     : L"Could not load both dropped saves. ") +
                      numberW(droppedCount - 2U) + L" additional file(s) were ignored.");
        }
    }

    void chooseSave(Role role) {
        const auto path = openFileDialog(hwnd_, role == Role::Primary ? L"Open primary GameData save"
                                                                    : L"Open comparison GameData save");
        if (path) openSave(*path, role);
    }

    void chooseSectionMap() {
        const auto path = openFileDialog(hwnd_, L"Open section cross-reference CSV",
                                         L"CSV files (*.csv)\0*.csv\0All files\0*.*\0\0");
        if (path) loadSectionMap(*path, true);
    }

    void loadSectionMap(const std::filesystem::path& path, bool notify) {
        try {
            const auto count = sectionMap_.load(path);
            rebuildTree();
            setStatus(L"Loaded " + numberW(count) + L" section-map entries from " + path.filename().wstring());
        } catch (const std::exception& error) {
            if (notify) showError(hwnd_, L"Could not load section map:\n\n" + utf8ToWide(error.what()));
            else setStatus(L"Section map could not be loaded: " + utf8ToWide(error.what()));
        }
    }

    void closeSummonModWindow() {
        if (summonModContext_) summonModContext_->suppressCloseNotification = true;
        if (summonModWindow_ && IsWindow(summonModWindow_)) DestroyWindow(summonModWindow_);
        summonModWindow_ = nullptr;
        summonModContext_.reset();
    }

    void closeSummonModWindowForSave(const gdtv::SaveData* save) {
        if (summonModContext_ && summonModContext_->save == save) closeSummonModWindow();
    }

    void closeMasteryModWindow() {
        if (masteryModContext_) masteryModContext_->suppressCloseNotification = true;
        if (masteryModWindow_ && IsWindow(masteryModWindow_)) DestroyWindow(masteryModWindow_);
        masteryModWindow_ = nullptr;
        masteryModContext_.reset();
    }

    void closeMasteryModWindowForSave(const gdtv::SaveData* save) {
        if (masteryModContext_ && masteryModContext_->save == save) closeMasteryModWindow();
    }

    void closeLogicalModWindow() {
        if (logicalModContext_) logicalModContext_->suppressCloseNotification = true;
        if (logicalModWindow_ && IsWindow(logicalModWindow_)) DestroyWindow(logicalModWindow_);
        logicalModWindow_ = nullptr;
        logicalModContext_.reset();
    }

    void closeLogicalModWindowForSave(const gdtv::SaveData* save) {
        if (logicalModContext_ && logicalModContext_->save == save) closeLogicalModWindow();
    }

    bool openSave(const std::filesystem::path& path, Role role, bool confirmDiscard = true) {
        if (confirmDiscard && !confirmDiscardRole(role)) return false;
        setBusy(true, L"Parsing " + path.filename().wstring() + L"...");
        bool loaded = false;
        try {
            auto parsed = std::make_unique<gdtv::SaveData>(path);
            const auto records = parsed->recordCount();
            const auto keys = parsed->keyCount();
            closeSummonModWindowForSave(role == Role::Primary ? primary_.get() : compare_.get());
            closeMasteryModWindowForSave(role == Role::Primary ? primary_.get() : compare_.get());
            closeLogicalModWindowForSave(role == Role::Primary ? primary_.get() : compare_.get());
            if (role == Role::Primary) primary_ = std::move(parsed);
            else compare_ = std::move(parsed);
            rebuildTree();
            setStatus((role == Role::Primary ? L"Primary loaded: " : L"Comparison loaded: ") +
                      path.filename().wstring() + L" \u2014 " + numberW(records) + L" records, " +
                      numberW(keys) + L" keys");
            loaded = true;
        } catch (const std::exception& error) {
            showError(hwnd_, L"Could not parse:\n" + path.wstring() + L"\n\n" + utf8ToWide(error.what()));
        }
        setBusy(false);
        return loaded;
    }

    NodeData* makeNode(NodeData data) {
        nodes_.push_back(std::make_unique<NodeData>(std::move(data)));
        return nodes_.back().get();
    }

    HTREEITEM addItem(HTREEITEM parent, const std::wstring& text, NodeData data,
                      bool hasChildren = false, bool expanded = false, HTREEITEM after = TVI_LAST) {
        auto* stored = makeNode(std::move(data));
        TVINSERTSTRUCTW insert{};
        insert.hParent = parent;
        insert.hInsertAfter = after;
        insert.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN | TVIF_STATE;
        insert.item.pszText = const_cast<wchar_t*>(text.c_str());
        insert.item.lParam = reinterpret_cast<LPARAM>(stored);
        insert.item.cChildren = hasChildren ? 1 : 0;
        if (expanded) {
            insert.item.stateMask = TVIS_EXPANDED;
            insert.item.state = TVIS_EXPANDED;
        }
        return TreeView_InsertItem(tree_, &insert);
    }

    void addDummy(HTREEITEM parent) {
        addItem(parent, L"Loading...", NodeData{NodeKind::Dummy}, false);
    }

    NodeData* nodeData(HTREEITEM item) const {
        if (!item) return nullptr;
        TVITEMW treeItem{};
        treeItem.mask = TVIF_PARAM;
        treeItem.hItem = item;
        if (!TreeView_GetItem(tree_, &treeItem)) return nullptr;
        return reinterpret_cast<NodeData*>(treeItem.lParam);
    }

    bool hasDummy(HTREEITEM item) const {
        const auto child = TreeView_GetChild(tree_, item);
        const auto* data = nodeData(child);
        return child && data && data->kind == NodeKind::Dummy;
    }

    void clearChildren(HTREEITEM item) {
        HTREEITEM child = TreeView_GetChild(tree_, item);
        while (child) {
            const HTREEITEM next = TreeView_GetNextSibling(tree_, child);
            TreeView_DeleteItem(tree_, child);
            child = next;
        }
    }

    void rebuildTree() {
        TreeView_DeleteAllItems(tree_);
        nodes_.clear();
        relationshipCaches_.clear();
        buildComparisonLists();
        if (primary_) insertSaveRoot(*primary_, Role::Primary);
        if (compare_) insertSaveRoot(*compare_, Role::Compare);
        if (logicalOnlyView_) {
            if (!primary_ && !compare_) {
                addItem(TVI_ROOT, L"Open a GameData save to begin", NodeData{NodeKind::Message});
            }
            return;
        }
        if (primary_ && compare_) {
            std::set<std::uint32_t> allKeys;
            for (const auto& [key, group] : primary_->groupsByKey()) { (void)group; allKeys.insert(key); }
            for (const auto& [key, group] : compare_->groupsByKey()) { (void)group; allKeys.insert(key); }
            const auto root = addItem(TVI_ROOT, L"Comparison by Stable Key (" + numberW(allKeys.size()) + L")",
                                      NodeData{NodeKind::ComparisonRoot}, true);
            addDummy(root);
        }
        if (hashDatabase_.uniqueHashCount() > 0U) {
            const auto root = addItem(TVI_ROOT,
                L"Hash Database (" + numberW(hashDatabase_.uniqueHashCount()) + L" unique hashes, " +
                numberW(hashDatabase_.databaseEntryCount()) + L" rows, " +
                numberW(hashDatabase_.friendlyEntryCount()) + L" in-game names)",
                NodeData{NodeKind::HashRoot}, true);
            addDummy(root);
        }
        if (!searchResults_.empty()) insertSearchRoot();
        if (!primary_ && !compare_ && hashDatabase_.uniqueHashCount() == 0U) {
            addItem(TVI_ROOT, L"Open a GameData save to begin", NodeData{NodeKind::Message});
        }
    }

    void insertSaveRoot(gdtv::SaveData& save, Role role) {
        const std::wstring roleName = role == Role::Primary ? L"Primary: " : L"Comparison: ";
        std::wstring rootLabel = roleName + save.path().filename().wstring() + L" (" +
                                 numberW(save.recordCount()) + L" records)";
        if (save.dirty()) rootLabel += L" [EDITED - " + numberW(save.editCount()) + L" changes]";
        const auto root = addItem(TVI_ROOT, rootLabel,
            NodeData{NodeKind::SaveRoot, role, &save}, true, true);

        const auto insertLogicalRecords = [&]() {
            bool hasLogicalRecords = gdtv::familyAvailable(save, gdtv::masteryTreeFamily());
            for (const auto* family : gdtv::sharedLogicalFamilies()) {
                if (family && gdtv::familyAvailable(save, *family)) {
                    hasLogicalRecords = true;
                    break;
                }
            }
            if (!hasLogicalRecords) return;

            NodeData logicalData{NodeKind::LogicalRoot, role, &save};
            const auto logicalRoot = addItem(root, L"Logical Save Records", logicalData, true, true);
            populateLogicalRoot(logicalRoot, logicalData);
            if (gdtv::familyAvailable(save, gdtv::masteryTreeFamily())) {
                const auto* masteryAnchor = save.findGroup(gdtv::masteryTreeFamily().anchorKey);
                std::uint64_t nonZeroStates = 0U;
                if (masteryAnchor) {
                    for (const auto& record : masteryAnchor->records) {
                        if (save.elementOffset(0x0642U, record.index, 0U) &&
                            save.elementBits(0x0642U, record.index, 0U) != 0U) {
                            ++nonZeroStates;
                        }
                    }
                }
                NodeData masteryData{NodeKind::MasteryRoot, role, &save};
                masteryData.key = gdtv::masteryTreeFamily().anchorKey;
                std::wstring masteryLabel = L"Mastery Tree (" +
                    numberW(masteryAnchor ? masteryAnchor->records.size() : 0U) + L" entries, " +
                    numberW(nonZeroStates) + L" non-zero states";
                if (save.findGroup(0x0645U)) masteryLabel += L", legacy companion present";
                masteryLabel += L")";
                const auto masteryRoot = addItem(logicalRoot, masteryLabel, masteryData, true);
                addDummy(masteryRoot);
            }
        };

        if (logicalOnlyView_) {
            insertLogicalRecords();
            return;
        }

        addItem(root, L"Root Table", NodeData{NodeKind::RootInfo, role, &save});
        {
            auto& relationships = relationshipCache(save);
            NodeData relationshipData{NodeKind::RelationshipRoot, role, &save};
            const auto relationshipRoot = addItem(
                root,
                L"Save Relationship Map (" + numberW(relationships.physical.size()) +
                    L" physical sections, " + numberW(relationships.families.size()) + L" linked families)",
                relationshipData, true, true);
            populateRelationshipRoot(relationshipRoot, relationshipData);
        }
        insertLogicalRecords();
        if (sectionNames_.size() > 0U) insertSubsystemSections(root, save, role);
        const auto vectorsRoot = addItem(root, L"Root Vectors", NodeData{NodeKind::VectorsRoot, role, &save},
                                         true, true);
        for (const auto& vector : save.vectors()) {
            const auto typeName = utf8ToWide(std::string(gdtv::valueTypeName(gdtv::valueTypeForVector(vector.number))));
            const auto text = vector.present
                ? typeName + L" Table (Vector " + std::to_wstring(vector.number) + L") \u2014 " + numberW(vector.count) +
                  L" save units / " + numberW(vector.keys.size()) + L" IDTypes"
                : typeName + L" Table (Vector " + std::to_wstring(vector.number) + L") \u2014 absent";
            const auto item = addItem(vectorsRoot, text,
                NodeData{NodeKind::Vector, role, &save, vector.number}, vector.present && !vector.keys.empty());
            if (vector.present && !vector.keys.empty()) addDummy(item);
        }
        const auto linked = addItem(root,
            L"Linked Structures (" + numberW(save.linkedClusters().size()) + L")",
            NodeData{NodeKind::LinkedRoot, role, &save}, true);
        addDummy(linked);
    }

    void buildComparisonLists() {
        for (auto& list : compareKeys_) list.clear();
        if (!primary_ || !compare_) return;
        std::set<std::uint32_t> keys;
        for (const auto& [key, group] : primary_->groupsByKey()) { (void)group; keys.insert(key); }
        for (const auto& [key, group] : compare_->groupsByKey()) { (void)group; keys.insert(key); }
        for (const auto key : keys) {
            const auto info = gdtv::compareKey(primary_.get(), compare_.get(), key);
            CompareCategory category = CompareCategory::Changed;
            if (info.status == "Primary-only key") category = CompareCategory::PrimaryOnly;
            else if (info.status == "Comparison-only key") category = CompareCategory::CompareOnly;
            else if (info.status == "Unchanged") category = CompareCategory::Unchanged;
            compareKeys_[static_cast<std::size_t>(category)].push_back(key);
        }
    }

    void populateIfNeeded(HTREEITEM item) {
        if (!hasDummy(item)) return;
        const auto* data = nodeData(item);
        if (!data) return;
        clearChildren(item);
        switch (data->kind) {
        case NodeKind::Vector: populateVector(item, *data); break;
        case NodeKind::Key: populateKey(item, *data); break;
        case NodeKind::Page: populatePage(item, *data); break;
        case NodeKind::LinkedRoot: populateLinkedRoot(item, *data); break;
        case NodeKind::LinkedCluster: populateLinkedCluster(item, *data); break;
        case NodeKind::ComparisonRoot: populateComparisonRoot(item); break;
        case NodeKind::ComparisonCategory: populateComparisonCategory(item, *data); break;
        case NodeKind::ComparisonKey: populateComparisonKey(item, *data); break;
        case NodeKind::HashRoot: populateHashRoot(item); break;
        case NodeKind::HashFriendlyRoot: populateHashFriendlyRoot(item); break;
        case NodeKind::HashUserRoot: populateHashUserRoot(item); break;
        case NodeKind::HashEntry: populateHashEntry(item, *data); break;
        case NodeKind::LogicalRoot: populateLogicalRoot(item, *data); break;
        case NodeKind::RelationshipRoot: populateRelationshipRoot(item, *data); break;
        case NodeKind::RelationshipPhysicalRoot: populateRelationshipPhysicalRoot(item, *data); break;
        case NodeKind::RelationshipPhysicalPage: populateRelationshipPhysicalPage(item, *data); break;
        case NodeKind::RelationshipPhysicalSection: populateRelationshipPhysicalSection(item, *data); break;
        case NodeKind::RelationshipFamiliesRoot: populateRelationshipFamiliesRoot(item, *data); break;
        case NodeKind::RelationshipFamily: populateRelationshipFamily(item, *data); break;
        case NodeKind::RelationshipReferencesRoot: populateRelationshipReferencesRoot(item, *data); break;
        case NodeKind::RelationshipReferenceSection: populateRelationshipReferenceSection(item, *data); break;
        case NodeKind::LogicalFamily: populateLogicalFamily(item, *data); break;
        case NodeKind::LogicalCharacter: populateLogicalCharacter(item, *data); break;
        case NodeKind::LogicalPage: populateLogicalPage(item, *data); break;
        case NodeKind::LogicalSlot: populateLogicalSlot(item, *data); break;
        case NodeKind::CurioEntriesRoot: populateCurioEntriesRoot(item, *data); break;
        case NodeKind::CurioSlot: populateCurioSlot(item, *data); break;
        case NodeKind::MasteryRoot: populateMasteryRoot(item, *data); break;
        case NodeKind::MasteryCharacter: populateMasteryCharacter(item, *data); break;
        case NodeKind::MasteryPage: populateMasteryPage(item, *data); break;
        case NodeKind::MasteryEntry: populateMasteryEntry(item, *data); break;
        default: break;
        }
    }

    std::wstring sectionDisplayName(std::uint32_t key, bool includeConfidence = true) const {
        if (const auto* named = sectionNames_.find(key); named && !named->name.empty()) {
            std::wstring result = utf8ToWide(named->name);
            if (includeConfidence) result += utf8ToWide(gdtv::confidenceMarker(named->confidence));
            return result;
        }
        if (const auto* section = sectionMap_.find(key); section && !section->name.empty()) {
            return utf8ToWide(section->name);
        }
        return {};
    }

    std::wstring sectionLocator(std::uint32_t key) const {
        if (const auto* named = sectionNames_.find(key); named && !named->locator.empty()) {
            return utf8ToWide(named->locator);
        }
        if (const auto* section = sectionMap_.find(key); section && !section->locator.empty()) {
            return utf8ToWide(section->locator);
        }
        return legacyLocatorW(key);
    }

    RelationshipCache& relationshipCache(gdtv::SaveData& save) {
        auto [iterator, inserted] = relationshipCaches_.try_emplace(&save);
        if (inserted) {
            iterator->second.physical = gdtv::buildPhysicalSectionOrder(save);
            iterator->second.families = gdtv::buildRelationshipFamilies(save);
        }
        return iterator->second;
    }

    std::vector<std::uint32_t> relationshipPeers(const RelationshipCache& cache,
                                                 std::uint32_t key) const {
        std::set<std::uint32_t> unique;
        for (const auto& family : cache.families) {
            if (std::find(family.keys.begin(), family.keys.end(), key) == family.keys.end()) continue;
            for (const auto member : family.keys) {
                if (member != key) unique.insert(member);
            }
        }
        return {unique.begin(), unique.end()};
    }

    void buildReferenceSummaries(gdtv::SaveData& save, RelationshipCache& cache) {
        if (cache.referencesBuilt) return;
        cache.references = gdtv::buildReferenceSections(save, hashDatabase_);
        cache.referencesBuilt = true;
    }

    std::wstring groupLabel(const gdtv::KeyGroup& group) const {
        const auto displayName = sectionDisplayName(group.key);
        const auto prefix = displayName.empty() ? std::wstring{} : displayName + L" \u2014 ";
        return prefix + sectionLocator(group.key) + L" \u2014 " +
               utf8ToWide(std::string(gdtv::valueTypeName(group.valueType()))) + L" \u2014 V" +
               std::to_wstring(group.vectorNumber) + L":" + utf8ToWide(group.keyHex()) +
               L" \u2014 " + numberW(group.records.size()) + L" records \u2014 UnitIDs " +
               utf8ToWide(group.indexRanges(5));
    }

    void insertSubsystemSections(HTREEITEM parent, gdtv::SaveData& save, Role role) {
        std::map<std::wstring, std::vector<std::uint32_t>> bySubsystem;
        for (const auto& [key, group] : save.groupsByKey()) {
            (void)group;
            std::wstring subsystem = L"Uncategorized";
            if (const auto* named = sectionNames_.find(key); named && !named->subsystem.empty()) {
                subsystem = utf8ToWide(named->subsystem);
            }
            bySubsystem[subsystem].push_back(key);
        }
        const auto root = addItem(parent,
            L"Sections by Subsystem (" + numberW(bySubsystem.size()) + L" groups)",
            NodeData{NodeKind::Message, role, &save}, !bySubsystem.empty());
        for (const auto& [subsystem, keys] : bySubsystem) {
            const auto subsystemItem = addItem(root,
                subsystem + L" (" + numberW(keys.size()) + L" sections)",
                NodeData{NodeKind::Message, role, &save}, !keys.empty());
            for (const auto key : keys) {
                const auto* group = save.findGroup(key);
                if (!group) continue;
                const auto keyItem = addItem(subsystemItem, groupLabel(*group),
                    NodeData{NodeKind::Key, role, &save, group->vectorNumber, key},
                    !group->records.empty());
                if (!group->records.empty()) addDummy(keyItem);
            }
        }
    }

    std::wstring characterSectionLabel(std::uint32_t group) const {
        const auto fallback = L"Character Group " + numberW(group);
        const auto* entry = characterSections_.find(group);
        if (!entry) return fallback;
        std::wstring name;
        if (!entry->inGameName.empty()) name = utf8ToWide(entry->inGameName);
        if (!entry->internalName.empty()) {
            if (!name.empty()) name += L" (" + utf8ToWide(entry->internalName) + L")";
            else name = utf8ToWide(entry->internalName);
        }
        return name.empty() ? fallback : name + L" - " + fallback;
    }

    std::wstring characterSectionDetails(std::uint32_t group) const {
        std::wstring details = characterSectionLabel(group);
        if (const auto* entry = characterSections_.find(group); entry && !entry->note.empty()) {
            details += L"\r\nNote: " + utf8ToWide(entry->note);
        }
        return details;
    }


    std::wstring hashDescription(std::uint32_t value) const {
        std::wstring description = hexW(value) + L" / raw " + rawLittleEndianW(value, 4U);
        if (const auto* preferred = hashDatabase_.preferred(value)) {
            std::wstring name;
            if (!preferred->displayName.empty()) name = utf8ToWide(preferred->displayName);
            const auto ids = hashDatabase_.idsForHash(value);
            if (!ids.empty()) {
                if (!name.empty()) name += L" - ";
                for (std::size_t index = 0; index < ids.size(); ++index) {
                    if (index) name += L" | ";
                    name += utf8ToWide(ids[index]);
                }
            } else if (!preferred->id.empty()) {
                if (!name.empty()) name += L" - ";
                name += utf8ToWide(preferred->id);
            }
            if (!preferred->category.empty()) {
                if (!name.empty()) name += L" ";
                name += L"[" + utf8ToWide(preferred->category) + L"]";
            }
            if (!name.empty()) description += L" => " + name;
        }
        return description;
    }

    std::wstring formatElementValue(const gdtv::SaveData& save, std::uint32_t key,
                                    std::uint32_t unitId, std::uint32_t elementIndex,
                                    gdtv::LogicalValueKind logicalKind) const {
        const auto* group = save.findGroup(key);
        if (!group) return L"<section missing>";
        const auto offset = save.elementOffset(key, unitId, elementIndex);
        if (!offset) return L"<element missing>";
        const auto bits = save.elementBits(key, unitId, elementIndex);
        const auto size = group->elementSize();

        if (logicalKind == gdtv::LogicalValueKind::Hash && size == 4U) {
            return hashDescription(static_cast<std::uint32_t>(bits));
        }
        if (logicalKind == gdtv::LogicalValueKind::State) {
            std::wstring state;
            if (bits == 0U) state = L"Empty";
            else if (bits == 2U) state = L"Active";
            else if (bits == 3U) state = L"Active + additional flag";
            else state = L"Unknown state";
            return std::to_wstring(bits) + L" / " + hexW(bits, static_cast<int>(size * 2U)) +
                   L" (" + state + L")";
        }

        if (logicalKind == gdtv::LogicalValueKind::Bitfield) {
            const auto value = static_cast<std::uint32_t>(bits);
            unsigned count = 0U;
            auto remaining = value;
            while (remaining != 0U) {
                count += remaining & 1U;
                remaining >>= 1U;
            }
            return std::to_wstring(bitCopy<std::int32_t>(value)) + L" / " + hexW(value, 8) +
                   L" (" + numberW(count) + L" bit(s) set)";
        }

        switch (group->valueType()) {
        case gdtv::ValueType::Bool:
            return (bits == 0U ? L"false" : L"true") +
                   std::wstring(L" / ") + hexW(bits, 2);
        case gdtv::ValueType::Byte:
            return std::to_wstring(static_cast<std::int8_t>(bits)) + L" / " + hexW(bits, 2);
        case gdtv::ValueType::UByte:
            return std::to_wstring(bits) + L" / " + hexW(bits, 2);
        case gdtv::ValueType::Short:
            return std::to_wstring(static_cast<std::int16_t>(bits)) + L" / " + hexW(bits, 4);
        case gdtv::ValueType::UShort:
            return std::to_wstring(bits) + L" / " + hexW(bits, 4);
        case gdtv::ValueType::Int:
            return std::to_wstring(bitCopy<std::int32_t>(static_cast<std::uint32_t>(bits))) +
                   L" / " + hexW(bits, 8);
        case gdtv::ValueType::UInt:
            return std::to_wstring(bits) + L" / " + hexW(bits, 8) +
                   L" / raw " + rawLittleEndianW(bits, 4U);
        case gdtv::ValueType::Long:
            return std::to_wstring(bitCopy<std::int64_t>(bits)) + L" / " + hexW(bits, 16);
        case gdtv::ValueType::ULong:
            return std::to_wstring(bits) + L" / " + hexW(bits, 16);
        case gdtv::ValueType::Float: {
            const auto value = bitCopy<float>(static_cast<std::uint32_t>(bits));
            std::wostringstream out;
            out << std::setprecision(9) << value << L" / bits " << hexW(bits, 8);
            return out.str();
        }
        }
        return L"<unsupported>";
    }

    std::wstring logicalFieldLabel(const gdtv::SaveData& save,
                                   const gdtv::LogicalFieldDefinition& field,
                                   std::uint32_t unitId) const {
        (void)save;
        (void)unitId;
        return utf8ToWide(std::string(field.locator)) + L" - " +
               utf8ToWide(std::string(field.label));
    }

    std::wstring logicalHashName(const gdtv::SaveData& save,
                                 const gdtv::LogicalFieldDefinition& field,
                                 std::uint32_t unitId) const {
        return utf8ToWide(gdtv::logicalHashFieldDisplayName(
            save, hashDatabase_, field, unitId));
    }

    std::wstring logicalLevelText(const gdtv::SaveData& save,
                                  const gdtv::LogicalFieldDefinition& field,
                                  std::uint32_t unitId) const {
        if (!gdtv::logicalFieldAvailable(save, field, unitId)) return {};
        const auto bits = static_cast<std::uint32_t>(
            save.elementBits(field.key, unitId, field.elementIndex));
        return std::to_wstring(bitCopy<std::int32_t>(bits));
    }

    std::wstring summonSlotLabel(const gdtv::SaveData& save, std::uint32_t unitId) const {
        auto label = L"Summon Inv - Slot " + numberW(unitId);
        const auto& family = gdtv::summonInventoryFamily();
        if (family.fieldCount > 1U) {
            const auto name = logicalHashName(save, family.fields[1], unitId);
            if (!name.empty()) label += L" - " + name;
        }
        return label;
    }

    void populateLogicalRoot(HTREEITEM item, const NodeData& data) {
        if (!data.save) return;
        for (const auto* family : gdtv::sharedLogicalFamilies()) {
            if (!family || !gdtv::familyAvailable(*data.save, *family)) continue;
            const auto* anchor = data.save->findGroup(family->anchorKey);
            if (!anchor) continue;

            std::wstring label = utf8ToWide(std::string(family->name)) + L" (";
            if (family->grouping == gdtv::LogicalGroupingKind::CurioSlotEntries) {
                std::set<std::uint32_t> slots;
                for (const auto& record : anchor->records) {
                    const auto address = gdtv::decodeLogicalUnitId(*family, record.index);
                    if (address.valid) slots.insert(address.slot);
                }
                label += numberW(slots.size()) + L" slots, " +
                         numberW(anchor->records.size()) + L" entries";
            } else {
                label += numberW(anchor->records.size()) + L" entries";
            }
            if (family->anchorKey == gdtv::summonInventoryFamily().anchorKey) {
                std::uint64_t active = 0U;
                for (const auto& record : anchor->records) {
                    if (data.save->elementOffset(0x05B4U, record.index, 0U) &&
                        data.save->elementBits(0x05B4U, record.index, 0U) != 0U) {
                        ++active;
                    }
                }
                label += L", " + numberW(active) + L" active";
            }
            label += L")";

            NodeData familyData{NodeKind::LogicalFamily, data.role, data.save};
            familyData.key = family->anchorKey;
            familyData.logicalFamilyAnchor = family->anchorKey;
            const auto familyItem = addItem(item, label, familyData, true);
            addDummy(familyItem);
        }
    }

    void populateLogicalFamily(HTREEITEM item, const NodeData& data) {
        if (!data.save) return;
        const auto anchorKey = data.logicalFamilyAnchor != 0U ? data.logicalFamilyAnchor : data.key;
        const auto* family = gdtv::logicalFamilyForAnchor(anchorKey);
        if (!family) return;
        const auto* anchor = data.save->findGroup(family->anchorKey);
        if (!anchor) return;

        if (family->grouping == gdtv::LogicalGroupingKind::CurrentTraitsCharacter ||
            family->grouping == gdtv::LogicalGroupingKind::OverMasteryCharacter) {
            std::map<std::uint32_t, std::size_t> characterCounts;
            for (const auto& record : anchor->records) {
                const auto address = gdtv::decodeLogicalUnitId(*family, record.index);
                if (address.valid && address.characterScoped) ++characterCounts[address.characterGroup];
            }
            for (const auto& [characterGroup, count] : characterCounts) {
                NodeData characterData{NodeKind::LogicalCharacter, data.role, data.save};
                characterData.key = family->anchorKey;
                characterData.logicalFamilyAnchor = family->anchorKey;
                characterData.characterGroup = characterGroup;
                const auto label = characterSectionLabel(characterGroup) + L" (" +
                                   numberW(count) + L" entries)";
                const auto characterItem = addItem(item, label, characterData, true);
                addDummy(characterItem);
            }
            return;
        }

        if (family->grouping == gdtv::LogicalGroupingKind::CurioSlotEntries) {
            std::set<std::uint32_t> slots;
            for (const auto& record : anchor->records) {
                const auto address = gdtv::decodeLogicalUnitId(*family, record.index);
                if (address.valid) slots.insert(address.slot);
            }
            NodeData entriesData{NodeKind::CurioEntriesRoot, data.role, data.save};
            entriesData.key = family->anchorKey;
            entriesData.logicalFamilyAnchor = family->anchorKey;
            const auto entries = addItem(item,
                L"Entries (" + numberW(slots.size()) + L" slots × 5 reward entries)",
                entriesData, !slots.empty());
            if (!slots.empty()) addDummy(entries);
            return;
        }

        constexpr std::size_t logicalPageSize = 100U;
        for (std::size_t startOrdinal = 0; startOrdinal < anchor->records.size();
             startOrdinal += logicalPageSize) {
            const auto endOrdinal = std::min(anchor->records.size(), startOrdinal + logicalPageSize);
            NodeData pageData{NodeKind::LogicalPage, data.role, data.save};
            pageData.key = family->anchorKey;
            pageData.logicalFamilyAnchor = family->anchorKey;
            pageData.start = startOrdinal;
            pageData.end = endOrdinal;
            const auto firstEntry = anchor->records[startOrdinal].index;
            const auto lastEntry = anchor->records[endOrdinal - 1U].index;
            const auto prefix = family->anchorKey == gdtv::summonInventoryFamily().anchorKey
                ? L"Slots " : L"Entries ";
            const auto page = addItem(item,
                std::wstring(prefix) + numberW(firstEntry) + L"-" + numberW(lastEntry),
                pageData, true);
            addDummy(page);
        }
    }

    void populateLogicalCharacter(HTREEITEM item, const NodeData& data) {
        if (!data.save) return;
        const auto* family = gdtv::logicalFamilyForAnchor(data.logicalFamilyAnchor);
        if (!family) return;
        const auto* anchor = data.save->findGroup(family->anchorKey);
        if (!anchor) return;

        if (family->grouping == gdtv::LogicalGroupingKind::OverMasteryCharacter) {
            for (std::size_t ordinal = 0; ordinal < anchor->records.size(); ++ordinal) {
                const auto unitId = anchor->records[ordinal].index;
                const auto address = gdtv::decodeLogicalUnitId(*family, unitId);
                if (!address.valid || address.characterGroup != data.characterGroup) continue;
                NodeData slotData{NodeKind::LogicalSlot, data.role, data.save};
                slotData.key = family->anchorKey;
                slotData.logicalFamilyAnchor = family->anchorKey;
                slotData.recordOrdinal = ordinal;
                slotData.unitId = unitId;
                slotData.characterGroup = data.characterGroup;
                auto label = L"Slot " + numberW(address.slot);
                if (family->fieldCount >= 2U) {
                    const auto name = logicalHashName(*data.save, family->fields[0], unitId);
                    if (!name.empty()) {
                        label += L" - " + name;
                        const auto level = logicalLevelText(*data.save, family->fields[1], unitId);
                        if (!level.empty()) label += L" \\ Level " + level;
                    }
                }
                const auto slot = addItem(item, label, slotData, true);
                addDummy(slot);
            }
            return;
        }

        if (family->grouping != gdtv::LogicalGroupingKind::CurrentTraitsCharacter) return;
        struct PageBucket {
            std::size_t firstOrdinal{};
            std::size_t lastOrdinal{};
            std::uint32_t firstPosition{};
            std::uint32_t lastPosition{};
            std::size_t count{};
            bool initialized{};
        };
        std::map<std::pair<std::uint32_t, std::uint32_t>, PageBucket> pages;
        for (std::size_t ordinal = 0; ordinal < anchor->records.size(); ++ordinal) {
            const auto address = gdtv::decodeLogicalUnitId(*family, anchor->records[ordinal].index);
            if (!address.valid || address.characterGroup != data.characterGroup) continue;
            const auto pageIndex = address.position / 25U;
            auto& page = pages[{address.nameSpace, pageIndex}];
            if (!page.initialized) {
                page.firstOrdinal = ordinal;
                page.firstPosition = address.position;
                page.initialized = true;
            }
            page.lastOrdinal = ordinal + 1U;
            page.lastPosition = address.position;
            ++page.count;
        }
        for (const auto& [key, page] : pages) {
            NodeData pageData{NodeKind::LogicalPage, data.role, data.save};
            pageData.key = family->anchorKey;
            pageData.logicalFamilyAnchor = family->anchorKey;
            pageData.characterGroup = data.characterGroup;
            pageData.logicalNamespace = key.first;
            pageData.start = page.firstOrdinal;
            pageData.end = page.lastOrdinal;
            const auto label = L"Namespace " + numberW(key.first) + L" - Positions " +
                               numberW(page.firstPosition) + L"-" + numberW(page.lastPosition) +
                               L" (" + numberW(page.count) + L" slots)";
            const auto pageItem = addItem(item, label, pageData, true);
            addDummy(pageItem);
        }
    }

    void populateLogicalPage(HTREEITEM item, const NodeData& data) {
        if (!data.save) return;
        const auto anchorKey = data.logicalFamilyAnchor != 0U ? data.logicalFamilyAnchor : data.key;
        const auto* family = gdtv::logicalFamilyForAnchor(anchorKey);
        if (!family) return;
        const auto* anchor = data.save->findGroup(family->anchorKey);
        if (!anchor) return;
        const auto endOrdinal = std::min(data.end, anchor->records.size());
        for (std::size_t ordinal = data.start; ordinal < endOrdinal; ++ordinal) {
            const auto unitId = anchor->records[ordinal].index;
            const auto address = gdtv::decodeLogicalUnitId(*family, unitId);
            if (family->grouping == gdtv::LogicalGroupingKind::CurrentTraitsCharacter &&
                (!address.valid || address.characterGroup != data.characterGroup ||
                 address.nameSpace != data.logicalNamespace)) {
                continue;
            }

            NodeData slotData{NodeKind::LogicalSlot, data.role, data.save};
            slotData.key = family->anchorKey;
            slotData.logicalFamilyAnchor = family->anchorKey;
            slotData.recordOrdinal = ordinal;
            slotData.unitId = unitId;
            slotData.characterGroup = data.characterGroup;
            slotData.logicalNamespace = data.logicalNamespace;

            std::wstring label;
            if (family->grouping == gdtv::LogicalGroupingKind::CurrentTraitsCharacter && address.valid) {
                label = L"Position " + numberW(address.position) + L" - Slot " + numberW(address.slot);
                if (family->fieldCount >= 2U) {
                    const auto name = logicalHashName(*data.save, family->fields[0], unitId);
                    if (!name.empty()) {
                        label += L" - " + name;
                        const auto level = logicalLevelText(*data.save, family->fields[1], unitId);
                        if (!level.empty()) label += L" \\ Level " + level;
                    }
                }
            } else if (family->anchorKey == gdtv::summonInventoryFamily().anchorKey) {
                label = summonSlotLabel(*data.save, unitId);
            } else {
                label = utf8ToWide(std::string(family->slotLabel)) + L" " + numberW(unitId);
                if (family->fieldCount > 0U &&
                    family->fields[0].kind == gdtv::LogicalValueKind::Hash) {
                    const auto name = logicalHashName(*data.save, family->fields[0], unitId);
                    if (!name.empty()) label += L" - " + name;
                }
            }
            const auto slot = addItem(item, label, slotData, true);
            addDummy(slot);
        }
    }

    static std::wstring curioEntryTypeLabel(std::uint32_t entryIndex) {
        switch (entryIndex) {
        case 0U: return L"Items";
        case 1U: return L"Sigils";
        case 2U: return L"Unknown 1";
        case 3U: return L"Wrightstones";
        case 4U: return L"Unknown 3";
        default: return L"Unknown";
        }
    }

    void populateCurioEntriesRoot(HTREEITEM item, const NodeData& data) {
        if (!data.save) return;
        const auto* family = gdtv::logicalFamilyForAnchor(data.logicalFamilyAnchor);
        if (!family || family->grouping != gdtv::LogicalGroupingKind::CurioSlotEntries) return;
        const auto* anchor = data.save->findGroup(family->anchorKey);
        if (!anchor) return;

        std::map<std::uint32_t, std::vector<std::pair<std::uint32_t, std::uint32_t>>> slotEntries;
        for (const auto& record : anchor->records) {
            const auto address = gdtv::decodeLogicalUnitId(*family, record.index);
            if (!address.valid) continue;
            slotEntries[address.slot].emplace_back(address.position, record.index);
        }
        for (auto& [slotIndex, entries] : slotEntries) {
            std::sort(entries.begin(), entries.end());
            NodeData slotData{NodeKind::CurioSlot, data.role, data.save};
            slotData.key = family->anchorKey;
            slotData.logicalFamilyAnchor = family->anchorKey;
            slotData.logicalNamespace = slotIndex;
            auto label = L"Slot " + numberW(slotIndex + 1U);
            const auto rewardNames =
                gdtv::curioSlotRewardDisplayNames(*data.save, hashDatabase_, slotIndex);
            for (std::size_t index = 0U; index < rewardNames.size(); ++index) {
                label += index == 0U ? L" - " : L" / ";
                label += utf8ToWide(rewardNames[index]);
            }
            const auto slot = addItem(item, label, slotData, !entries.empty());
            if (!entries.empty()) addDummy(slot);
        }
    }

    void populateCurioSlot(HTREEITEM item, const NodeData& data) {
        if (!data.save) return;
        const auto* family = gdtv::logicalFamilyForAnchor(data.logicalFamilyAnchor);
        if (!family || family->grouping != gdtv::LogicalGroupingKind::CurioSlotEntries) return;
        const auto* anchor = data.save->findGroup(family->anchorKey);
        if (!anchor) return;

        std::vector<std::pair<std::uint32_t, std::size_t>> entries;
        for (std::size_t ordinal = 0U; ordinal < anchor->records.size(); ++ordinal) {
            const auto unitId = anchor->records[ordinal].index;
            const auto address = gdtv::decodeLogicalUnitId(*family, unitId);
            if (!address.valid || address.slot != data.logicalNamespace) continue;
            entries.emplace_back(address.position, ordinal);
        }
        std::sort(entries.begin(), entries.end());
        for (const auto& [entryIndex, ordinal] : entries) {
            const auto unitId = anchor->records[ordinal].index;
            NodeData entryData{NodeKind::LogicalSlot, data.role, data.save};
            entryData.key = family->anchorKey;
            entryData.logicalFamilyAnchor = family->anchorKey;
            entryData.recordOrdinal = ordinal;
            entryData.unitId = unitId;
            entryData.logicalNamespace = data.logicalNamespace;
            const auto entry = addItem(item,
                L"Curio Reward Entry " + numberW(entryIndex) + L" - " +
                    curioEntryTypeLabel(entryIndex),
                entryData, true);
            addDummy(entry);
        }
    }

    void populateLogicalSlot(HTREEITEM item, const NodeData& data) {
        if (!data.save) return;
        const auto anchorKey = data.logicalFamilyAnchor != 0U ? data.logicalFamilyAnchor : data.key;
        const auto* family = gdtv::logicalFamilyForAnchor(anchorKey);
        if (!family) return;
        for (std::size_t fieldIndex = 0; fieldIndex < family->fieldCount; ++fieldIndex) {
            const auto& field = family->fields[fieldIndex];
            std::size_t ordinal = 0;
            const auto* record = data.save->findRecord(field.key, data.unitId, &ordinal);
            if (!record || field.elementIndex >= record->elementCount) {
                if (!field.optional) {
                    addItem(item,
                        utf8ToWide(std::string(field.locator)) + L" - " +
                        utf8ToWide(std::string(field.label)) + L" - <missing>",
                        NodeData{NodeKind::Message});
                }
                continue;
            }
            const auto* group = data.save->findGroup(field.key);
            if (!group) continue;
            NodeData fieldData{NodeKind::LogicalField, data.role, data.save};
            fieldData.vectorNumber = group->vectorNumber;
            fieldData.key = field.key;
            fieldData.start = field.elementIndex;
            fieldData.recordOrdinal = ordinal;
            fieldData.unitId = data.unitId;
            fieldData.logicalFieldIndex = static_cast<std::uint32_t>(fieldIndex);
            fieldData.logicalFamilyAnchor = family->anchorKey;
            addItem(item, logicalFieldLabel(*data.save, field, data.unitId), fieldData);
        }
    }

    void populateMasteryRoot(HTREEITEM item, const NodeData& data) {
        if (!data.save) return;
        const auto& family = gdtv::masteryTreeFamily();
        const auto* anchor = data.save->findGroup(family.anchorKey);
        if (!anchor) return;

        std::size_t sharedCount = 0U;
        std::map<std::uint32_t, std::size_t> characterCounts;
        for (const auto& record : anchor->records) {
            const auto address = gdtv::decodeLogicalUnitId(family, record.index);
            if (!address.valid) continue;
            if (address.shared) ++sharedCount;
            else if (address.characterScoped) ++characterCounts[address.characterGroup];
        }
        if (sharedCount != 0U) {
            NodeData sharedData{NodeKind::MasteryCharacter, data.role, data.save};
            sharedData.key = family.anchorKey;
            sharedData.sharedGroup = true;
            const auto sharedItem = addItem(item, L"Shared / Global (" + numberW(sharedCount) +
                                             L" slots)", sharedData, true);
            addDummy(sharedItem);
        }
        for (const auto& [characterGroup, count] : characterCounts) {
            NodeData characterData{NodeKind::MasteryCharacter, data.role, data.save};
            characterData.key = family.anchorKey;
            characterData.characterGroup = characterGroup;
            const auto label = characterSectionLabel(characterGroup) + L" (" +
                               numberW(count) + L" slots)";
            const auto characterItem = addItem(item, label, characterData, true);
            addDummy(characterItem);
        }
    }

    void populateMasteryCharacter(HTREEITEM item, const NodeData& data) {
        if (!data.save) return;
        const auto& family = gdtv::masteryTreeFamily();
        const auto* anchor = data.save->findGroup(family.anchorKey);
        if (!anchor) return;
        struct PageBucket {
            std::size_t firstOrdinal{};
            std::size_t lastOrdinal{};
            std::uint32_t firstSlot{};
            std::uint32_t lastSlot{};
            bool initialized{};
        };
        std::map<std::uint32_t, PageBucket> pages;
        for (std::size_t ordinal = 0; ordinal < anchor->records.size(); ++ordinal) {
            const auto address = gdtv::decodeLogicalUnitId(family, anchor->records[ordinal].index);
            if (!address.valid || address.shared != data.sharedGroup) continue;
            if (!data.sharedGroup && address.characterGroup != data.characterGroup) continue;
            const auto pageIndex = address.slot / 100U;
            auto& page = pages[pageIndex];
            if (!page.initialized) {
                page.firstOrdinal = ordinal;
                page.firstSlot = address.slot;
                page.initialized = true;
            }
            page.lastOrdinal = ordinal + 1U;
            page.lastSlot = address.slot;
        }
        for (const auto& [pageIndex, page] : pages) {
            (void)pageIndex;
            NodeData pageData{NodeKind::MasteryPage, data.role, data.save};
            pageData.key = family.anchorKey;
            pageData.start = page.firstOrdinal;
            pageData.end = page.lastOrdinal;
            pageData.characterGroup = data.characterGroup;
            pageData.sharedGroup = data.sharedGroup;
            const auto pageItem = addItem(item, L"Slots " + numberW(page.firstSlot) + L"-" +
                                           numberW(page.lastSlot), pageData, true);
            addDummy(pageItem);
        }
    }

    void populateMasteryPage(HTREEITEM item, const NodeData& data) {
        if (!data.save) return;
        const auto& family = gdtv::masteryTreeFamily();
        const auto* anchor = data.save->findGroup(family.anchorKey);
        if (!anchor) return;
        const auto endOrdinal = std::min(data.end, anchor->records.size());
        for (std::size_t ordinal = data.start; ordinal < endOrdinal; ++ordinal) {
            const auto unitId = anchor->records[ordinal].index;
            const auto address = gdtv::decodeLogicalUnitId(family, unitId);
            if (!address.valid || address.shared != data.sharedGroup) continue;
            if (!data.sharedGroup && address.characterGroup != data.characterGroup) continue;
            NodeData entryData{NodeKind::MasteryEntry, data.role, data.save};
            entryData.key = family.anchorKey;
            entryData.recordOrdinal = ordinal;
            entryData.unitId = unitId;
            entryData.characterGroup = data.characterGroup;
            entryData.sharedGroup = data.sharedGroup;
            auto label = L"Slot " + numberW(address.slot);
            if (family.fieldCount > 0U) {
                const auto name = logicalHashName(*data.save, family.fields[0], unitId);
                if (!name.empty()) label += L" - " + name;
            }
            const auto entry = addItem(item, label, entryData, true);
            addDummy(entry);
        }
    }

    void populateMasteryEntry(HTREEITEM item, const NodeData& data) {
        if (!data.save) return;
        const auto& family = gdtv::masteryTreeFamily();
        for (std::size_t fieldIndex = 0; fieldIndex < family.fieldCount; ++fieldIndex) {
            const auto& field = family.fields[fieldIndex];
            std::size_t ordinal = 0;
            const auto* record = data.save->findRecord(field.key, data.unitId, &ordinal);
            if (!record || field.elementIndex >= record->elementCount) {
                if (!field.optional) {
                    addItem(item,
                        legacyLocatorW(field.key) + L" - " +
                        utf8ToWide(std::string(field.label)) + L" - <missing>",
                        NodeData{NodeKind::Message});
                }
                continue;
            }
            NodeData fieldData{NodeKind::MasteryField, data.role, data.save};
            fieldData.vectorNumber = data.save->findGroup(field.key)->vectorNumber;
            fieldData.key = field.key;
            fieldData.start = field.elementIndex;
            fieldData.recordOrdinal = ordinal;
            fieldData.unitId = data.unitId;
            fieldData.logicalFieldIndex = static_cast<std::uint32_t>(fieldIndex);
            addItem(item,
                legacyLocatorW(field.key) + L" - " + utf8ToWide(std::string(field.label)),
                fieldData);
        }
    }

    void populateVector(HTREEITEM item, const NodeData& data) {
        if (!data.save || data.vectorNumber == 0 || data.vectorNumber > 10) return;
        const auto& vector = data.save->vectors()[data.vectorNumber - 1U];
        for (const auto key : vector.keys) {
            const auto* group = data.save->findGroup(key);
            if (!group) continue;
            const auto keyItem = addItem(item, groupLabel(*group),
                NodeData{NodeKind::Key, data.role, data.save, group->vectorNumber, key}, !group->records.empty());
            if (!group->records.empty()) addDummy(keyItem);
        }
    }

    void populateKey(HTREEITEM item, const NodeData& data) {
        const auto* group = data.save ? data.save->findGroup(data.key) : nullptr;
        if (!group) return;
        if (group->records.size() <= kPageSize) {
            addRecordItems(item, data, 0, group->records.size());
            return;
        }
        for (std::size_t start = 0; start < group->records.size(); start += kPageSize) {
            const auto end = std::min(group->records.size(), start + kPageSize);
            const auto firstIndex = group->records[start].index;
            const auto lastIndex = group->records[end - 1U].index;
            const auto page = addItem(item,
                L"Records " + numberW(start + 1U) + L"-" + numberW(end) + L" \u2014 indices " +
                numberW(firstIndex) + L"-" + numberW(lastIndex),
                NodeData{NodeKind::Page, data.role, data.save, group->vectorNumber, data.key, start, end}, true);
            addDummy(page);
        }
    }

    void populatePage(HTREEITEM item, const NodeData& data) {
        addRecordItems(item, data, data.start, data.end);
    }

    void addRecordItems(HTREEITEM parent, const NodeData& data, std::size_t start, std::size_t end) {
        const auto* group = data.save ? data.save->findGroup(data.key) : nullptr;
        if (!group) return;
        end = std::min(end, group->records.size());
        for (std::size_t ordinal = start; ordinal < end; ++ordinal) {
            const auto& record = group->records[ordinal];
            const auto text = L"UnitID " + numberW(record.index) + L" \u2014 " +
                              numberW(record.elementCount) + L" " +
                              utf8ToWide(std::string(gdtv::valueTypeName(group->valueType()))) + L" values / " +
                              numberW(record.payloadByteLength) + L" bytes \u2014 record " + hexW(record.recordOffset);
            addItem(parent, text,
                    NodeData{NodeKind::Record, data.role, data.save, group->vectorNumber, data.key,
                             0, 0, ordinal});
        }
    }

    void populateHashRoot(HTREEITEM item) {
        addItem(item,
                L"Unified text database - " + numberW(hashDatabase_.databaseEntryCount()) + L" rows; " +
                numberW(hashDatabase_.baseEntryCount()) + L" internal-name rows (" +
                numberW(hashDatabase_.verifiedBaseCount()) + L" algorithm-verified)",
                NodeData{NodeKind::Message});
        const auto friendly = addItem(item,
            L"In-Game Names (" + numberW(hashDatabase_.friendlyEntryCount()) + L")",
            NodeData{NodeKind::HashFriendlyRoot}, hashDatabase_.friendlyEntryCount() > 0U);
        if (hashDatabase_.friendlyEntryCount() > 0U) addDummy(friendly);
        const auto user = addItem(item,
            L"Session Additions (" + numberW(hashDatabase_.userEntryCount()) + L")",
            NodeData{NodeKind::HashUserRoot}, hashDatabase_.userEntryCount() > 0U);
        if (hashDatabase_.userEntryCount() > 0U) addDummy(user);
    }

    static void sortHashEntries(std::vector<gdtv::HashEntry>& entries) {
        std::sort(entries.begin(), entries.end(), [](const gdtv::HashEntry& left, const gdtv::HashEntry& right) {
            if (left.category != right.category) return left.category < right.category;
            if (left.displayName != right.displayName) return left.displayName < right.displayName;
            if (left.id != right.id) return left.id < right.id;
            return left.hash < right.hash;
        });
    }

    void addHashEntriesToTree(HTREEITEM item, std::vector<gdtv::HashEntry> entries) {
        sortHashEntries(entries);
        for (const auto& entry : entries) {
            std::wstring label;
            if (!entry.category.empty()) label += L"[" + utf8ToWide(entry.category) + L"] ";
            if (!entry.displayName.empty()) label += utf8ToWide(entry.displayName) + L" - ";
            const auto ids = hashDatabase_.idsForHash(entry.hash);
            if (!ids.empty()) {
                for (std::size_t i = 0; i < ids.size(); ++i) {
                    if (i) label += L" | ";
                    label += utf8ToWide(ids[i]);
                }
                label += L" - ";
            } else if (!entry.id.empty()) {
                label += utf8ToWide(entry.id) + L" - ";
            }
            label += L"0x" + utf8ToWide(gdtv::hashHex(entry.hash)) + L" / raw " +
                     utf8ToWide(gdtv::hashRawLittleEndian(entry.hash));
            NodeData data{NodeKind::HashEntry};
            data.hashValue = entry.hash;
            const bool canSearchOccurrences = primary_ || compare_;
            const auto hashItem = addItem(item, label, data, canSearchOccurrences);
            if (canSearchOccurrences) addDummy(hashItem);
        }
    }

    void populateHashFriendlyRoot(HTREEITEM item) {
        addHashEntriesToTree(item, hashDatabase_.friendlyEntries());
    }

    void populateHashUserRoot(HTREEITEM item) {
        addHashEntriesToTree(item, hashDatabase_.userEntries());
    }

    void populateHashEntry(HTREEITEM item, const NodeData& data) {
        const auto addOccurrences = [&](gdtv::SaveData* save, Role role, const wchar_t* roleName) {
            if (!save) return;
            const auto occurrences = save->findUIntValue(data.hashValue, 5000);
            for (const auto& occurrence : occurrences) {
                const auto* group = save->findGroup(occurrence.key);
                if (!group || occurrence.recordOrdinal >= group->records.size()) continue;
                const auto& record = group->records[occurrence.recordOrdinal];
                NodeData occurrenceData{NodeKind::HashOccurrence, role, save, group->vectorNumber,
                                        occurrence.key, occurrence.elementIndex, 0, occurrence.recordOrdinal};
                occurrenceData.hashValue = data.hashValue;
                addItem(item,
                        std::wstring(roleName) + L" \u2014 " + groupLabel(*group) + L" \u2014 UnitID " +
                        numberW(record.index) + L" \u2014 element " + numberW(occurrence.elementIndex),
                        occurrenceData);
            }
        };
        addOccurrences(primary_.get(), Role::Primary, L"Primary");
        addOccurrences(compare_.get(), Role::Compare, L"Comparison");
    }

    void populateRelationshipRoot(HTREEITEM item, const NodeData& data) {
        if (!data.save) return;
        auto& cache = relationshipCache(*data.save);

        NodeData physicalData{NodeKind::RelationshipPhysicalRoot, data.role, data.save};
        const auto physical = addItem(item,
            L"Physical Order (" + numberW(cache.physical.size()) + L" sections, first to last)",
            physicalData, !cache.physical.empty());
        if (!cache.physical.empty()) addDummy(physical);

        NodeData familiesData{NodeKind::RelationshipFamiliesRoot, data.role, data.save};
        const auto families = addItem(item,
            L"Logical Families (" + numberW(cache.families.size()) + L")",
            familiesData, !cache.families.empty());
        if (!cache.families.empty()) addDummy(families);

        NodeData referencesData{NodeKind::RelationshipReferencesRoot, data.role, data.save};
        const auto references = addItem(item,
            L"Hash Reference Links (scan when expanded)", referencesData, true);
        addDummy(references);
    }

    void populateRelationshipPhysicalRoot(HTREEITEM item, const NodeData& data) {
        if (!data.save) return;
        const auto& physical = relationshipCache(*data.save).physical;
        constexpr std::size_t pageSize = 100U;
        for (std::size_t start = 0U; start < physical.size(); start += pageSize) {
            const auto end = std::min(start + pageSize, physical.size());
            NodeData pageData{NodeKind::RelationshipPhysicalPage, data.role, data.save};
            pageData.start = start;
            pageData.end = end;
            const auto page = addItem(item,
                L"Sections " + numberW(start + 1U) + L"-" + numberW(end) +
                    L" \u2014 offsets " + hexW(physical[start].firstRecordOffset) + L"-" +
                    hexW(physical[end - 1U].lastRecordOffset),
                pageData, true);
            addDummy(page);
        }
    }

    void populateRelationshipPhysicalPage(HTREEITEM item, const NodeData& data) {
        if (!data.save) return;
        const auto& physical = relationshipCache(*data.save).physical;
        const auto end = std::min(data.end, physical.size());
        for (std::size_t index = data.start; index < end; ++index) {
            const auto& section = physical[index];
            const auto* group = data.save->findGroup(section.key);
            if (!group) continue;
            NodeData sectionData{NodeKind::RelationshipPhysicalSection, data.role, data.save,
                                 group->vectorNumber, section.key};
            sectionData.relationshipIndex = index;
            const auto peers = relationshipPeers(relationshipCache(*data.save), section.key);
            const bool hasLinks = index > 0U || index + 1U < physical.size() || !peers.empty();
            const auto child = addItem(item,
                L"#" + numberW(index + 1U) + L" \u2014 " + hexW(section.firstRecordOffset) +
                    L" \u2014 " + groupLabel(*group),
                sectionData, hasLinks);
            if (hasLinks) addDummy(child);
        }
    }

    void populateRelationshipPhysicalSection(HTREEITEM item, const NodeData& data) {
        if (!data.save) return;
        const auto& physical = relationshipCache(*data.save).physical;
        if (data.relationshipIndex >= physical.size()) return;
        const auto& current = physical[data.relationshipIndex];

        auto addRelation = [&](std::wstring prefix, std::uint32_t targetKey, std::size_t relationKind) {
            const auto* target = data.save->findGroup(targetKey);
            if (!target) return;
            NodeData memberData{NodeKind::RelationshipMember, data.role, data.save,
                                target->vectorNumber, targetKey};
            memberData.logicalFamilyAnchor = current.key;
            memberData.relationshipSubIndex = relationKind;
            addItem(item, std::move(prefix) + groupLabel(*target), memberData);
        };

        if (data.relationshipIndex > 0U) {
            addRelation(L"Previous \u2014 ", physical[data.relationshipIndex - 1U].key, 0U);
        }
        if (data.relationshipIndex + 1U < physical.size()) {
            addRelation(L"Next \u2014 ", physical[data.relationshipIndex + 1U].key, 1U);
        }
        for (const auto peer : relationshipPeers(relationshipCache(*data.save), current.key)) {
            addRelation(L"Same UnitIDs \u2014 ", peer, 2U);
        }
    }

    void populateRelationshipFamiliesRoot(HTREEITEM item, const NodeData& data) {
        if (!data.save) return;
        const auto& families = relationshipCache(*data.save).families;
        for (std::size_t index = 0U; index < families.size(); ++index) {
            const auto& family = families[index];
            NodeData familyData{NodeKind::RelationshipFamily, data.role, data.save};
            familyData.relationshipIndex = index;
            const auto child = addItem(item,
                utf8ToWide(family.name) + L" [" + utf8ToWide(family.confidence) + L"] \u2014 " +
                    numberW(family.keys.size()) + L" sections \u2014 " +
                    numberW(family.recordCount) + L" shared UnitIDs",
                familyData, !family.keys.empty());
            if (!family.keys.empty()) addDummy(child);
        }
    }

    void populateRelationshipFamily(HTREEITEM item, const NodeData& data) {
        if (!data.save) return;
        const auto& families = relationshipCache(*data.save).families;
        if (data.relationshipIndex >= families.size()) return;
        const auto& family = families[data.relationshipIndex];
        for (const auto key : family.keys) {
            const auto* group = data.save->findGroup(key);
            if (!group) continue;
            NodeData memberData{NodeKind::RelationshipMember, data.role, data.save,
                                group->vectorNumber, key};
            memberData.relationshipIndex = data.relationshipIndex;
            memberData.relationshipSubIndex = 3U;
            addItem(item, groupLabel(*group), memberData);
        }
    }

    void populateRelationshipReferencesRoot(HTREEITEM item, const NodeData& data) {
        if (!data.save) return;
        auto& cache = relationshipCache(*data.save);
        buildReferenceSummaries(*data.save, cache);
        if (cache.references.empty()) {
            addItem(item, L"No database-resolved UInt references were found", NodeData{NodeKind::Message});
            return;
        }
        for (std::size_t index = 0U; index < cache.references.size(); ++index) {
            const auto& reference = cache.references[index];
            const auto* group = data.save->findGroup(reference.key);
            if (!group) continue;
            NodeData referenceData{NodeKind::RelationshipReferenceSection, data.role, data.save,
                                   group->vectorNumber, reference.key};
            referenceData.relationshipIndex = index;
            const auto child = addItem(item,
                groupLabel(*group) + L" \u2014 " + numberW(reference.resolvedValues) +
                    L" resolved references \u2014 " + numberW(reference.categories.size()) + L" categories",
                referenceData, !reference.categories.empty());
            if (!reference.categories.empty()) addDummy(child);
        }
    }

    void populateRelationshipReferenceSection(HTREEITEM item, const NodeData& data) {
        if (!data.save) return;
        auto& cache = relationshipCache(*data.save);
        buildReferenceSummaries(*data.save, cache);
        if (data.relationshipIndex >= cache.references.size()) return;
        const auto& reference = cache.references[data.relationshipIndex];
        for (std::size_t index = 0U; index < reference.categories.size(); ++index) {
            const auto& category = reference.categories[index];
            NodeData categoryData{NodeKind::RelationshipReferenceCategory, data.role, data.save,
                                  0U, reference.key};
            categoryData.relationshipIndex = data.relationshipIndex;
            categoryData.relationshipSubIndex = index;
            addItem(item,
                utf8ToWide(category.category) + L" \u2014 " + numberW(category.occurrences) +
                    L" references \u2014 " + numberW(category.exampleHashes.size()) + L" example hashes",
                categoryData);
        }
    }

    void populateLinkedRoot(HTREEITEM item, const NodeData& data) {
        if (!data.save) return;
        const auto& clusters = data.save->linkedClusters();
        for (std::size_t i = 0; i < clusters.size(); ++i) {
            const auto& cluster = clusters[i];
            if (cluster.empty()) continue;
            const auto* first = data.save->findGroup(cluster.front());
            const auto* last = data.save->findGroup(cluster.back());
            if (!first || !last) continue;
            std::wstring names;
            for (const auto key : cluster) {
                const auto sectionName = sectionDisplayName(key);
                if (!sectionName.empty()) {
                    if (!names.empty()) names += L" / ";
                    names += sectionName;
                    if (names.size() > 90) { names += L"..."; break; }
                }
            }
            const auto prefix = names.empty() ? std::wstring{} : names + L" \u2014 ";
            const auto clusterItem = addItem(item,
                prefix + L"Group " + numberW(i + 1U) + L": " + utf8ToWide(first->keyHex()) + L"-" +
                utf8ToWide(last->keyHex()) + L" \u2014 " + numberW(cluster.size()) + L" keys \u00D7 " +
                numberW(first->records.size()) + L" records",
                NodeData{NodeKind::LinkedCluster, data.role, data.save, 0, 0, 0, 0, 0, i}, true);
            addDummy(clusterItem);
        }
    }

    void populateLinkedCluster(HTREEITEM item, const NodeData& data) {
        if (!data.save) return;
        const auto& clusters = data.save->linkedClusters();
        if (data.clusterIndex >= clusters.size()) return;
        for (const auto key : clusters[data.clusterIndex]) {
            const auto* group = data.save->findGroup(key);
            if (!group) continue;
            const auto child = addItem(item, groupLabel(*group),
                NodeData{NodeKind::Key, data.role, data.save, group->vectorNumber, key}, true);
            addDummy(child);
        }
    }

    void populateComparisonRoot(HTREEITEM item) {
        static constexpr std::array<const wchar_t*, 4> labels{
            L"Changed / Moved", L"Primary-only Keys", L"Comparison-only Keys", L"Unchanged Keys"
        };
        for (std::size_t index = 0; index < labels.size(); ++index) {
            const auto category = static_cast<CompareCategory>(index);
            const auto child = addItem(item,
                std::wstring(labels[index]) + L" (" + numberW(compareKeys_[index].size()) + L")",
                NodeData{NodeKind::ComparisonCategory, Role::Primary, nullptr, 0, 0, 0, 0, 0, 0, category},
                !compareKeys_[index].empty());
            if (!compareKeys_[index].empty()) addDummy(child);
        }
    }

    void populateComparisonCategory(HTREEITEM item, const NodeData& data) {
        const auto& keys = compareKeys_[static_cast<std::size_t>(data.category)];
        for (const auto key : keys) {
            const auto info = gdtv::compareKey(primary_.get(), compare_.get(), key);
            const auto* group = info.primary ? info.primary : info.compare;
            if (!group) continue;
            const auto displayName = sectionDisplayName(key);
            const auto name = displayName.empty() ? std::wstring{} : displayName + L" \u2014 ";
            const auto child = addItem(item,
                name + utf8ToWide(group->keyHex()) + L" \u2014 " + utf8ToWide(info.status),
                NodeData{NodeKind::ComparisonKey, Role::Primary, nullptr, group->vectorNumber, key}, true);
            addDummy(child);
        }
    }

    void populateComparisonKey(HTREEITEM item, const NodeData& data) {
        if (primary_) {
            if (const auto* group = primary_->findGroup(data.key)) {
                const auto child = addItem(item, L"Primary \u2014 " + groupLabel(*group),
                    NodeData{NodeKind::Key, Role::Primary, primary_.get(), group->vectorNumber, data.key}, true);
                addDummy(child);
            }
        }
        if (compare_) {
            if (const auto* group = compare_->findGroup(data.key)) {
                const auto child = addItem(item, L"Comparison \u2014 " + groupLabel(*group),
                    NodeData{NodeKind::Key, Role::Compare, compare_.get(), group->vectorNumber, data.key}, true);
                addDummy(child);
            }
        }
    }

    void insertSearchRoot() {
        const auto root = addItem(TVI_ROOT, L"Search Results (" + numberW(searchResults_.size()) + L")",
                                  NodeData{NodeKind::SearchRoot}, true, true, TVI_FIRST);
        const auto limit = std::min<std::size_t>(searchResults_.size(), 2000);
        for (std::size_t i = 0; i < limit; ++i) {
            const auto [role, key] = searchResults_[i];
            auto* save = role == Role::Primary ? primary_.get() : compare_.get();
            const auto* group = save ? save->findGroup(key) : nullptr;
            if (!group) continue;
            auto name = sectionDisplayName(key);
            if (name.empty()) name = L"(not named)";
            addItem(root,
                (role == Role::Primary ? L"Primary \u2014 " : L"Comparison \u2014 ") + name + L" \u2014 V" +
                std::to_wstring(group->vectorNumber) + L":" + utf8ToWide(group->keyHex()) + L" \u2014 " +
                numberW(group->records.size()) + L" records",
                NodeData{NodeKind::SearchKey, role, save, group->vectorNumber, key});
        }
    }

    void runSearch() {
        const auto queryWide = getWindowTextString(searchEdit_);
        const auto query = gdtv::toLowerAscii(wideToUtf8(queryWide));
        if (query.empty()) return;
        searchResults_.clear();
        const auto numericKey = gdtv::parseKeyQuery(query);
        auto scan = [&](Role role, gdtv::SaveData* save) {
            if (!save) return;
            for (const auto& [key, group] : save->groupsByKey()) {
                bool match = numericKey && *numericKey == key;
                if (!match) {
                    std::string haystack = group.keyHex() + " " + group.stableLocator();
                    if (const auto* section = sectionMap_.find(key)) {
                        haystack += " " + section->locator + " " + section->stableLocator + " " +
                                    section->name + " " + section->stride + " " + section->countAudit;
                    }
                    if (const auto* named = sectionNames_.find(key)) {
                        haystack += " " + named->locator + " " + named->name + " " + named->subsystem +
                                    " " + named->confidence + " " + named->officialSaveIdType +
                                    " " + named->storageType + " " + named->hashCategory +
                                    " " + named->internalPrefix + " " + named->note +
                                    " " + named->recommendedTest +
                                    " subsystem:" + named->subsystem +
                                    " confidence:" + named->confidence +
                                    " storage:" + named->storageType +
                                    " enum:" + named->officialSaveIdType +
                                    " hashcategory:" + named->hashCategory +
                                    " prefix:" + named->internalPrefix;
                    }
                    match = gdtv::toLowerAscii(std::move(haystack)).find(query) != std::string::npos;
                }
                if (match) searchResults_.emplace_back(role, key);
            }
        };
        scan(Role::Primary, primary_.get());
        scan(Role::Compare, compare_.get());
        rebuildTree();
        setStatus(L"Search found " + numberW(searchResults_.size()) + L" matching key entries");
    }

    void clearSearch() {
        searchResults_.clear();
        SetWindowTextW(searchEdit_, L"");
        rebuildTree();
    }


    struct EditableSelection {
        Role role{Role::Primary};
        gdtv::SaveData* save{};
        const gdtv::KeyGroup* group{};
        const gdtv::Record* record{};
        std::uint32_t elementIndex{};
        std::wstring label;
    };

    std::optional<EditableSelection> selectedEditableElement() const {
        const auto* data = nodeData(TreeView_GetSelection(tree_));
        if (!data || !data->save) return std::nullopt;
        const auto* group = data->save->findGroup(data->key);
        if (!group || data->recordOrdinal >= group->records.size()) return std::nullopt;
        const auto* record = &group->records[data->recordOrdinal];

        std::uint32_t elementIndex = 0;
        std::wstring label;
        if (data->kind == NodeKind::LogicalField) {
            elementIndex = static_cast<std::uint32_t>(data->start);
            const auto* family = gdtv::logicalFamilyForAnchor(data->logicalFamilyAnchor);
            if (family && data->logicalFieldIndex < family->fieldCount) {
                const auto& field = family->fields[data->logicalFieldIndex];
                label = utf8ToWide(std::string(field.locator)) + L" - " +
                        utf8ToWide(std::string(field.label));
            }
        } else if (data->kind == NodeKind::MasteryField) {
            elementIndex = static_cast<std::uint32_t>(data->start);
            const auto& family = gdtv::masteryTreeFamily();
            if (data->logicalFieldIndex < family.fieldCount) {
                const auto& field = family.fields[data->logicalFieldIndex];
                label = legacyLocatorW(field.key) + L" - " +
                        utf8ToWide(std::string(field.label));
            }
        } else if (data->kind == NodeKind::HashOccurrence) {
            elementIndex = static_cast<std::uint32_t>(data->start);
            label = L"Hash element " + numberW(elementIndex);
        } else if (data->kind == NodeKind::Record && record->elementCount == 1U) {
            label = L"V" + std::to_wstring(group->vectorNumber) + L":" +
                    utf8ToWide(group->keyHex()) + L" UnitID " + numberW(record->index);
        } else {
            return std::nullopt;
        }
        if (elementIndex >= record->elementCount) return std::nullopt;
        return EditableSelection{data->role, data->save, group, record, elementIndex, std::move(label)};
    }

    static std::optional<std::uint64_t> parseRawLittleEndianInput(std::wstring text,
                                                                  std::size_t size) {
        text = trimWide(std::move(text));
        std::wstring compact;
        compact.reserve(text.size());
        for (const auto ch : text) {
            if (ch != L' ' && ch != L'_' && ch != L'-') compact.push_back(ch);
        }
        if (compact.size() != size * 2U) return std::nullopt;
        std::uint64_t bits = 0;
        for (std::size_t index = 0; index < size; ++index) {
            const auto pair = compact.substr(index * 2U, 2U);
            try {
                std::size_t used = 0;
                const auto byte = std::stoul(pair, &used, 16);
                if (used != 2U || byte > 0xFFU) return std::nullopt;
                bits |= static_cast<std::uint64_t>(byte) << (index * 8U);
            } catch (...) {
                return std::nullopt;
            }
        }
        return bits;
    }

    static std::optional<std::uint64_t> parseUnsignedInput(std::wstring text,
                                                            std::uint64_t maximum) {
        text = trimWide(std::move(text));
        if (text.empty()) return std::nullopt;
        int base = 10;
        if (text.size() > 2U && text[0] == L'0' && (text[1] == L'x' || text[1] == L'X')) base = 16;
        try {
            std::size_t used = 0;
            const auto value = std::stoull(text, &used, base);
            if (used != text.size() || value > maximum) return std::nullopt;
            return value;
        } catch (...) {
            return std::nullopt;
        }
    }

    static std::optional<std::uint64_t> parseEditedBits(std::wstring input,
                                                         gdtv::ValueType type,
                                                         std::wstring& error) {
        input = trimWide(std::move(input));
        const auto size = gdtv::valueTypeSize(type);
        const auto lowerAscii = [](wchar_t ch) noexcept {
            return ch >= L'A' && ch <= L'Z' ? static_cast<wchar_t>(ch - L'A' + L'a') : ch;
        };
        const auto lowerPrefix = [&](std::wstring_view prefix) {
            if (input.size() < prefix.size()) return false;
            for (std::size_t index = 0; index < prefix.size(); ++index) {
                if (lowerAscii(input[index]) != lowerAscii(prefix[index])) return false;
            }
            return true;
        };

        if (lowerPrefix(L"raw:") || lowerPrefix(L"le:")) {
            const auto colon = input.find(L':');
            const auto raw = parseRawLittleEndianInput(input.substr(colon + 1U), size);
            if (!raw) error = L"Raw input must contain exactly " + numberW(size * 2U) + L" hexadecimal digits.";
            return raw;
        }
        if (type == gdtv::ValueType::Bool) {
            std::wstring lower = input;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](wchar_t ch) {
                return ch >= L'A' && ch <= L'Z' ? static_cast<wchar_t>(ch - L'A' + L'a') : ch;
            });
            if (lower == L"true" || lower == L"1") return 1U;
            if (lower == L"false" || lower == L"0") return 0U;
            error = L"Bool values must be true, false, 1, or 0.";
            return std::nullopt;
        }
        if (type == gdtv::ValueType::Float) {
            if (lowerPrefix(L"bits:")) {
                const auto value = parseUnsignedInput(input.substr(5U), 0xFFFFFFFFULL);
                if (!value) error = L"Float bits must be a valid 32-bit number, for example bits:0x3F800000.";
                return value;
            }
            try {
                std::size_t used = 0;
                const auto value = std::stof(input, &used);
                if (used != input.size() || !std::isfinite(value)) {
                    error = L"Enter a finite floating-point value.";
                    return std::nullopt;
                }
                return static_cast<std::uint64_t>(bitCopy<std::uint32_t>(value));
            } catch (...) {
                error = L"Enter a valid floating-point value or bits:0xXXXXXXXX.";
                return std::nullopt;
            }
        }

        const bool signedType = type == gdtv::ValueType::Byte || type == gdtv::ValueType::Short ||
                                type == gdtv::ValueType::Int || type == gdtv::ValueType::Long;
        if (signedType && !(input.size() > 2U && input[0] == L'0' &&
                            (input[1] == L'x' || input[1] == L'X'))) {
            try {
                std::size_t used = 0;
                const auto value = std::stoll(input, &used, 10);
                if (used != input.size()) throw std::invalid_argument("trailing characters");
                const auto bits = static_cast<unsigned>(size * 8U);
                const auto minimum = size == 8U ? std::numeric_limits<std::int64_t>::min()
                                                : -(std::int64_t{1} << (bits - 1U));
                const auto maximum = size == 8U ? std::numeric_limits<std::int64_t>::max()
                                                : (std::int64_t{1} << (bits - 1U)) - 1;
                if (value < minimum || value > maximum) {
                    error = L"The signed value is outside the selected type's range.";
                    return std::nullopt;
                }
                if (size == 8U) return bitCopy<std::uint64_t>(value);
                const auto mask = (std::uint64_t{1} << bits) - 1U;
                return static_cast<std::uint64_t>(value) & mask;
            } catch (...) {
                error = L"Enter a signed decimal number, 0x canonical bits, or raw: little-endian bytes.";
                return std::nullopt;
            }
        }

        const auto maximum = size == 8U ? std::numeric_limits<std::uint64_t>::max()
                                        : (std::uint64_t{1} << (size * 8U)) - 1U;
        const auto value = parseUnsignedInput(input, maximum);
        if (!value) error = L"Enter decimal, 0x canonical hexadecimal, or raw: little-endian bytes.";
        return value;
    }

    static std::wstring editSeed(std::uint64_t bits, gdtv::ValueType type) {
        switch (type) {
        case gdtv::ValueType::Bool:
            return bits == 0U ? L"false" : L"true";
        case gdtv::ValueType::Byte:
            return std::to_wstring(static_cast<std::int8_t>(bits));
        case gdtv::ValueType::UByte:
            return std::to_wstring(bits);
        case gdtv::ValueType::Short:
            return std::to_wstring(static_cast<std::int16_t>(bits));
        case gdtv::ValueType::UShort:
            return std::to_wstring(bits);
        case gdtv::ValueType::Int:
            return std::to_wstring(bitCopy<std::int32_t>(static_cast<std::uint32_t>(bits)));
        case gdtv::ValueType::UInt:
            return hexW(bits, 8);
        case gdtv::ValueType::Long:
            return std::to_wstring(bitCopy<std::int64_t>(bits));
        case gdtv::ValueType::ULong:
            return hexW(bits, 16);
        case gdtv::ValueType::Float: {
            std::wostringstream out;
            out << std::setprecision(9) << bitCopy<float>(static_cast<std::uint32_t>(bits));
            return out.str();
        }
        }
        return {};
    }

    void editSelectedValue() {
        if (const auto* data = nodeData(TreeView_GetSelection(tree_)); data) {
            if (data->kind == NodeKind::LogicalSlot) {
                openLogicalSlotModWindow(*data);
                return;
            }
            if (data->kind == NodeKind::MasteryEntry) {
                openMasteryTreeModWindow(*data);
                return;
            }
        }
        const auto selected = selectedEditableElement();
        if (!selected) {
            MessageBoxW(hwnd_,
                        L"Select a logical field, a hash occurrence, or a raw record containing exactly one value.",
                        L"Edit Current Value", MB_OK | MB_ICONINFORMATION);
            return;
        }
        const auto type = selected->group->valueType();
        const auto bits = selected->save->elementBits(selected->group->key, selected->record->index,
                                                       selected->elementIndex);
        const auto offset = selected->save->elementOffset(selected->group->key, selected->record->index,
                                                           selected->elementIndex);
        const auto size = selected->group->elementSize();
        const auto current = editSeed(bits, type);
        std::wstring instructions = L"Accepted: decimal or 0x canonical hexadecimal. Use raw:" +
            rawLittleEndianW(bits, size) + L" to enter bytes exactly as seen in the save.";
        if (type == gdtv::ValueType::UInt) {
            instructions += L" You may also enter an exact hash database internal ID or in-game name.";
            if (const auto* named = sectionNames_.find(selected->group->key); named &&
                (!named->hashCategory.empty() || !named->internalPrefix.empty())) {
                instructions += L" Expected section filter:";
                if (!named->hashCategory.empty()) instructions += L" TYPE=" + utf8ToWide(named->hashCategory);
                if (!named->internalPrefix.empty()) instructions += L" PREFIX=" + utf8ToWide(named->internalPrefix);
                instructions += L".";
            }
        }
        if (type == gdtv::ValueType::Float) {
            instructions = L"Accepted: decimal float, bits:0xXXXXXXXX, or raw:XXXXXXXX.";
        } else if (type == gdtv::ValueType::Bool) {
            instructions = L"Accepted: true, false, 1, 0, or raw:00/raw:01.";
        }
        const auto heading = selected->label + L"\r\nUnitID " + numberW(selected->record->index) +
                             L" - element " + numberW(selected->elementIndex) +
                             (offset ? L" - offset " + hexW(*offset) : L"");
        const auto text = showValueEditDialog(hwnd_, heading, current, instructions);
        if (!text) return;

        std::wstring error;
        auto newBits = parseEditedBits(*text, type, error);
        if (!newBits && type == gdtv::ValueType::UInt) {
            auto matches = hashDatabase_.hashesForText(wideToUtf8(trimWide(*text)));
            if (const auto* named = sectionNames_.find(selected->group->key); named &&
                (!named->hashCategory.empty() || !named->internalPrefix.empty())) {
                matches.erase(std::remove_if(matches.begin(), matches.end(), [&](std::uint32_t hash) {
                    return !hashDatabase_.hasMatchingEntry(hash, named->hashCategory, named->internalPrefix);
                }), matches.end());
            }
            if (matches.size() == 1U) {
                newBits = matches.front();
                error.clear();
            } else if (matches.size() > 1U) {
                error = L"The name matches multiple hashes. Enter the canonical 0x hash or a unique internal ID.";
            } else {
                error += L" The text did not match a unique hash database name for this section.";
            }
        }
        if (!newBits) {
            showError(hwnd_, L"The value was not changed.\n\n" + error);
            return;
        }
        if (type == gdtv::ValueType::UInt) {
            if (const auto* named = sectionNames_.find(selected->group->key); named &&
                (!named->hashCategory.empty() || !named->internalPrefix.empty())) {
                const auto hashValue = static_cast<std::uint32_t>(*newBits);
                if (hashValue != 0U && !gdtv::isGlobalEmptySlotHash(hashValue) &&
                    hashValue != std::numeric_limits<std::uint32_t>::max() &&
                    hashDatabase_.find(hashValue) &&
                    !hashDatabase_.hasMatchingEntry(hashValue, named->hashCategory, named->internalPrefix)) {
                    std::wstring warning = L"This hash is known, but it does not match the expected ID filter for this section.\n\n";
                    if (!named->hashCategory.empty()) warning += L"Expected category: " + utf8ToWide(named->hashCategory) + L"\n";
                    if (!named->internalPrefix.empty()) warning += L"Expected internal prefix: " + utf8ToWide(named->internalPrefix) + L"\n";
                    warning += L"\nWrite it anyway?";
                    if (MessageBoxW(hwnd_, warning.c_str(), L"Section ID Warning",
                                    MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
                        return;
                    }
                }
            }
        }
        try {
            selected->save->setElementBits(selected->group->key, selected->record->index,
                                           selected->elementIndex, *newBits);
            const auto roleName = selected->role == Role::Primary ? L"Primary" : L"Comparison";
            setStatus(std::wstring(roleName) + L" edited in memory - " + selected->label +
                      L" - use File > Save Edited " + roleName + L" As");
            rebuildTree();
        } catch (const std::exception& exception) {
            showError(hwnd_, L"Could not edit the selected value:\n\n" + utf8ToWide(exception.what()));
        }
    }

    void saveEditedSave(Role role) {
        auto* save = role == Role::Primary ? primary_.get() : compare_.get();
        if (!save) {
            showError(hwnd_, role == Role::Primary ? L"No primary save is open." : L"No comparison save is open.");
            return;
        }
        auto initial = save->path().filename().wstring();
        initial += L".edited";
        const auto output = saveFileDialog(hwnd_,
            role == Role::Primary ? L"Save edited primary GameData" : L"Save edited comparison GameData",
            initial, L"GameData saves\0*.*\0All files\0*.*\0\0", nullptr);
        if (!output) return;
        try {
            const auto integrity = save->saveAs(*output, true);
            rebuildTree();
            std::wstring message = L"Saved edited file: " + output->wstring();
            if (integrity.supported) {
                message += L"\r\n\r\nGBFR integrity hash repaired at index " +
                           numberW(integrity.activeIndex) + L".\r\nNew hash: " +
                           hexW(integrity.newHash, 16);
            } else {
                message += L"\r\n\r\nNo supported GBFR integrity block was found; file was written without hash repair.";
            }
            MessageBoxW(hwnd_, message.c_str(), L"Save Complete", MB_OK | MB_ICONINFORMATION);
            setStatus(L"Saved " + output->filename().wstring());
        } catch (const std::exception& exception) {
            showError(hwnd_, L"Could not save the edited file:\n\n" + utf8ToWide(exception.what()));
        }
    }

    void handleTreeDoubleClick() {
        const auto selected = TreeView_GetSelection(tree_);
        const auto* data = nodeData(selected);
        if (!data) return;
        if (data->kind == NodeKind::LogicalSlot) {
            openLogicalSlotModWindow(*data);
        } else if (data->kind == NodeKind::MasteryEntry) {
            openMasteryTreeModWindow(*data);
        } else if (data->kind == NodeKind::LogicalField || data->kind == NodeKind::MasteryField) {
            editSelectedValue();
        } else if (data->kind == NodeKind::SearchKey || data->kind == NodeKind::HashOccurrence) {
            focusKey(data->role, data->key);
        }
    }

    void handleTreeClick() {
        const DWORD position = GetMessagePos();
        POINT point{static_cast<SHORT>(LOWORD(position)), static_cast<SHORT>(HIWORD(position))};
        ScreenToClient(tree_, &point);
        TVHITTESTINFO hit{};
        hit.pt = point;
        const auto item = TreeView_HitTest(tree_, &hit);
        if (!item || !(hit.flags & (TVHT_ONITEMICON | TVHT_ONITEMLABEL | TVHT_ONITEMSTATEICON))) return;
        const auto* data = nodeData(item);
        if (!data || (data->kind != NodeKind::LogicalSlot && data->kind != NodeKind::MasteryEntry)) return;
        TreeView_SelectItem(tree_, item);
        if (data->kind == NodeKind::LogicalSlot) openLogicalSlotModWindow(*data);
        else openMasteryTreeModWindow(*data);
    }

    void openLogicalSlotModWindow(const NodeData& data) {
        if (!data.save || data.kind != NodeKind::LogicalSlot) return;
        const auto anchorKey = data.logicalFamilyAnchor != 0U ? data.logicalFamilyAnchor : data.key;
        const auto* family = gdtv::logicalFamilyForAnchor(anchorKey);
        if (!family) return;
        if (family->anchorKey == gdtv::summonInventoryFamily().anchorKey) {
            openSummonSlotModWindow(data);
        } else {
            openSharedLogicalFamilyModWindow(data, *family);
        }
    }

    void openSharedLogicalFamilyModWindow(const NodeData& data,
                                          const gdtv::LogicalFamilyDefinition& family) {
        if (!data.save || data.kind != NodeKind::LogicalSlot) return;

        if (logicalModContext_ && logicalModWindow_ && IsWindow(logicalModWindow_) &&
            logicalModContext_->family == &family) {
            selectLogicalFamilyEntryInModWindow(*logicalModContext_, *data.save, data.unitId);
        } else {
            closeLogicalModWindow();
            logicalModContext_ = std::make_unique<LogicalFamilyModDialogContext>();
            logicalModContext_->save = data.save;
            logicalModContext_->hashDatabase = &hashDatabase_;
            logicalModContext_->family = &family;
            logicalModContext_->unitId = data.unitId;
            logicalModWindow_ = createLogicalFamilyModWindow(hwnd_, *logicalModContext_);
            if (!logicalModWindow_) {
                logicalModContext_.reset();
                showError(hwnd_, L"Could not open the logical-record MOD window.");
                return;
            }
        }

        const auto roleName = data.role == Role::Primary ? L"Primary" : L"Comparison";
        setStatus(std::wstring(roleName) + L" " + utf8ToWide(std::string(family.name)) +
                  L" - " + logicalUnitSummaryW(family, data.unitId) +
                  L" is open in the MOD window");
    }

    void openSummonSlotModWindow(const NodeData& data) {
        if (!data.save || data.kind != NodeKind::LogicalSlot) return;

        if (summonModContext_ && summonModWindow_ && IsWindow(summonModWindow_)) {
            selectSummonSlotInModWindow(*summonModContext_, *data.save, data.unitId);
        } else {
            summonModContext_ = std::make_unique<SummonSlotModDialogContext>();
            summonModContext_->save = data.save;
            summonModContext_->hashDatabase = &hashDatabase_;
            summonModContext_->unitId = data.unitId;
            summonModWindow_ = createSummonSlotModWindow(hwnd_, *summonModContext_);
            if (!summonModWindow_) {
                summonModContext_.reset();
                showError(hwnd_, L"Could not open the Summon Inventory Slot MOD window.");
                return;
            }
        }

        const auto roleName = data.role == Role::Primary ? L"Primary" : L"Comparison";
        setStatus(std::wstring(roleName) + L" summon slot " + numberW(data.unitId) +
                  L" is open in the MOD window");
    }

    void openMasteryTreeModWindow(const NodeData& data) {
        if (!data.save || data.kind != NodeKind::MasteryEntry) return;

        if (masteryModContext_ && masteryModWindow_ && IsWindow(masteryModWindow_)) {
            selectMasteryTreeEntryInModWindow(*masteryModContext_, *data.save, data.unitId);
        } else {
            masteryModContext_ = std::make_unique<MasteryTreeModDialogContext>();
            masteryModContext_->save = data.save;
            masteryModContext_->hashDatabase = &hashDatabase_;
            masteryModContext_->unitId = data.unitId;
            masteryModWindow_ = createMasteryTreeModWindow(hwnd_, *masteryModContext_);
            if (!masteryModWindow_) {
                masteryModContext_.reset();
                showError(hwnd_, L"Could not open the Mastery Tree MOD window.");
                return;
            }
        }

        const auto roleName = data.role == Role::Primary ? L"Primary" : L"Comparison";
        setStatus(std::wstring(roleName) + L" Mastery Tree - " +
                  logicalUnitSummaryW(gdtv::masteryTreeFamily(), data.unitId) +
                  L" is open in the MOD window");
    }

    void focusKey(Role role, std::uint32_t key) {
        auto* save = role == Role::Primary ? primary_.get() : compare_.get();
        const auto* targetGroup = save ? save->findGroup(key) : nullptr;
        if (!targetGroup) return;
        for (HTREEITEM root = TreeView_GetRoot(tree_); root; root = TreeView_GetNextSibling(tree_, root)) {
            const auto* rootData = nodeData(root);
            if (!rootData || rootData->kind != NodeKind::SaveRoot || rootData->role != role) continue;
            TreeView_Expand(tree_, root, TVE_EXPAND);
            for (HTREEITEM child = TreeView_GetChild(tree_, root); child; child = TreeView_GetNextSibling(tree_, child)) {
                const auto* childData = nodeData(child);
                if (!childData || childData->kind != NodeKind::VectorsRoot) continue;
                TreeView_Expand(tree_, child, TVE_EXPAND);
                for (HTREEITEM vectorItem = TreeView_GetChild(tree_, child); vectorItem;
                     vectorItem = TreeView_GetNextSibling(tree_, vectorItem)) {
                    const auto* vectorData = nodeData(vectorItem);
                    if (!vectorData || vectorData->kind != NodeKind::Vector ||
                        vectorData->vectorNumber != targetGroup->vectorNumber) continue;
                    populateIfNeeded(vectorItem);
                    TreeView_Expand(tree_, vectorItem, TVE_EXPAND);
                    for (HTREEITEM keyItem = TreeView_GetChild(tree_, vectorItem); keyItem;
                         keyItem = TreeView_GetNextSibling(tree_, keyItem)) {
                        const auto* keyData = nodeData(keyItem);
                        if (keyData && keyData->kind == NodeKind::Key && keyData->key == key) {
                            TreeView_SelectItem(tree_, keyItem);
                            TreeView_EnsureVisible(tree_, keyItem);
                            SetFocus(tree_);
                            return;
                        }
                    }
                }
            }
        }
    }

    std::optional<std::tuple<Role, gdtv::SaveData*, const gdtv::KeyGroup*, const gdtv::Record*>> selectedRecord() const {
        const auto* data = nodeData(TreeView_GetSelection(tree_));
        if (!data || (data->kind != NodeKind::Record && data->kind != NodeKind::HashOccurrence &&
                      data->kind != NodeKind::LogicalField) || !data->save) return std::nullopt;
        const auto* group = data->save->findGroup(data->key);
        if (!group || data->recordOrdinal >= group->records.size()) return std::nullopt;
        return std::tuple{data->role, data->save, group, &group->records[data->recordOrdinal]};
    }

    std::optional<std::tuple<Role, gdtv::SaveData*, const gdtv::KeyGroup*>> selectedKey() const {
        const auto* data = nodeData(TreeView_GetSelection(tree_));
        if (!data) return std::nullopt;
        if ((data->kind == NodeKind::Key || data->kind == NodeKind::Record || data->kind == NodeKind::HashOccurrence ||
             data->kind == NodeKind::LogicalField || data->kind == NodeKind::SearchKey ||
             data->kind == NodeKind::RelationshipPhysicalSection ||
             data->kind == NodeKind::RelationshipMember ||
             data->kind == NodeKind::RelationshipReferenceSection ||
             data->kind == NodeKind::RelationshipReferenceCategory) &&
            data->save) {
            if (const auto* group = data->save->findGroup(data->key)) return std::tuple{data->role, data->save, group};
        }
        if (data->kind == NodeKind::ComparisonKey) {
            if (primary_) if (const auto* group = primary_->findGroup(data->key)) return std::tuple{Role::Primary, primary_.get(), group};
            if (compare_) if (const auto* group = compare_->findGroup(data->key)) return std::tuple{Role::Compare, compare_.get(), group};
        }
        return std::nullopt;
    }

    void copyStableLocator() {
        std::wstring locator;
        if (const auto record = selectedRecord()) {
            const auto* group = std::get<2>(*record);
            const auto* item = std::get<3>(*record);
            locator = L"V" + std::to_wstring(group->vectorNumber) + L":" + utf8ToWide(group->keyHex()) +
                      L":" + numberW(item->index);
        } else if (const auto key = selectedKey()) {
            locator = utf8ToWide(std::get<2>(*key)->stableLocator());
        } else {
            setStatus(L"Select a key or record first");
            return;
        }
        setClipboardText(hwnd_, locator);
        setStatus(L"Copied " + locator);
    }

    void exportPayload() {
        const auto selected = selectedRecord();
        if (!selected) {
            MessageBoxW(hwnd_, L"Select an individual record first.", GDTV_APP_NAME_W, MB_OK | MB_ICONINFORMATION);
            return;
        }
        auto* save = std::get<1>(*selected);
        const auto* group = std::get<2>(*selected);
        const auto* record = std::get<3>(*selected);
        const auto initial = L"V" + std::to_wstring(group->vectorNumber) + L"-Key-" +
                             utf8ToWide(group->keyHex().substr(2)) + L"-Index-" + numberW(record->index) + L".bin";
        const auto path = saveFileDialog(hwnd_, L"Export payload", initial);
        if (!path) return;
        try {
            const auto payload = save->payloadView(*record);
            std::ofstream output(*path, std::ios::binary);
            if (!output) throw std::runtime_error("could not create output file");
            output.write(payload.data(), static_cast<std::streamsize>(payload.size()));
            if (!output) throw std::runtime_error("could not write complete payload");
            setStatus(L"Exported " + numberW(payload.size()) + L" bytes to " + path->wstring());
        } catch (const std::exception& error) {
            showError(hwnd_, L"Could not export payload:\n\n" + utf8ToWide(error.what()));
        }
    }

    const gdtv::Record* counterpartRecord(Role role, const gdtv::KeyGroup& group,
                                          const gdtv::Record& record) const {
        const auto* otherSave = role == Role::Primary ? compare_.get() : primary_.get();
        const auto* otherGroup = otherSave ? otherSave->findGroup(group.key) : nullptr;
        if (!otherGroup) return nullptr;
        const auto it = std::lower_bound(otherGroup->records.begin(), otherGroup->records.end(), record.index,
            [](const gdtv::Record& candidate, std::uint32_t index) { return candidate.index < index; });
        return it != otherGroup->records.end() && it->index == record.index ? &*it : nullptr;
    }

    void setDetails(const std::wstring& details, const std::wstring& hex = {},
                    const std::wstring& values = {}) {
        SetWindowTextW(detailsEdit_, details.c_str());
        SetWindowTextW(hexEdit_, hex.c_str());
        SetWindowTextW(valuesEdit_, values.c_str());
    }

    void showSelectedNode() {
        const auto* data = nodeData(TreeView_GetSelection(tree_));
        if (!data) return;
        std::wostringstream out;
        std::wstring hex;
        std::wstring values;
        switch (data->kind) {
        case NodeKind::SaveRoot:
            if (data->save) {
                out << (data->role == Role::Primary ? L"PRIMARY SAVE\r\n" : L"COMPARISON SAVE\r\n")
                    << L"File: " << data->save->path().wstring() << L"\r\n"
                    << L"Size: " << numberW(data->save->fileSize()) << L" bytes\r\n"
                    << L"Root table: " << hexW(data->save->rootOffset()) << L"\r\n"
                    << L"Root vtable: " << hexW(data->save->rootVtableOffset()) << L"\r\n"
                    << L"Root scalar 0: " << data->save->rootScalar0() << L"\r\n"
                    << L"Records: " << numberW(data->save->recordCount()) << L"\r\n"
                    << L"Keys: " << numberW(data->save->keyCount()) << L"\r\n"
                    << L"Linked structures: " << numberW(data->save->linkedClusters().size()) << L"\r\n"
                    << L"Section names loaded: " << numberW(sectionNames_.size()) << L"\r\n"
                    << L"Structural map rows: " << numberW(sectionMap_.size()) << L"\r\n"
                    << L"Character section names: " << numberW(characterSections_.size()) << L"\r\n"
                    << L"Hash database rows: " << numberW(hashDatabase_.databaseEntryCount()) << L"\r\n"
                    << L"Edited in memory: " << (data->save->dirty() ? L"Yes" : L"No") << L"\r\n"
                    << L"Edit operations: " << numberW(data->save->editCount()) << L"\r\n";
                if (!sectionNamesPath_.empty()) {
                    out << L"Section names source: " << sectionNamesPath_.wstring() << L"\r\n";
                }
            }
            break;
        case NodeKind::RootInfo:
            if (data->save) {
                out << L"ROOT TABLE\r\n"
                    << L"Root offset: " << hexW(data->save->rootOffset()) << L"\r\n"
                    << L"Vtable offset: " << hexW(data->save->rootVtableOffset()) << L"\r\n"
                    << L"Scalar field 0: " << data->save->rootScalar0() << L"\r\n\r\n"
                    << L"The ten root vectors are the save's logical TOC buckets.\r\n";
            }
            break;
        case NodeKind::Vector:
            if (data->save && data->vectorNumber >= 1 && data->vectorNumber <= 10) {
                const auto& vector = data->save->vectors()[data->vectorNumber - 1U];
                out << L"ROOT VECTOR " << vector.number << L"\r\n"
                    << L"Official table: " << utf8ToWide(std::string(gdtv::valueTypeName(gdtv::valueTypeForVector(vector.number)))) << L"Table\r\n"
                    << L"Element size: " << gdtv::valueTypeSize(gdtv::valueTypeForVector(vector.number)) << L" bytes\r\n"
                    << L"Present: " << (vector.present ? L"Yes" : L"No") << L"\r\n";
                if (vector.offset) out << L"Vector offset: " << hexW(*vector.offset) << L"\r\n";
                out << L"Save units: " << numberW(vector.count) << L"\r\n"
                    << L"Unique IDTypes: " << numberW(vector.keys.size()) << L"\r\n";
            }
            break;
        case NodeKind::Key:
        case NodeKind::SearchKey:
            showKeyDetails(*data, out);
            break;
        case NodeKind::Page:
            if (data->save) {
                const auto* group = data->save->findGroup(data->key);
                if (group && data->start < data->end && data->end <= group->records.size()) {
                    out << L"RECORD PAGE\r\n"
                        << L"Stable key: " << utf8ToWide(group->stableLocator()) << L"\r\n"
                        << L"Records: " << numberW(data->start + 1U) << L"-" << numberW(data->end) << L"\r\n"
                        << L"Index range: " << numberW(group->records[data->start].index) << L"-"
                        << numberW(group->records[data->end - 1U].index) << L"\r\n";
                }
            }
            break;
        case NodeKind::Record:
            showRecordDetails(*data, out, hex, values);
            break;
        case NodeKind::RelationshipRoot:
            if (data->save) {
                auto& cache = relationshipCache(*data->save);
                out << L"SAVE RELATIONSHIP MAP\r\n"
                    << L"Physical sections: " << numberW(cache.physical.size()) << L"\r\n"
                    << L"Logical families: " << numberW(cache.families.size()) << L"\r\n\r\n"
                    << L"Physical Order lists every parsed IDType by its first serialized record offset.\r\n"
                    << L"Logical Families combines confirmed structures and exact matching UnitID arrays.\r\n"
                    << L"Hash Reference Links shows which database categories are referenced by each UInt section.\r\n\r\n"
                    << L"Physical adjacency alone is not treated as proof of a logical relationship.\r\n";
            }
            break;
        case NodeKind::RelationshipPhysicalRoot:
            if (data->save) {
                const auto& physical = relationshipCache(*data->save).physical;
                out << L"PHYSICAL SECTION ORDER\r\n"
                    << L"Sections: " << numberW(physical.size()) << L"\r\n";
                if (!physical.empty()) {
                    out << L"First record offset: " << hexW(physical.front().firstRecordOffset) << L"\r\n"
                        << L"Last record offset: " << hexW(physical.back().lastRecordOffset) << L"\r\n";
                }
                out << L"\r\nThis is the complete parsed section chain from the first physical section to the last.\r\n";
            }
            break;
        case NodeKind::RelationshipPhysicalPage:
            out << L"PHYSICAL SECTION PAGE\r\n"
                << L"Positions: " << numberW(data->start + 1U) << L"-" << numberW(data->end) << L"\r\n";
            break;
        case NodeKind::RelationshipPhysicalSection:
            if (data->save) {
                auto& cache = relationshipCache(*data->save);
                if (data->relationshipIndex < cache.physical.size()) {
                    const auto& section = cache.physical[data->relationshipIndex];
                    const auto* group = data->save->findGroup(section.key);
                    out << L"PHYSICAL SECTION #" << numberW(data->relationshipIndex + 1U) << L"\r\n"
                        << L"First record offset: " << hexW(section.firstRecordOffset) << L"\r\n"
                        << L"Last record offset: " << hexW(section.lastRecordOffset) << L"\r\n";
                    if (section.firstPayloadOffset != 0U) {
                        out << L"First payload offset: " << hexW(section.firstPayloadOffset) << L"\r\n"
                            << L"Last payload end: " << hexW(section.lastPayloadEnd) << L"\r\n";
                    }
                    if (group) {
                        out << L"Section: " << groupLabel(*group) << L"\r\n";
                        appendSectionInfo(out, section.key);
                    }
                    out << L"\r\nPHYSICAL NEIGHBORS\r\n";
                    if (data->relationshipIndex > 0U) {
                        const auto previousKey = cache.physical[data->relationshipIndex - 1U].key;
                        const auto* previous = data->save->findGroup(previousKey);
                        if (previous) out << L"Previous: " << groupLabel(*previous) << L"\r\n";
                    } else {
                        out << L"Previous: <first section>\r\n";
                    }
                    if (data->relationshipIndex + 1U < cache.physical.size()) {
                        const auto nextKey = cache.physical[data->relationshipIndex + 1U].key;
                        const auto* next = data->save->findGroup(nextKey);
                        if (next) out << L"Next: " << groupLabel(*next) << L"\r\n";
                    } else {
                        out << L"Next: <last section>\r\n";
                    }
                    const auto peers = relationshipPeers(cache, section.key);
                    out << L"\r\nEXACT UNITID PEERS: " << numberW(peers.size()) << L"\r\n";
                    for (const auto peerKey : peers) {
                        const auto* peer = data->save->findGroup(peerKey);
                        if (peer) out << groupLabel(*peer) << L"\r\n";
                    }
                    out << L"\r\nLOGICAL FAMILY MEMBERSHIP\r\n";
                    bool foundFamily = false;
                    for (const auto& family : cache.families) {
                        if (std::find(family.keys.begin(), family.keys.end(), section.key) == family.keys.end()) continue;
                        foundFamily = true;
                        out << utf8ToWide(family.name) << L" [" << utf8ToWide(family.confidence) << L"]\r\n";
                    }
                    if (!foundFamily) out << L"No confirmed or automatically detected family.\r\n";
                }
            }
            break;
        case NodeKind::RelationshipFamiliesRoot:
            if (data->save) {
                const auto& families = relationshipCache(*data->save).families;
                std::size_t explicitCount = 0U;
                for (const auto& family : families) if (family.explicitDefinition) ++explicitCount;
                out << L"LOGICAL FAMILIES\r\n"
                    << L"Total families: " << numberW(families.size()) << L"\r\n"
                    << L"Explicit confirmed families: " << numberW(explicitCount) << L"\r\n"
                    << L"Automatically detected exact-UnitID families: "
                    << numberW(families.size() - explicitCount) << L"\r\n";
            }
            break;
        case NodeKind::RelationshipFamily:
            if (data->save) {
                const auto& families = relationshipCache(*data->save).families;
                if (data->relationshipIndex < families.size()) {
                    const auto& family = families[data->relationshipIndex];
                    out << utf8ToWide(family.name) << L"\r\n"
                        << L"Confidence: " << utf8ToWide(family.confidence) << L"\r\n"
                        << L"Definition: " << (family.explicitDefinition ? L"Explicit" : L"Automatically detected") << L"\r\n"
                        << L"Shared UnitIDs: " << numberW(family.recordCount) << L"\r\n"
                        << L"Sections: " << numberW(family.keys.size()) << L"\r\n"
                        << L"Evidence: " << utf8ToWide(family.reason) << L"\r\n\r\nMEMBERS\r\n";
                    for (const auto key : family.keys) {
                        const auto* group = data->save->findGroup(key);
                        if (group) out << groupLabel(*group) << L"\r\n";
                    }
                }
            }
            break;
        case NodeKind::RelationshipMember:
            if (data->save) {
                const auto* group = data->save->findGroup(data->key);
                static constexpr std::array<const wchar_t*, 4> relationLabels{
                    L"Previous physical section", L"Next physical section",
                    L"Exact matching UnitID set", L"Logical family member"
                };
                const auto relationIndex = std::min<std::size_t>(data->relationshipSubIndex,
                                                                  relationLabels.size() - 1U);
                out << L"SECTION RELATIONSHIP\r\n"
                    << L"Relationship: " << relationLabels[relationIndex] << L"\r\n";
                if (data->logicalFamilyAnchor != 0U) {
                    out << L"Source key: " << utf8ToWide(gdtv::KeyGroup{0U, data->logicalFamilyAnchor, {}}.keyHex()) << L"\r\n";
                }
                if (group) {
                    out << L"Target: " << groupLabel(*group) << L"\r\n";
                    appendSectionInfo(out, group->key);
                }
            }
            break;
        case NodeKind::RelationshipReferencesRoot:
            if (data->save) {
                auto& cache = relationshipCache(*data->save);
                buildReferenceSummaries(*data->save, cache);
                std::uint64_t resolved = 0U;
                for (const auto& reference : cache.references) resolved += reference.resolvedValues;
                out << L"HASH REFERENCE LINKS\r\n"
                    << L"Sections with resolved references: " << numberW(cache.references.size()) << L"\r\n"
                    << L"Resolved reference occurrences: " << numberW(resolved) << L"\r\n\r\n"
                    << L"Each UInt section is linked to the hash-database categories found in its stored values.\r\n"
                    << L"This view does not change the existing global hash resolution behavior.\r\n";
            }
            break;
        case NodeKind::RelationshipReferenceSection:
            if (data->save) {
                auto& cache = relationshipCache(*data->save);
                buildReferenceSummaries(*data->save, cache);
                if (data->relationshipIndex < cache.references.size()) {
                    const auto& reference = cache.references[data->relationshipIndex];
                    const auto* group = data->save->findGroup(reference.key);
                    out << L"REFERENCE SOURCE SECTION\r\n";
                    if (group) out << groupLabel(*group) << L"\r\n";
                    out << L"Total UInt values: " << numberW(reference.totalValues) << L"\r\n"
                        << L"Resolved non-sentinel references: " << numberW(reference.resolvedValues) << L"\r\n"
                        << L"Unresolved nonzero values: " << numberW(reference.unresolvedValues) << L"\r\n"
                        << L"Referenced categories: " << numberW(reference.categories.size()) << L"\r\n\r\n";
                    for (const auto& category : reference.categories) {
                        out << utf8ToWide(category.category) << L": " << numberW(category.occurrences) << L"\r\n";
                    }
                    appendSectionInfo(out, reference.key);
                }
            }
            break;
        case NodeKind::RelationshipReferenceCategory:
            if (data->save) {
                auto& cache = relationshipCache(*data->save);
                buildReferenceSummaries(*data->save, cache);
                if (data->relationshipIndex < cache.references.size()) {
                    const auto& reference = cache.references[data->relationshipIndex];
                    if (data->relationshipSubIndex < reference.categories.size()) {
                        const auto& category = reference.categories[data->relationshipSubIndex];
                        out << L"REFERENCE CATEGORY\r\n"
                            << L"Source section: " << sectionLocator(reference.key) << L"\r\n"
                            << L"Category: " << utf8ToWide(category.category) << L"\r\n"
                            << L"Occurrences: " << numberW(category.occurrences) << L"\r\n\r\n"
                            << L"EXAMPLE HASHES\r\n";
                        for (const auto hash : category.exampleHashes) {
                            out << utf8ToWide(gdtv::hashHex(hash));
                            if (const auto* entry = hashDatabase_.preferred(hash)) {
                                out << L" \u2014 " << utf8ToWide(entry->displayName)
                                    << L" (" << utf8ToWide(entry->id) << L")";
                            }
                            out << L"\r\n";
                        }
                    }
                }
            }
            break;
        case NodeKind::LogicalRoot:
            if (data->save) {
                out << L"LOGICAL SAVE RECORDS\r\n"
                    << L"Known record families are assembled from parallel IDType arrays that share UnitIDs.\r\n"
                    << L"v0.29.1 adds a built-in Global Empty Slot available to every filtered ID list.\r\n"
                    << L"The raw root-vector tree remains available below.\r\n";
            }
            break;
        case NodeKind::LogicalFamily:
            if (data->save) {
                const auto anchorKey = data->logicalFamilyAnchor != 0U
                    ? data->logicalFamilyAnchor : data->key;
                const auto* family = gdtv::logicalFamilyForAnchor(anchorKey);
                if (family) {
                    const auto* anchor = data->save->findGroup(family->anchorKey);
                    out << utf8ToWide(std::string(family->name)) << L"\r\n"
                        << L"Entries: " << numberW(anchor ? anchor->records.size() : 0U) << L"\r\n"
                        << L"Fields per entry: " << numberW(family->fieldCount) << L"\r\n\r\n"
                        << L"Click an entry to open its MOD window.\r\n"
                        << L"Edits stay in memory until File > Save Edited ... As is used.\r\n";
                }
            }
            break;
        case NodeKind::LogicalCharacter:
            if (data->save) {
                const auto* family = gdtv::logicalFamilyForAnchor(data->logicalFamilyAnchor);
                out << L"CHARACTER SECTION\r\n"
                    << characterSectionDetails(data->characterGroup) << L"\r\n";
                if (family) out << L"Family: " << utf8ToWide(std::string(family->name)) << L"\r\n";
            }
            break;
        case NodeKind::LogicalPage:
            out << L"LOGICAL ENTRY PAGE\r\n";
            if (data->logicalNamespace != 0U) {
                out << L"Character group: " << numberW(data->characterGroup) << L"\r\n"
                    << L"Namespace: " << numberW(data->logicalNamespace) << L"\r\n";
            }
            out << L"Record ordinals: " << numberW(data->start) << L"-"
                << numberW(data->end == 0U ? 0U : data->end - 1U) << L"\r\n";
            break;
        case NodeKind::LogicalSlot:
            if (data->save) {
                const auto anchorKey = data->logicalFamilyAnchor != 0U
                    ? data->logicalFamilyAnchor : data->key;
                const auto* family = gdtv::logicalFamilyForAnchor(anchorKey);
                if (family) {
                    out << logicalUnitSummaryW(*family, data->unitId) << L"\r\n"
                        << L"Raw UnitID: " << numberW(data->unitId) << L"\r\n\r\n"
                        << L"Click this entry to open the MOD window.\r\n";
                }
            }
            break;
        case NodeKind::LogicalField:
            if (data->save) {
                const auto* family = gdtv::logicalFamilyForAnchor(data->logicalFamilyAnchor);
                if (family && data->logicalFieldIndex < family->fieldCount) {
                    const auto& field = family->fields[data->logicalFieldIndex];
                    const auto offset = data->save->elementOffset(field.key, data->unitId, field.elementIndex);
                    out << L"Legacy locator: " << legacyLocatorW(field.key) << L"\r\n"
                        << L"Exact value offset: " << (offset ? hexW(*offset) : L"<missing>") << L"\r\n"
                        << L"Current Value: "
                        << formatElementValue(*data->save, field.key, data->unitId,
                                              field.elementIndex, field.kind) << L"\r\n";
                    if (showDetailedLogicalInfo_) {
                        std::wostringstream detailed;
                        showRecordDetails(*data, detailed, hex, values);
                        out << L"\r\nVIEWER INFO\r\n"
                            << L"Family: " << utf8ToWide(std::string(family->name)) << L"\r\n"
                            << L"Field: " << utf8ToWide(std::string(field.locator)) << L"\r\n"
                            << L"Meaning: " << utf8ToWide(std::string(field.label)) << L"\r\n"
                            << L"Confidence: " << utf8ToWide(std::string(field.confidence)) << L"\r\n"
                            << L"UnitID / Entry: " << numberW(data->unitId) << L"\r\n"
                            << L"Element index: " << numberW(field.elementIndex) << L"\r\n\r\n"
                            << detailed.str();
                    }
                }
            }
            break;
        case NodeKind::MasteryRoot:
            if (data->save) {
                const auto& family = gdtv::masteryTreeFamily();
                const auto* anchor = data->save->findGroup(family.anchorKey);
                out << L"MASTERY TREE\r\n"
                    << L"Entries: " << numberW(anchor ? anchor->records.size() : 0U) << L"\r\n"
                    << L"FF41060000: Mastery Tree ID\r\n"
                    << L"FF42060000: Activated / state bitfield\r\n";
                if (data->save->findGroup(0x0645U)) {
                    out << L"FF45060000: Legacy state companion (present in this save)\r\n";
                }
                out << L"\r\nClick a Mastery Tree Entry to open its MOD window.\r\n"
                    << L"Edits stay in memory until File > Save Edited ... As is used.\r\n";
            }
            break;
        case NodeKind::MasteryCharacter:
            out << L"MASTERY TREE CHARACTER SECTION\r\n";
            if (data->sharedGroup) out << L"Shared / Global\r\n";
            else out << characterSectionDetails(data->characterGroup) << L"\r\n";
            break;
        case NodeKind::MasteryPage:
            out << L"MASTERY TREE SLOT PAGE\r\n"
                << L"Record ordinals: " << numberW(data->start) << L"-"
                << numberW(data->end == 0U ? 0U : data->end - 1U) << L"\r\n";
            break;
        case NodeKind::MasteryEntry:
            if (data->save) {
                out << logicalUnitSummaryW(gdtv::masteryTreeFamily(), data->unitId) << L"\r\n"
                    << L"Raw UnitID: " << numberW(data->unitId) << L"\r\n\r\n"
                    << L"Click this entry to open the MOD window.\r\n";
            }
            break;
        case NodeKind::MasteryField:
            if (data->save) {
                const auto& family = gdtv::masteryTreeFamily();
                if (data->logicalFieldIndex < family.fieldCount) {
                    const auto& field = family.fields[data->logicalFieldIndex];
                    const auto offset = data->save->elementOffset(field.key, data->unitId, field.elementIndex);
                    out << L"Legacy locator: " << legacyLocatorW(field.key) << L"\r\n"
                        << L"Exact value offset: " << (offset ? hexW(*offset) : L"<missing>") << L"\r\n"
                        << L"Current Value: "
                        << formatElementValue(*data->save, field.key, data->unitId,
                                              field.elementIndex, field.kind) << L"\r\n";
                    if (showDetailedLogicalInfo_) {
                        std::wostringstream detailed;
                        showRecordDetails(*data, detailed, hex, values);
                        out << L"\r\nVIEWER INFO\r\n"
                            << L"Field: " << legacyLocatorW(field.key) << L"\r\n"
                            << L"Meaning: " << utf8ToWide(std::string(field.label)) << L"\r\n"
                            << L"Confidence: " << utf8ToWide(std::string(field.confidence)) << L"\r\n"
                            << L"UnitID / Entry: " << numberW(data->unitId) << L"\r\n"
                            << L"Element index: " << numberW(field.elementIndex) << L"\r\n\r\n"
                            << detailed.str();
                    }
                }
            }
            break;
        case NodeKind::LinkedRoot:
            if (data->save) {
                out << L"LINKED STRUCTURES\r\n"
                    << L"Detected groups: " << numberW(data->save->linkedClusters().size()) << L"\r\n\r\n"
                    << L"Keys are grouped when they share the exact same logical index set and are near each other numerically.\r\n";
            }
            break;
        case NodeKind::LinkedCluster:
            showClusterDetails(*data, out);
            break;
        case NodeKind::ComparisonRoot:
            out << L"COMPARISON BY STABLE KEY\r\n"
                << L"Changed / moved: " << numberW(compareKeys_[0].size()) << L"\r\n"
                << L"Primary-only: " << numberW(compareKeys_[1].size()) << L"\r\n"
                << L"Comparison-only: " << numberW(compareKeys_[2].size()) << L"\r\n"
                << L"Unchanged: " << numberW(compareKeys_[3].size()) << L"\r\n";
            break;
        case NodeKind::ComparisonCategory:
            out << L"COMPARISON CATEGORY\r\nKeys: "
                << numberW(compareKeys_[static_cast<std::size_t>(data->category)].size()) << L"\r\n";
            break;
        case NodeKind::ComparisonKey:
            showComparisonKeyDetails(data->key, out);
            break;
        case NodeKind::SearchRoot:
            out << L"SEARCH RESULTS\r\nMatches: " << numberW(searchResults_.size()) << L"\r\n"
                << L"Double-click a result to reveal it under its root vector.\r\n";
            break;
        case NodeKind::HashRoot:
            out << L"UNIFIED HASH DATABASE\r\n"
                << L"Rows: " << numberW(hashDatabase_.databaseEntryCount()) << L"\r\n"
                << L"Unique hashes: " << numberW(hashDatabase_.uniqueHashCount()) << L"\r\n"
                << L"Rows with internal names: " << numberW(hashDatabase_.baseEntryCount()) << L"\r\n"
                << L"Internal names verified by custom XXHash32: " << numberW(hashDatabase_.verifiedBaseCount()) << L"\r\n"
                << L"Internal-name/hash mismatches retained: " << numberW(hashDatabase_.baseMismatchCount()) << L"\r\n"
                << L"Rows with in-game names: " << numberW(hashDatabase_.friendlyEntryCount()) << L"\r\n"
                << L"Endian mismatches: " << numberW(hashDatabase_.endianMismatchCount()) << L"\r\n"
                << L"Invalid lines skipped: " << numberW(hashDatabase_.invalidLineCount()) << L"\r\n"
                << L"Session additions: " << numberW(hashDatabase_.userEntryCount()) << L"\r\n";
            break;
        case NodeKind::HashFriendlyRoot:
            out << L"IN-GAME NAMES FROM GBFR-HASH-DATABASE.TXT\r\nRows: "
                << numberW(hashDatabase_.friendlyEntryCount())
                << L"\r\nInternal-name rows sharing the same hash are resolved automatically.\r\n";
            break;
        case NodeKind::HashUserRoot:
            out << L"SESSION HASH DATABASE ADDITIONS\r\nRows: " << numberW(hashDatabase_.userEntryCount())
                << L"\r\nUse Hashes > Save Hash Database to write them into the unified text file.\r\n";
            break;
        case NodeKind::HashEntry:
            if (const auto* entries = hashDatabase_.find(data->hashValue)) {
                out << L"HASH MAPPING\r\nCanonical: 0x" << utf8ToWide(gdtv::hashHex(data->hashValue))
                    << L"\r\nRaw little-endian: " << utf8ToWide(gdtv::hashRawLittleEndian(data->hashValue)) << L"\r\n";
                for (const auto& entry : *entries) {
                    out << L"\r\n"
                        << (entry.userDefined ? L"SESSION ADDITION" : L"UNIFIED TEXT DATABASE ROW")
                        << L"\r\n";
                    if (!entry.displayName.empty()) out << L"In-game name: " << utf8ToWide(entry.displayName) << L"\r\n";
                    if (!entry.id.empty()) out << L"Internal name: " << utf8ToWide(entry.id) << L"\r\n";
                    if (!entry.category.empty()) out << L"Type: " << utf8ToWide(entry.category) << L"\r\n";
                    if (!entry.version.empty()) out << L"Version: " << utf8ToWide(entry.version) << L"\r\n";
                    if (!entry.source.empty()) out << L"Source: " << utf8ToWide(entry.source) << L"\r\n";
                    if (!entry.notes.empty()) out << L"Notes: " << utf8ToWide(entry.notes) << L"\r\n";
                    out << L"Algorithm verified: " << (entry.algorithmVerified ? L"Yes" : L"No / not applicable") << L"\r\n";
                }
            }
            if (primary_) out << L"\r\nPrimary occurrences: " << numberW(primary_->findUIntValue(data->hashValue).size()) << L"\r\n";
            if (compare_) out << L"Comparison occurrences: " << numberW(compare_->findUIntValue(data->hashValue).size()) << L"\r\n";
            break;
        case NodeKind::HashOccurrence:
            showRecordDetails(*data, out, hex, values);
            out << L"\r\nHASH OCCURRENCE\r\n"
                << L"Hash: 0x" << utf8ToWide(gdtv::hashHex(data->hashValue)) << L"\r\n"
                << L"Raw little-endian: " << utf8ToWide(gdtv::hashRawLittleEndian(data->hashValue)) << L"\r\n"
                << L"ValueData element index: " << numberW(data->start) << L"\r\n";
            break;
        default:
            break;
        }
        setDetails(out.str(), hex, values);
    }

    void appendSectionInfo(std::wostringstream& out, std::uint32_t key) const {
        out << L"\r\nSECTION IDENTIFICATION\r\n"
            << L"Preferred locator: " << sectionLocator(key) << L"\r\n"
            << L"Fallback generated locator: " << legacyLocatorW(key) << L"\r\n";
        if (const auto* named = sectionNames_.find(key)) {
            if (!named->name.empty()) out << L"Name: " << utf8ToWide(named->name) << L"\r\n";
            if (!named->subsystem.empty()) out << L"Subsystem: " << utf8ToWide(named->subsystem) << L"\r\n";
            if (!named->confidence.empty()) out << L"Name confidence: " << utf8ToWide(named->confidence) << L"\r\n";
            if (!named->officialSaveIdType.empty()) {
                out << L"Official SaveIDType: " << utf8ToWide(named->officialSaveIdType) << L"\r\n";
            }
            if (!named->storageType.empty()) out << L"Expected storage type: " << utf8ToWide(named->storageType) << L"\r\n";
            if (!named->hashCategory.empty()) out << L"Expected hash category: " << utf8ToWide(named->hashCategory) << L"\r\n";
            if (!named->internalPrefix.empty()) out << L"Expected internal prefix: " << utf8ToWide(named->internalPrefix) << L"\r\n";
            if (!named->note.empty()) out << L"Evidence / notes: " << utf8ToWide(named->note) << L"\r\n";
            if (!named->recommendedTest.empty()) {
                out << L"Recommended verification: " << utf8ToWide(named->recommendedTest) << L"\r\n";
            }
        }
        if (const auto* section = sectionMap_.find(key)) {
            out << L"\r\nSTRUCTURAL SECTION MAP\r\n";
            if (!section->name.empty()) out << L"Legacy name / notes: " << utf8ToWide(section->name) << L"\r\n";
            if (!section->locator.empty()) out << L"Legacy locator: " << utf8ToWide(section->locator) << L"\r\n";
            if (!section->stableLocator.empty()) out << L"Mapped locator: " << utf8ToWide(section->stableLocator) << L"\r\n";
            if (!section->stride.empty()) out << L"Observed stride: " << utf8ToWide(section->stride) << L"\r\n";
            if (!section->mappingConfidence.empty()) out << L"Mapping confidence: " << utf8ToWide(section->mappingConfidence) << L"\r\n";
            if (!section->originalGroup.empty()) out << L"Original linked group(s): " << utf8ToWide(section->originalGroup) << L"\r\n";
            if (!section->dlcGroup.empty()) out << L"DLC linked group(s): " << utf8ToWide(section->dlcGroup) << L"\r\n";
            if (!section->countAudit.empty()) out << L"Count audit: " << utf8ToWide(section->countAudit) << L"\r\n";
        }
    }

    void showKeyDetails(const NodeData& data, std::wostringstream& out) const {
        const auto* group = data.save ? data.save->findGroup(data.key) : nullptr;
        if (!group) return;
        out << L"KEY / SECTION\r\n"
            << L"Role: " << (data.role == Role::Primary ? L"Primary" : L"Comparison") << L"\r\n"
            << L"Stable locator: " << utf8ToWide(group->stableLocator()) << L"\r\n"
            << L"Key: " << utf8ToWide(group->keyHex()) << L" (" << group->key << L")\r\n"
            << L"Root vector: " << group->vectorNumber << L"\r\n"
            << L"Official value type: " << utf8ToWide(std::string(gdtv::valueTypeName(group->valueType()))) << L"\r\n"
            << L"Element size: " << group->elementSize() << L" bytes\r\n"
            << L"Save units: " << numberW(group->records.size()) << L"\r\n"
            << L"UnitID ranges: " << utf8ToWide(group->indexRanges()) << L"\r\n"
            << L"ValueData element counts: " << utf8ToWide(group->elementCounts()) << L"\r\n"
            << L"ValueData byte sizes: " << utf8ToWide(group->payloadByteLengths()) << L"\r\n";
        appendSectionInfo(out, group->key);
        if (primary_ && compare_) {
            const auto comparison = gdtv::compareKey(primary_.get(), compare_.get(), group->key);
            out << L"\r\nCOMPARISON\r\n"
                << L"Status: " << utf8ToWide(comparison.status) << L"\r\n"
                << L"Common records: " << numberW(comparison.commonRecords) << L"\r\n"
                << L"Unchanged payloads: " << numberW(comparison.unchangedPayloads) << L"\r\n"
                << L"Changed payloads: " << numberW(comparison.changedPayloads) << L"\r\n"
                << L"Primary-only records: " << numberW(comparison.primaryOnlyRecords) << L"\r\n"
                << L"Comparison-only records: " << numberW(comparison.compareOnlyRecords) << L"\r\n";
        }
    }

    void showRecordDetails(const NodeData& data, std::wostringstream& out, std::wstring& hex,
                           std::wstring& values) const {
        if (!data.save) return;
        const auto* group = data.save->findGroup(data.key);
        if (!group || data.recordOrdinal >= group->records.size()) return;
        const auto& record = group->records[data.recordOrdinal];
        out << L"RECORD\r\n"
            << L"Role: " << (data.role == Role::Primary ? L"Primary" : L"Comparison") << L"\r\n"
            << L"Stable locator: V" << group->vectorNumber << L":" << utf8ToWide(group->keyHex())
            << L":" << record.index << L"\r\n"
            << L"UnitID: " << numberW(record.index) << L"\r\n"
            << L"UnitID field physically present: " << (record.indexFieldPresent ? L"Yes" : L"No \u2014 default 0") << L"\r\n"
            << L"Vector element index: " << numberW(record.vectorIndex) << L"\r\n"
            << L"Record table offset: " << hexW(record.recordOffset) << L"\r\n";
        if (record.payloadOffset) out << L"Payload offset: " << hexW(*record.payloadOffset) << L"\r\n";
        else out << L"Payload offset: absent\r\n";
        out << L"Value type: " << utf8ToWide(std::string(gdtv::valueTypeName(group->valueType()))) << L"\r\n"
            << L"ValueData elements: " << numberW(record.elementCount) << L"\r\n"
            << L"ValueData bytes: " << numberW(record.payloadByteLength) << L" bytes\r\n";
        appendSectionInfo(out, group->key);

        if (const auto* counterpart = counterpartRecord(data.role, *group, record)) {
            const auto* otherSave = data.role == Role::Primary ? compare_.get() : primary_.get();
            const bool same = otherSave && data.save->payloadEqual(record, *otherSave, *counterpart);
            out << L"\r\nMATCHING RECORD IN OTHER SAVE\r\n"
                << L"ValueData elements: " << numberW(counterpart->elementCount) << L"\r\n"
                << L"ValueData bytes: " << numberW(counterpart->payloadByteLength) << L" bytes\r\n"
                << L"Payload bytes: " << (same ? L"Unchanged" : L"Changed") << L"\r\n";
        } else if (primary_ && compare_) {
            out << L"\r\nMATCHING RECORD IN OTHER SAVE\r\nNot present at this logical index.\r\n";
        }

        const auto payload = data.save->payloadView(record);
        hex = utf8ToWide(gdtv::formatHex(payload, record.payloadOffset.value_or(0)));
        gdtv::HashResolutionFilter filter;
        const gdtv::HashResolutionFilter* filterPointer = nullptr;
        if (const auto* named = sectionNames_.find(group->key); named &&
            (!named->hashCategory.empty() || !named->internalPrefix.empty())) {
            filter.category = named->hashCategory;
            filter.internalPrefix = named->internalPrefix;
            filterPointer = &filter;
        }
        const auto decoded = gdtv::decodeValues(payload, group->valueType(), &hashDatabase_, 4000U,
                                                filterPointer);
        values = utf8ToWide(decoded.text);
        if (group->valueType() == gdtv::ValueType::UInt) {
            out << L"Resolved hash elements in preview: " << numberW(decoded.resolvedHashes) << L"\r\n"
                << L"Unknown nonzero UInt elements in preview: " << numberW(decoded.unresolvedHashes) << L"\r\n";
            if (filterPointer) {
                out << L"Resolved values outside expected section filter: "
                    << numberW(decoded.filterMismatches) << L"\r\n";
            }
        }
    }

    void showClusterDetails(const NodeData& data, std::wostringstream& out) const {
        if (!data.save) return;
        const auto& clusters = data.save->linkedClusters();
        if (data.clusterIndex >= clusters.size()) return;
        const auto& cluster = clusters[data.clusterIndex];
        if (cluster.empty()) return;
        const auto* first = data.save->findGroup(cluster.front());
        out << L"LINKED STRUCTURE\r\n"
            << L"Group: " << numberW(data.clusterIndex + 1U) << L"\r\n"
            << L"Keys: " << numberW(cluster.size()) << L"\r\n";
        if (first) {
            out << L"Records per key: " << numberW(first->records.size()) << L"\r\n"
                << L"Shared index ranges: " << utf8ToWide(first->indexRanges()) << L"\r\n";
        }
        out << L"\r\nMEMBERS\r\n";
        for (const auto key : cluster) {
            const auto* group = data.save->findGroup(key);
            if (!group) continue;
            out << L"V" << group->vectorNumber << L":" << utf8ToWide(group->keyHex());
            const auto displayName = sectionDisplayName(key);
            if (!displayName.empty()) out << L" \u2014 " << displayName;
            out << L"\r\n";
        }
    }

    void showComparisonKeyDetails(std::uint32_t key, std::wostringstream& out) const {
        const auto comparison = gdtv::compareKey(primary_.get(), compare_.get(), key);
        out << L"STABLE KEY COMPARISON\r\n"
            << L"Key: " << utf8ToWide(comparison.primary ? comparison.primary->keyHex() : comparison.compare->keyHex()) << L"\r\n"
            << L"Status: " << utf8ToWide(comparison.status) << L"\r\n"
            << L"Common records: " << numberW(comparison.commonRecords) << L"\r\n"
            << L"Unchanged payloads: " << numberW(comparison.unchangedPayloads) << L"\r\n"
            << L"Changed payloads: " << numberW(comparison.changedPayloads) << L"\r\n"
            << L"Primary-only records: " << numberW(comparison.primaryOnlyRecords) << L"\r\n"
            << L"Comparison-only records: " << numberW(comparison.compareOnlyRecords) << L"\r\n";
        if (comparison.primary) {
            out << L"\r\nPRIMARY\r\nVector: " << comparison.primary->vectorNumber
                << L"\r\nRecords: " << numberW(comparison.primary->records.size())
                << L"\r\nIndices: " << utf8ToWide(comparison.primary->indexRanges()) << L"\r\n";
        }
        if (comparison.compare) {
            out << L"\r\nCOMPARISON\r\nVector: " << comparison.compare->vectorNumber
                << L"\r\nRecords: " << numberW(comparison.compare->records.size())
                << L"\r\nIndices: " << utf8ToWide(comparison.compare->indexRanges()) << L"\r\n";
        }
        appendSectionInfo(out, key);
    }
};

} // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int showCommand) {
    // Available since Windows Vista. This keeps the native UI crisp without
    // requiring a second custom manifest resource.
    SetProcessDPIAware();

    MainWindow window(instance);
    if (!window.create()) return 1;
    window.show(showCommand);
    window.loadStartupArguments();

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (message.message == WM_KEYDOWN && GetKeyState(VK_CONTROL) < 0) {
            if (message.wParam == 'O') {
                SendMessageW(window.hwnd(), WM_COMMAND,
                             GetKeyState(VK_SHIFT) < 0 ? ID_FILE_OPEN_COMPARE : ID_FILE_OPEN_PRIMARY, 0);
                continue;
            }
            if (message.wParam == 'F') {
                SetFocus(GetDlgItem(window.hwnd(), ID_EDIT_SEARCH));
                continue;
            }
        }
        const HWND summonMod = window.summonModWindow();
        if (summonMod && IsWindow(summonMod) && IsDialogMessageW(summonMod, &message)) continue;
        const HWND masteryMod = window.masteryModWindow();
        if (masteryMod && IsWindow(masteryMod) && IsDialogMessageW(masteryMod, &message)) continue;
        if (IsDialogMessageW(window.hwnd(), &message)) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
