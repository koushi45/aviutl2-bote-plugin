#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <mmsystem.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "aviutl2_sdk/plugin2.h"

namespace fs = std::filesystem;

namespace {

constexpr wchar_t kPluginName[] = L"Pawnhub";
constexpr wchar_t kWindowName[] = L"Pawnhub";
constexpr wchar_t kMainClass[] = L"PawnhubMainWindow";
constexpr wchar_t kIconBarClass[] = L"PawnhubIconBar";
constexpr wchar_t kConfigClass[] = L"PawnhubConfigWindow";
constexpr UINT kIconButtonBase = 2000;
constexpr UINT kSearchId = 1001;
constexpr UINT kListId = 1002;
constexpr UINT kStatusId = 1003;
constexpr UINT kThumbScaleId = 1004;
constexpr UINT kThumbScaleLabelId = 1005;
constexpr UINT kPornListId = 3001;
constexpr UINT kPornAddId = 3002;
constexpr UINT kPornDeleteId = 3003;
constexpr UINT kNameEditId = 3010;
constexpr UINT kIconEditId = 3011;
constexpr UINT kIconBrowseId = 3012;
constexpr UINT kVcDirEditId = 3013;
constexpr UINT kVcDirBrowseId = 3014;
constexpr UINT kImgDirEditId = 3015;
constexpr UINT kImgDirBrowseId = 3016;
constexpr UINT kVcLayerEditId = 3017;
constexpr UINT kImgLayerEditId = 3018;
constexpr UINT kAliasListId = 3020;
constexpr UINT kAliasLayerEditId = 3021;
constexpr UINT kAliasBrowseId = 3022;
constexpr UINT kAliasAddId = 3023;
constexpr UINT kAliasDeleteId = 3024;
constexpr UINT kConfigSaveId = 3030;
constexpr UINT kConfigCancelId = 3031;
constexpr int kIconButtonWidth = 92;
constexpr int kIconButtonHeight = 76;

HMENU control_id(UINT id) {
    return reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id));
}

HINSTANCE g_instance{};
EDIT_HANDLE* g_edit_handle{};
bool g_com_initialized{};
HWND g_main{};
HWND g_icon_bar{};
HWND g_search{};
HWND g_list{};
HWND g_status{};
HWND g_thumb_scale{};
HWND g_config{};
HWND g_porn_list{};
HWND g_alias_list{};
int g_config_selected = -1;
bool g_config_loading{};
HIMAGELIST g_image_list{};
int g_icon_scroll{};
bool g_dark_mode{};
HBRUSH g_dark_brush{};
constexpr COLORREF kDarkBackground = RGB(32, 32, 32);
constexpr COLORREF kDarkControl = RGB(45, 45, 48);
constexpr COLORREF kDarkText = RGB(240, 240, 240);

struct AliasEntry {
    int layer = 1;
    std::wstring alias;
};

struct Porn {
    std::wstring name;
    std::wstring icon;
    std::wstring vc_directory;
    std::wstring img_directory;
    int vc_layer = 1;
    int img_layer = 1;
    std::vector<AliasEntry> aliases;
};

enum class ViewMode { None, Voice, Image };
std::vector<Porn> g_porns;
std::vector<Porn> g_config_porns;
std::vector<fs::path> g_visible_files;
std::optional<size_t> g_selected_porn;
ViewMode g_view_mode = ViewMode::None;

std::wstring utf8_to_wide(std::string_view value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                         static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), result.data(), size);
    return result;
}

std::string wide_to_utf8(std::wstring_view value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                         nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        result.data(), size, nullptr, nullptr);
    return result;
}

fs::path config_path() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(g_instance, path, MAX_PATH);
    return fs::path(path).replace_extension(L".json");
}

fs::path legacy_config_path() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(g_instance, path, MAX_PATH);
    return fs::path(path).parent_path() / L"porns_yagi.json";
}

std::string read_binary(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

bool write_binary(const fs::path& path, std::string_view text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    return output.good();
}

struct Json {
    enum class Type { Null, String, Number, Object, Array, Boolean } type = Type::Null;
    std::string string;
    int number{};
    std::map<std::string, Json> object;
    std::vector<Json> array;

    const Json* get(std::string_view key) const {
        auto it = object.find(std::string(key));
        return it == object.end() ? nullptr : &it->second;
    }
};

class JsonParser {
public:
    explicit JsonParser(std::string_view text) : text_(text) {}

    std::optional<Json> parse(std::string& error) {
        skip();
        auto value = parse_value(error);
        skip();
        if (value && pos_ != text_.size()) error = "JSON末尾に不明な文字があります";
        if (!error.empty()) return std::nullopt;
        return value;
    }

private:
    std::string_view text_;
    size_t pos_{};

    void skip() {
        while (pos_ < text_.size() && strchr(" \t\r\n", text_[pos_])) ++pos_;
    }

    bool take(char c) {
        skip();
        if (pos_ < text_.size() && text_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    std::optional<Json> parse_value(std::string& error) {
        skip();
        if (pos_ >= text_.size()) {
            error = "JSONが途中で終了しています";
            return std::nullopt;
        }
        if (text_[pos_] == '"') return parse_string_value(error);
        if (text_[pos_] == '{') return parse_object(error);
        if (text_[pos_] == '[') return parse_array(error);
        if (text_[pos_] == '-' || isdigit(static_cast<unsigned char>(text_[pos_]))) return parse_number(error);
        for (auto [word, type] : {std::pair{"true", Json::Type::Boolean},
                                  {"false", Json::Type::Boolean}, {"null", Json::Type::Null}}) {
            if (text_.substr(pos_, strlen(word)) == word) {
                pos_ += strlen(word);
                Json result;
                result.type = type;
                return result;
            }
        }
        error = "JSONの値を解釈できません";
        return std::nullopt;
    }

    std::optional<std::string> parse_string(std::string& error) {
        if (!take('"')) return std::nullopt;
        std::string out;
        while (pos_ < text_.size()) {
            char c = text_[pos_++];
            if (c == '"') return out;
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (pos_ >= text_.size()) break;
            const char escaped = text_[pos_++];
            switch (escaped) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    if (pos_ + 4 > text_.size()) break;
                    unsigned code{};
                    auto part = text_.substr(pos_, 4);
                    auto result = std::from_chars(part.data(), part.data() + part.size(), code, 16);
                    if (result.ec != std::errc{}) break;
                    pos_ += 4;
                    wchar_t wide[2]{static_cast<wchar_t>(code), 0};
                    out += wide_to_utf8(wide);
                    break;
                }
                default: out.push_back(escaped); break;
            }
        }
        error = "JSON文字列が閉じられていません";
        return std::nullopt;
    }

    std::optional<Json> parse_string_value(std::string& error) {
        auto value = parse_string(error);
        if (!value) return std::nullopt;
        Json result;
        result.type = Json::Type::String;
        result.string = std::move(*value);
        return result;
    }

    std::optional<Json> parse_number(std::string& error) {
        skip();
        const size_t begin = pos_;
        if (text_[pos_] == '-') ++pos_;
        while (pos_ < text_.size() && isdigit(static_cast<unsigned char>(text_[pos_]))) ++pos_;
        int value{};
        auto part = text_.substr(begin, pos_ - begin);
        auto result = std::from_chars(part.data(), part.data() + part.size(), value);
        if (result.ec != std::errc{}) {
            error = "整数を解釈できません";
            return std::nullopt;
        }
        Json json;
        json.type = Json::Type::Number;
        json.number = value;
        return json;
    }

    std::optional<Json> parse_object(std::string& error) {
        take('{');
        Json result;
        result.type = Json::Type::Object;
        skip();
        if (take('}')) return result;
        while (error.empty()) {
            auto key = parse_string(error);
            if (!key || !take(':')) {
                if (error.empty()) error = "JSONオブジェクトの ':' がありません";
                return std::nullopt;
            }
            auto value = parse_value(error);
            if (!value) return std::nullopt;
            result.object.emplace(std::move(*key), std::move(*value));
            if (take('}')) return result;
            if (!take(',')) {
                error = "JSONオブジェクトの ',' がありません";
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    std::optional<Json> parse_array(std::string& error) {
        take('[');
        Json result;
        result.type = Json::Type::Array;
        skip();
        if (take(']')) return result;
        while (error.empty()) {
            auto value = parse_value(error);
            if (!value) return std::nullopt;
            result.array.push_back(std::move(*value));
            if (take(']')) return result;
            if (!take(',')) {
                error = "JSON配列の ',' がありません";
                return std::nullopt;
            }
        }
        return std::nullopt;
    }
};

std::wstring json_string(const Json& object, std::string_view key) {
    const Json* value = object.get(key);
    return value && value->type == Json::Type::String ? utf8_to_wide(value->string) : L"";
}

int json_layer(const Json& object, std::string_view key, int fallback = 1) {
    const Json* value = object.get(key);
    return value && value->type == Json::Type::Number && value->number >= 1 ? value->number : fallback;
}

bool load_config(std::wstring& error) {
    const auto path = config_path();
    if (!fs::exists(path)) {
        const auto legacy = legacy_config_path();
        if (fs::exists(legacy)) {
            write_binary(path, read_binary(legacy));
        } else {
            write_binary(path, "{\n  \"porns\": []\n}\n");
        }
    }
    const std::string text = read_binary(path);
    std::string parse_error;
    auto root = JsonParser(text).parse(parse_error);
    if (!root) {
        error = utf8_to_wide(parse_error);
        return false;
    }
    const Json* list = root->get("porns");
    if (!list || list->type != Json::Type::Array) {
        error = L"ルートに porns 配列がありません";
        return false;
    }
    std::vector<Porn> loaded;
    for (const Json& item : list->array) {
        if (item.type != Json::Type::Object) continue;
        Porn porn;
        porn.name = json_string(item, "name");
        porn.icon = json_string(item, "icon");
        porn.vc_directory = json_string(item, "vc-directory");
        porn.img_directory = json_string(item, "img-directory");
        porn.vc_layer = json_layer(item, "vc-layer");
        porn.img_layer = json_layer(item, "img-layer");
        const Json* aliases = item.get("alias-list");
        if (!aliases) aliases = item.get("alias"); // 初期案との互換性
        if (aliases && aliases->type == Json::Type::Array) {
            for (const Json& alias : aliases->array) {
                if (alias.type != Json::Type::Object) continue;
                porn.aliases.push_back({json_layer(alias, "layer"), json_string(alias, "alias")});
            }
        }
        if (porn.name.empty()) porn.name = L"(名称未設定)";
        loaded.push_back(std::move(porn));
    }
    g_porns = std::move(loaded);
    g_selected_porn.reset();
    g_view_mode = ViewMode::None;
    return true;
}

void set_status(std::wstring_view message) {
    if (g_status) SetWindowTextW(g_status, std::wstring(message).c_str());
}

bool system_uses_dark_mode() {
    DWORD value = 1;
    DWORD size = sizeof(value);
    RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return value == 0;
}

void apply_theme(HWND hwnd) {
    if (!hwnd) return;
    BOOL dark = g_dark_mode;
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
    SetWindowTheme(hwnd, g_dark_mode ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    for (HWND child = GetWindow(hwnd, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        SetWindowTheme(child, g_dark_mode ? L"DarkMode_Explorer" : L"Explorer", nullptr);
        apply_theme(child);
    }
    InvalidateRect(hwnd, nullptr, TRUE);
}

HBITMAP thumbnail_for(const fs::path& path, SIZE size) {
    IShellItem* item{};
    if (FAILED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&item)))) return nullptr;
    IShellItemImageFactory* factory{};
    HBITMAP bitmap{};
    if (SUCCEEDED(item->QueryInterface(IID_PPV_ARGS(&factory)))) {
        factory->GetImage(size, SIIGBF_BIGGERSIZEOK | SIIGBF_THUMBNAILONLY, &bitmap);
        factory->Release();
    }
    item->Release();
    return bitmap;
}

HBITMAP fit_bitmap(HBITMAP source, SIZE target, COLORREF background) {
    if (!source || target.cx < 1 || target.cy < 1) return nullptr;
    BITMAP source_info{};
    GetObjectW(source, sizeof(source_info), &source_info);
    const double scale = std::min(
        static_cast<double>(target.cx) / std::max(1L, source_info.bmWidth),
        static_cast<double>(target.cy) / std::max(1L, source_info.bmHeight));
    const int width = std::max(1, static_cast<int>(source_info.bmWidth * scale));
    const int height = std::max(1, static_cast<int>(source_info.bmHeight * scale));
    const int x = (target.cx - width) / 2;
    const int y = (target.cy - height) / 2;

    HDC screen = GetDC(nullptr);
    HDC source_dc = CreateCompatibleDC(screen);
    HDC target_dc = CreateCompatibleDC(screen);
    HBITMAP result = CreateCompatibleBitmap(screen, target.cx, target.cy);
    HGDIOBJ old_source = SelectObject(source_dc, source);
    HGDIOBJ old_target = SelectObject(target_dc, result);
    HBRUSH brush = CreateSolidBrush(background);
    RECT rect{0, 0, target.cx, target.cy};
    FillRect(target_dc, &rect, brush);
    SetStretchBltMode(target_dc, HALFTONE);
    SetBrushOrgEx(target_dc, 0, 0, nullptr);
    StretchBlt(target_dc, x, y, width, height, source_dc, 0, 0,
               source_info.bmWidth, source_info.bmHeight, SRCCOPY);
    SelectObject(source_dc, old_source);
    SelectObject(target_dc, old_target);
    DeleteObject(brush);
    DeleteDC(source_dc);
    DeleteDC(target_dc);
    ReleaseDC(nullptr, screen);
    return result;
}

int thumbnail_scale() {
    if (!g_thumb_scale) return 100;
    wchar_t text[16]{};
    GetWindowTextW(g_thumb_scale, text, 16);
    return std::clamp(_wtoi(text), 25, 300);
}

void clear_icon_buttons() {
    for (HWND child = GetWindow(g_icon_bar, GW_CHILD); child;) {
        HWND next = GetWindow(child, GW_HWNDNEXT);
        if (auto bitmap = reinterpret_cast<HBITMAP>(SendMessageW(child, BM_GETIMAGE, IMAGE_BITMAP, 0))) {
            DeleteObject(bitmap);
        }
        DestroyWindow(child);
        child = next;
    }
    g_icon_scroll = 0;
}

void rebuild_icons() {
    if (!g_icon_bar) return;
    clear_icon_buttons();
    for (size_t i = 0; i < g_porns.size(); ++i) {
        HBITMAP bitmap = g_porns[i].icon.empty() ? nullptr : thumbnail_for(g_porns[i].icon, {64, 64});
        HWND button = CreateWindowExW(0, WC_BUTTONW, g_porns[i].name.c_str(),
            WS_CHILD | WS_VISIBLE | (bitmap ? BS_BITMAP : BS_PUSHBUTTON | BS_MULTILINE),
            static_cast<int>(i) * kIconButtonWidth, 2, kIconButtonWidth - 4, kIconButtonHeight - 4,
            g_icon_bar, control_id(kIconButtonBase + static_cast<UINT>(i)), g_instance, nullptr);
        if (bitmap) SendMessageW(button, BM_SETIMAGE, IMAGE_BITMAP, reinterpret_cast<LPARAM>(bitmap));
    }
    RECT rect{};
    GetClientRect(g_icon_bar, &rect);
    SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS};
    info.nMin = 0;
    info.nMax = std::max(0, static_cast<int>(g_porns.size()) * kIconButtonWidth - 1);
    info.nPage = rect.right;
    info.nPos = 0;
    SetScrollInfo(g_icon_bar, SB_HORZ, &info, TRUE);
}

bool has_extension(const fs::path& path, const std::vector<std::wstring>& allowed) {
    std::wstring ext = path.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), towlower);
    return std::find(allowed.begin(), allowed.end(), ext) != allowed.end();
}

std::wstring search_text() {
    int size = GetWindowTextLengthW(g_search);
    std::wstring text(size + 1, L'\0');
    GetWindowTextW(g_search, text.data(), size + 1);
    text.resize(size);
    std::transform(text.begin(), text.end(), text.begin(), towlower);
    return text;
}

void rebuild_file_list() {
    ListView_DeleteAllItems(g_list);
    if (g_image_list) {
        ListView_SetImageList(g_list, nullptr, LVSIL_NORMAL);
        ImageList_Destroy(g_image_list);
        g_image_list = nullptr;
    }
    g_visible_files.clear();
    if (!g_selected_porn || g_view_mode == ViewMode::None) return;

    const Porn& porn = g_porns[*g_selected_porn];
    const fs::path directory = g_view_mode == ViewMode::Voice ? porn.vc_directory : porn.img_directory;
    const std::vector<std::wstring> voice{L".mp3", L".wav", L".m4a", L".aac", L".flac", L".ogg", L".wma"};
    const std::vector<std::wstring> image{L".jpg", L".jpeg", L".png", L".bmp", L".gif", L".webp",
                                          L".mp4", L".avi", L".mov", L".mkv", L".wmv", L".webm"};
    const std::wstring query = search_text();
    try {
        for (const auto& entry : fs::directory_iterator(directory)) {
            if (!entry.is_regular_file() || !has_extension(entry.path(), g_view_mode == ViewMode::Voice ? voice : image)) continue;
            std::wstring name = entry.path().filename().wstring();
            std::wstring lower = name;
            std::transform(lower.begin(), lower.end(), lower.begin(), towlower);
            if (!query.empty() && lower.find(query) == std::wstring::npos) continue;
            g_visible_files.push_back(entry.path());
        }
    } catch (...) {
        set_status(L"フォルダを開けません: " + directory.wstring());
        return;
    }
    std::sort(g_visible_files.begin(), g_visible_files.end());

    LONG_PTR style = GetWindowLongPtrW(g_list, GWL_STYLE);
    style &= ~(LVS_TYPEMASK);
    style |= g_view_mode == ViewMode::Voice ? LVS_REPORT : LVS_ICON;
    SetWindowLongPtrW(g_list, GWL_STYLE, style);

    while (ListView_DeleteColumn(g_list, 0)) {}
    if (g_view_mode == ViewMode::Voice) {
        LVCOLUMNW preview{LVCF_TEXT | LVCF_WIDTH};
        preview.pszText = const_cast<wchar_t*>(L"プレビュー");
        preview.cx = 90;
        ListView_InsertColumn(g_list, 0, &preview);
        LVCOLUMNW file{LVCF_TEXT | LVCF_WIDTH};
        file.pszText = const_cast<wchar_t*>(L"ファイル名（クリックで配置）");
        file.cx = 460;
        ListView_InsertColumn(g_list, 1, &file);
    } else {
        const int scale = thumbnail_scale();
        const int thumb_width = std::max(32, 128 * scale / 100);
        const int thumb_height = std::max(24, 96 * scale / 100);
        g_image_list = ImageList_Create(thumb_width, thumb_height, ILC_COLOR32,
                                       static_cast<int>(g_visible_files.size()), 8);
        ImageList_SetBkColor(g_image_list, g_dark_mode ? kDarkControl : GetSysColor(COLOR_WINDOW));
        ListView_SetImageList(g_list, g_image_list, LVSIL_NORMAL);
    }

    for (size_t i = 0; i < g_visible_files.size(); ++i) {
        std::wstring name = g_visible_files[i].filename().wstring();
        LVITEMW item{LVIF_TEXT | LVIF_PARAM};
        item.iItem = static_cast<int>(i);
        item.lParam = static_cast<LPARAM>(i);
        if (g_view_mode == ViewMode::Voice) {
            item.pszText = const_cast<wchar_t*>(L"▶ 再生");
            ListView_InsertItem(g_list, &item);
            ListView_SetItemText(g_list, static_cast<int>(i), 1, name.data());
        } else {
            const int scale = thumbnail_scale();
            const SIZE target{std::max(32, 128 * scale / 100), std::max(24, 96 * scale / 100)};
            HBITMAP source = thumbnail_for(g_visible_files[i], target);
            HBITMAP bitmap = fit_bitmap(source, target, g_dark_mode ? kDarkControl : GetSysColor(COLOR_WINDOW));
            int image_index = bitmap ? ImageList_Add(g_image_list, bitmap, nullptr) : -1;
            if (source) DeleteObject(source);
            if (bitmap) DeleteObject(bitmap);
            item.mask |= LVIF_IMAGE;
            item.iImage = image_index;
            item.pszText = name.data();
            ListView_InsertItem(g_list, &item);
        }
    }
    set_status(std::to_wstring(g_visible_files.size()) + L" 件");
}

void create_media_object(const fs::path& path, int ui_layer, bool still_image, bool replace_matching = false) {
    struct Params {
        fs::path path;
        int layer;
        bool still;
        bool replace_matching;
    } params{path, ui_layer, still_image, replace_matching};
    g_edit_handle->call_edit_section_param(&params, [](void* raw, EDIT_SECTION* edit) {
        auto* p = static_cast<Params*>(raw);
        if (p->replace_matching) {
            OBJECT_HANDLE selected = edit->get_focus_object();
            LPCWSTR effect = p->still ? L"画像ファイル" : L"動画ファイル";
            if (selected && edit->count_object_effect(selected, effect) > 0) {
                const std::string file = wide_to_utf8(p->path.wstring());
                if (edit->set_object_item_value(selected, effect, L"ファイル", file.c_str())) {
                    return;
                }
            }
        }
        int length = 0;
        if (p->still) {
            length = std::max(1, static_cast<int>(5.0 * edit->info->rate / edit->info->scale));
        }
        if (!edit->create_object_from_media_file(p->path.c_str(), p->layer - 1, edit->info->frame, length)) {
            MessageBoxW(g_main, L"メディアを配置できませんでした。配置先の重なりや対応形式を確認してください。",
                        kPluginName, MB_ICONWARNING);
        }
    });
}

void create_aliases(const Porn& porn) {
    struct Params { const Porn* porn; } params{&porn};
    g_edit_handle->call_edit_section_param(&params, [](void* raw, EDIT_SECTION* edit) {
        const Porn& selected = *static_cast<Params*>(raw)->porn;
        int failed = 0;
        for (const AliasEntry& entry : selected.aliases) {
            std::string alias = read_binary(entry.alias);
            if (alias.empty() || !edit->create_object_from_alias(alias.c_str(), entry.layer - 1, edit->info->frame, 0)) ++failed;
        }
        if (failed) {
            MessageBoxW(g_main, (std::to_wstring(failed) + L" 件のaliasを配置できませんでした。").c_str(),
                        kPluginName, MB_ICONWARNING);
        }
    });
}

void play_audio(const fs::path& path) {
    mciSendStringW(L"close pawnhub_preview", nullptr, 0, nullptr);
    std::wstring open = L"open \"" + path.wstring() + L"\" alias pawnhub_preview";
    if (mciSendStringW(open.c_str(), nullptr, 0, nullptr) == 0) {
        mciSendStringW(L"play pawnhub_preview", nullptr, 0, nullptr);
    } else {
        MessageBoxW(g_main, L"この音声ファイルをプレビュー再生できませんでした。", kPluginName, MB_ICONWARNING);
    }
}

std::optional<std::wstring> choose_path(HWND owner, bool folder) {
    IFileDialog* dialog{};
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return std::nullopt;
    if (folder) {
        DWORD options{};
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    } else {
        COMDLG_FILTERSPEC filters[]{{L"画像ファイル", L"*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.webp"},
                                    {L"すべてのファイル", L"*.*"}};
        dialog->SetFileTypes(2, filters);
    }
    std::optional<std::wstring> result;
    if (SUCCEEDED(dialog->Show(owner))) {
        IShellItem* item{};
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path{};
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                result = path;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
    return result;
}

std::vector<std::wstring> choose_alias_paths(HWND owner) {
    IFileOpenDialog* dialog{};
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return {};
    DWORD options{};
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_ALLOWMULTISELECT);
    COMDLG_FILTERSPEC filters[]{{L"すべてのalias", L"*.object"}, {L"すべてのファイル", L"*.*"}};
    dialog->SetFileTypes(2, filters);
    dialog->SetFileTypeIndex(1);
    dialog->SetTitle(L"追加するaliasを選択");

    std::vector<std::wstring> paths;
    if (SUCCEEDED(dialog->Show(owner))) {
        IShellItemArray* items{};
        if (SUCCEEDED(dialog->GetResults(&items))) {
            DWORD count{};
            items->GetCount(&count);
            for (DWORD i = 0; i < count; ++i) {
                IShellItem* item{};
                if (SUCCEEDED(items->GetItemAt(i, &item))) {
                    PWSTR path{};
                    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                        paths.emplace_back(path);
                        CoTaskMemFree(path);
                    }
                    item->Release();
                }
            }
            items->Release();
        }
    }
    dialog->Release();
    return paths;
}

std::wstring control_text(HWND parent, UINT id) {
    HWND control = GetDlgItem(parent, id);
    const int size = GetWindowTextLengthW(control);
    std::wstring text(size + 1, L'\0');
    GetWindowTextW(control, text.data(), size + 1);
    text.resize(size);
    return text;
}

void set_control_text(HWND parent, UINT id, std::wstring_view text) {
    SetWindowTextW(GetDlgItem(parent, id), std::wstring(text).c_str());
}

int control_layer(HWND parent, UINT id) {
    const std::wstring text = control_text(parent, id);
    int value = _wtoi(text.c_str());
    return std::max(1, value);
}

std::string json_escape(std::wstring_view value) {
    std::string utf8 = wide_to_utf8(value);
    std::string out;
    for (unsigned char c : utf8) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(static_cast<char>(c)); break;
        }
    }
    return out;
}

std::string serialize_config(const std::vector<Porn>& porns) {
    std::ostringstream out;
    out << "{\n  \"porns\": [";
    for (size_t i = 0; i < porns.size(); ++i) {
        const Porn& porn = porns[i];
        out << (i ? ",\n" : "\n")
            << "    {\n"
            << "      \"name\": \"" << json_escape(porn.name) << "\",\n"
            << "      \"icon\": \"" << json_escape(porn.icon) << "\",\n"
            << "      \"vc-directory\": \"" << json_escape(porn.vc_directory) << "\",\n"
            << "      \"img-directory\": \"" << json_escape(porn.img_directory) << "\",\n"
            << "      \"vc-layer\": " << porn.vc_layer << ",\n"
            << "      \"img-layer\": " << porn.img_layer << ",\n"
            << "      \"alias-list\": [";
        for (size_t j = 0; j < porn.aliases.size(); ++j) {
            const AliasEntry& alias = porn.aliases[j];
            out << (j ? ",\n" : "\n")
                << "        {\n"
                << "          \"layer\": " << alias.layer << ",\n"
                << "          \"alias\": \"" << json_escape(alias.alias) << "\"\n"
                << "        }";
        }
        if (!porn.aliases.empty()) out << '\n';
        out << "      ]\n"
            << "    }";
    }
    if (!porns.empty()) out << '\n';
    out << "  ]\n}\n";
    return out.str();
}

HWND create_label(HWND parent, UINT id, std::wstring_view text) {
    return CreateWindowExW(0, WC_STATICW, std::wstring(text).c_str(), WS_CHILD | WS_VISIBLE | SS_LEFT,
                           0, 0, 0, 0, parent, control_id(id), g_instance, nullptr);
}

HWND create_edit(HWND parent, UINT id, DWORD extra_style = 0) {
    return CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, nullptr,
                           WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | extra_style,
                           0, 0, 0, 0, parent, control_id(id), g_instance, nullptr);
}

HWND create_button(HWND parent, UINT id, std::wstring_view text) {
    return CreateWindowExW(0, WC_BUTTONW, std::wstring(text).c_str(), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                           0, 0, 0, 0, parent, control_id(id), g_instance, nullptr);
}

void refresh_alias_list() {
    ListView_DeleteAllItems(g_alias_list);
    if (g_config_selected < 0 || static_cast<size_t>(g_config_selected) >= g_config_porns.size()) return;
    const auto& aliases = g_config_porns[g_config_selected].aliases;
    for (size_t i = 0; i < aliases.size(); ++i) {
        std::wstring layer = std::to_wstring(aliases[i].layer);
        LVITEMW item{LVIF_TEXT};
        item.iItem = static_cast<int>(i);
        item.pszText = layer.data();
        ListView_InsertItem(g_alias_list, &item);
        ListView_SetItemText(g_alias_list, static_cast<int>(i), 1,
                             const_cast<wchar_t*>(aliases[i].alias.c_str()));
    }
}

void commit_config_porn(HWND hwnd) {
    if (g_config_loading || g_config_selected < 0 ||
        static_cast<size_t>(g_config_selected) >= g_config_porns.size()) return;
    Porn& porn = g_config_porns[g_config_selected];
    porn.name = control_text(hwnd, kNameEditId);
    porn.icon = control_text(hwnd, kIconEditId);
    porn.vc_directory = control_text(hwnd, kVcDirEditId);
    porn.img_directory = control_text(hwnd, kImgDirEditId);
    porn.vc_layer = control_layer(hwnd, kVcLayerEditId);
    porn.img_layer = control_layer(hwnd, kImgLayerEditId);
    SendMessageW(g_porn_list, LB_DELETESTRING, g_config_selected, 0);
    SendMessageW(g_porn_list, LB_INSERTSTRING, g_config_selected,
                 reinterpret_cast<LPARAM>((porn.name.empty() ? L"(名称未設定)" : porn.name.c_str())));
    SendMessageW(g_porn_list, LB_SETCURSEL, g_config_selected, 0);
}

void load_config_porn(HWND hwnd, int index) {
    g_config_loading = true;
    g_config_selected = index;
    const bool valid = index >= 0 && static_cast<size_t>(index) < g_config_porns.size();
    const Porn empty;
    const Porn& porn = valid ? g_config_porns[index] : empty;
    set_control_text(hwnd, kNameEditId, porn.name);
    set_control_text(hwnd, kIconEditId, porn.icon);
    set_control_text(hwnd, kVcDirEditId, porn.vc_directory);
    set_control_text(hwnd, kImgDirEditId, porn.img_directory);
    set_control_text(hwnd, kVcLayerEditId, valid ? std::to_wstring(porn.vc_layer) : L"");
    set_control_text(hwnd, kImgLayerEditId, valid ? std::to_wstring(porn.img_layer) : L"");
    for (UINT id : {kNameEditId, kIconEditId, kIconBrowseId, kVcDirEditId, kVcDirBrowseId,
                    kImgDirEditId, kImgDirBrowseId, kVcLayerEditId, kImgLayerEditId,
                    kAliasLayerEditId, kAliasBrowseId, kAliasDeleteId}) {
        EnableWindow(GetDlgItem(hwnd, id), valid);
    }
    refresh_alias_list();
    g_config_loading = false;
}

void show_config(HWND owner, HINSTANCE) {
    if (g_config) {
        ShowWindow(g_config, SW_SHOW);
        SetForegroundWindow(g_config);
        return;
    }
    g_config = CreateWindowExW(WS_EX_TOOLWINDOW, kConfigClass, L"Pawnhub 設定",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 980, 680,
        owner, nullptr, g_instance, nullptr);
}

void show_porn_menu(size_t index, HWND button) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"alias設置");
    AppendMenuW(menu, MF_STRING, 2, L"vc表示");
    AppendMenuW(menu, MF_STRING, 3, L"img表示");
    RECT rect{};
    GetWindowRect(button, &rect);
    int selected = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                                  rect.left, rect.bottom, 0, g_main, nullptr);
    DestroyMenu(menu);
    g_selected_porn = index;
    if (selected == 1) create_aliases(g_porns[index]);
    if (selected == 2) {
        g_view_mode = ViewMode::Voice;
        rebuild_file_list();
    }
    if (selected == 3) {
        g_view_mode = ViewMode::Image;
        rebuild_file_list();
    }
}

LRESULT CALLBACK icon_bar_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_HSCROLL) {
        SCROLLINFO info{sizeof(info), SIF_ALL};
        GetScrollInfo(hwnd, SB_HORZ, &info);
        int next = info.nPos;
        switch (LOWORD(wparam)) {
            case SB_LINELEFT: next -= kIconButtonWidth; break;
            case SB_LINERIGHT: next += kIconButtonWidth; break;
            case SB_PAGELEFT: next -= static_cast<int>(info.nPage); break;
            case SB_PAGERIGHT: next += static_cast<int>(info.nPage); break;
            case SB_THUMBTRACK: next = info.nTrackPos; break;
        }
        next = std::clamp(next, info.nMin, std::max(info.nMin, info.nMax - static_cast<int>(info.nPage) + 1));
        ScrollWindowEx(hwnd, g_icon_scroll - next, 0, nullptr, nullptr, nullptr, nullptr, SW_INVALIDATE);
        g_icon_scroll = next;
        info.fMask = SIF_POS;
        info.nPos = next;
        SetScrollInfo(hwnd, SB_HORZ, &info, TRUE);
        return 0;
    }
    if (message == WM_SIZE) rebuild_icons();
    if (message == WM_COMMAND && LOWORD(wparam) >= kIconButtonBase) {
        size_t index = LOWORD(wparam) - kIconButtonBase;
        if (index < g_porns.size()) show_porn_menu(index, reinterpret_cast<HWND>(lparam));
        return 0;
    }
    if (message == WM_CTLCOLORBTN && g_dark_mode) {
        HDC dc = reinterpret_cast<HDC>(wparam);
        SetTextColor(dc, kDarkText);
        SetBkColor(dc, kDarkControl);
        return reinterpret_cast<LRESULT>(g_dark_brush);
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT CALLBACK main_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_CREATE:
            g_icon_bar = CreateWindowExW(0, kIconBarClass, nullptr, WS_CHILD | WS_VISIBLE | WS_HSCROLL,
                                         0, 0, 0, 0, hwnd, nullptr, g_instance, nullptr);
            g_search = CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, nullptr,
                                      WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                      0, 0, 0, 0, hwnd, control_id(kSearchId), g_instance, nullptr);
            SendMessageW(g_search, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"ファイル名を検索"));
            g_thumb_scale = CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, L"100",
                                      WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER,
                                      0, 0, 0, 0, hwnd, control_id(kThumbScaleId), g_instance, nullptr);
            SendMessageW(g_thumb_scale, EM_SETLIMITTEXT, 3, 0);
            CreateWindowExW(0, WC_STATICW, L"サムネイル %", WS_CHILD | WS_VISIBLE | SS_LEFT,
                            0, 0, 0, 0, hwnd, control_id(kThumbScaleLabelId), g_instance, nullptr);
            g_list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, nullptr,
                                    WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL,
                                    0, 0, 0, 0, hwnd, control_id(kListId), g_instance, nullptr);
            ListView_SetExtendedListViewStyle(g_list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
            if (g_dark_mode) {
                ListView_SetBkColor(g_list, kDarkBackground);
                ListView_SetTextBkColor(g_list, kDarkBackground);
                ListView_SetTextColor(g_list, kDarkText);
            }
            g_status = CreateWindowExW(0, WC_STATICW, L"アイコンをクリックしてください",
                                      WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd,
                                      control_id(kStatusId), g_instance, nullptr);
            rebuild_icons();
            apply_theme(hwnd);
            return 0;
        case WM_SIZE: {
            int width = LOWORD(lparam), height = HIWORD(lparam);
            MoveWindow(g_icon_bar, 0, 0, width, kIconButtonHeight + GetSystemMetrics(SM_CYHSCROLL), TRUE);
            MoveWindow(g_search, 8, kIconButtonHeight + 22, std::max(0, width - 184), 26, TRUE);
            MoveWindow(GetDlgItem(hwnd, kThumbScaleLabelId), std::max(8, width - 168),
                       kIconButtonHeight + 27, 100, 20, TRUE);
            MoveWindow(g_thumb_scale, std::max(8, width - 66), kIconButtonHeight + 22, 58, 26, TRUE);
            MoveWindow(g_list, 8, kIconButtonHeight + 54, std::max(0, width - 16),
                       std::max(0, height - kIconButtonHeight - 82), TRUE);
            MoveWindow(g_status, 8, std::max(0, height - 24), std::max(0, width - 16), 20, TRUE);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wparam) == kSearchId && HIWORD(wparam) == EN_CHANGE) rebuild_file_list();
            if (LOWORD(wparam) == kThumbScaleId && HIWORD(wparam) == EN_CHANGE) {
                wchar_t text[16]{};
                GetWindowTextW(g_thumb_scale, text, 16);
                const int value = _wtoi(text);
                if (value >= 25 && value <= 300 && g_view_mode == ViewMode::Image) rebuild_file_list();
            }
            if (LOWORD(wparam) == kThumbScaleId && HIWORD(wparam) == EN_KILLFOCUS) {
                SetWindowTextW(g_thumb_scale, std::to_wstring(thumbnail_scale()).c_str());
            }
            return 0;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORBTN:
            if (g_dark_mode) {
                HDC dc = reinterpret_cast<HDC>(wparam);
                SetTextColor(dc, kDarkText);
                SetBkColor(dc, kDarkControl);
                return reinterpret_cast<LRESULT>(g_dark_brush);
            }
            break;
        case WM_NOTIFY: {
            auto* header = reinterpret_cast<NMHDR*>(lparam);
            if (header->idFrom != kListId || header->code != NM_CLICK) break;
            auto* activate = reinterpret_cast<NMITEMACTIVATE*>(lparam);
            if (activate->iItem < 0 || static_cast<size_t>(activate->iItem) >= g_visible_files.size() || !g_selected_porn) break;
            const fs::path& path = g_visible_files[activate->iItem];
            const Porn& porn = g_porns[*g_selected_porn];
            if (g_view_mode == ViewMode::Voice) {
                if (activate->iSubItem == 0) play_audio(path);
                else create_media_object(path, porn.vc_layer, false);
            } else {
                const std::vector<std::wstring> still{L".jpg", L".jpeg", L".png", L".bmp", L".gif", L".webp"};
                create_media_object(path, porn.img_layer, has_extension(path, still), true);
            }
            return 0;
        }
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT CALLBACK config_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_CREATE: {
            g_config_porns = g_porns;
            g_porn_list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTBOXW, nullptr,
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                0, 0, 0, 0, hwnd, control_id(kPornListId), g_instance, nullptr);
            create_button(hwnd, kPornAddId, L"ポーン追加");
            create_button(hwnd, kPornDeleteId, L"削除");
            create_label(hwnd, 3100, L"名前");
            create_edit(hwnd, kNameEditId);
            create_label(hwnd, 3101, L"アイコン画像");
            create_edit(hwnd, kIconEditId);
            create_button(hwnd, kIconBrowseId, L"選択...");
            create_label(hwnd, 3102, L"VCフォルダ");
            create_edit(hwnd, kVcDirEditId);
            create_button(hwnd, kVcDirBrowseId, L"選択...");
            create_label(hwnd, 3103, L"画像・動画フォルダ");
            create_edit(hwnd, kImgDirEditId);
            create_button(hwnd, kImgDirBrowseId, L"選択...");
            create_label(hwnd, 3104, L"VC配置レイヤー");
            create_edit(hwnd, kVcLayerEditId, ES_NUMBER);
            create_label(hwnd, 3105, L"画像・動画配置レイヤー");
            create_edit(hwnd, kImgLayerEditId, ES_NUMBER);
            create_label(hwnd, 3106, L"alias一覧");
            g_alias_list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, nullptr,
                WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                0, 0, 0, 0, hwnd, control_id(kAliasListId), g_instance, nullptr);
            ListView_SetExtendedListViewStyle(g_alias_list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
            if (g_dark_mode) {
                ListView_SetBkColor(g_alias_list, kDarkBackground);
                ListView_SetTextBkColor(g_alias_list, kDarkBackground);
                ListView_SetTextColor(g_alias_list, kDarkText);
            }
            LVCOLUMNW layer_column{LVCF_TEXT | LVCF_WIDTH};
            layer_column.pszText = const_cast<wchar_t*>(L"レイヤー");
            layer_column.cx = 80;
            ListView_InsertColumn(g_alias_list, 0, &layer_column);
            LVCOLUMNW alias_column{LVCF_TEXT | LVCF_WIDTH};
            alias_column.pszText = const_cast<wchar_t*>(L"aliasファイル");
            alias_column.cx = 500;
            ListView_InsertColumn(g_alias_list, 1, &alias_column);
            create_label(hwnd, 3107, L"配置レイヤー");
            create_edit(hwnd, kAliasLayerEditId, ES_NUMBER);
            set_control_text(hwnd, kAliasLayerEditId, L"1");
            create_button(hwnd, kAliasBrowseId, L"aliasを選択して追加...");
            create_button(hwnd, kAliasDeleteId, L"選択aliasを削除");
            create_button(hwnd, kConfigSaveId, L"保存して反映");
            create_button(hwnd, kConfigCancelId, L"キャンセル");
            for (const Porn& porn : g_config_porns) {
                SendMessageW(g_porn_list, LB_ADDSTRING, 0,
                    reinterpret_cast<LPARAM>((porn.name.empty() ? L"(名称未設定)" : porn.name.c_str())));
            }
            if (!g_config_porns.empty()) {
                SendMessageW(g_porn_list, LB_SETCURSEL, 0, 0);
                load_config_porn(hwnd, 0);
            } else {
                load_config_porn(hwnd, -1);
            }
            apply_theme(hwnd);
            return 0;
        }
        case WM_SIZE: {
            int width = LOWORD(lparam), height = HIWORD(lparam);
            const int margin = 12;
            const int left = 210;
            const int right_x = left + margin * 2;
            const int right_width = std::max(320, width - right_x - margin);
            const int edit_x = right_x + 145;
            const int browse_width = 76;
            const int edit_width = std::max(120, right_width - 145 - browse_width - 8);
            MoveWindow(g_porn_list, margin, margin, left, std::max(100, height - 92), TRUE);
            MoveWindow(GetDlgItem(hwnd, kPornAddId), margin, height - 68, 110, 28, TRUE);
            MoveWindow(GetDlgItem(hwnd, kPornDeleteId), margin + 118, height - 68, 92, 28, TRUE);
            int y = margin;
            for (auto [label, edit, browse] : {
                    std::tuple<UINT, UINT, UINT>{3100, kNameEditId, 0},
                    {3101, kIconEditId, kIconBrowseId},
                    {3102, kVcDirEditId, kVcDirBrowseId},
                    {3103, kImgDirEditId, kImgDirBrowseId}}) {
                MoveWindow(GetDlgItem(hwnd, label), right_x, y + 5, 140, 24, TRUE);
                MoveWindow(GetDlgItem(hwnd, edit), edit_x, y, edit_width + (browse ? 0 : browse_width + 8), 26, TRUE);
                if (browse) MoveWindow(GetDlgItem(hwnd, browse), edit_x + edit_width + 8, y, browse_width, 26, TRUE);
                y += 34;
            }
            MoveWindow(GetDlgItem(hwnd, 3104), right_x, y + 5, 140, 24, TRUE);
            MoveWindow(GetDlgItem(hwnd, kVcLayerEditId), edit_x, y, 90, 26, TRUE);
            MoveWindow(GetDlgItem(hwnd, 3105), edit_x + 110, y + 5, 170, 24, TRUE);
            MoveWindow(GetDlgItem(hwnd, kImgLayerEditId), edit_x + 285, y, 90, 26, TRUE);
            y += 42;
            MoveWindow(GetDlgItem(hwnd, 3106), right_x, y, 140, 24, TRUE);
            y += 24;
            const int alias_bottom = std::max(y + 100, height - 110);
            MoveWindow(g_alias_list, right_x, y, right_width, alias_bottom - y, TRUE);
            y = alias_bottom + 8;
            MoveWindow(GetDlgItem(hwnd, 3107), right_x, y + 5, 100, 24, TRUE);
            MoveWindow(GetDlgItem(hwnd, kAliasLayerEditId), right_x + 104, y, 70, 26, TRUE);
            MoveWindow(GetDlgItem(hwnd, kAliasBrowseId), right_x + 184, y, 180, 28, TRUE);
            MoveWindow(GetDlgItem(hwnd, kAliasDeleteId), right_x + 374, y, 150, 28, TRUE);
            MoveWindow(GetDlgItem(hwnd, kConfigSaveId), width - 274, height - 42, 126, 30, TRUE);
            MoveWindow(GetDlgItem(hwnd, kConfigCancelId), width - 140, height - 42, 126, 30, TRUE);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wparam) == kPornListId && HIWORD(wparam) == LBN_SELCHANGE) {
                int selected = static_cast<int>(SendMessageW(g_porn_list, LB_GETCURSEL, 0, 0));
                commit_config_porn(hwnd);
                SendMessageW(g_porn_list, LB_SETCURSEL, selected, 0);
                load_config_porn(hwnd, selected);
                return 0;
            }
            if (LOWORD(wparam) == kPornAddId) {
                commit_config_porn(hwnd);
                Porn porn;
                porn.name = L"新しいポーン";
                g_config_porns.push_back(porn);
                int index = static_cast<int>(g_config_porns.size() - 1);
                SendMessageW(g_porn_list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(porn.name.c_str()));
                SendMessageW(g_porn_list, LB_SETCURSEL, index, 0);
                load_config_porn(hwnd, index);
                SetFocus(GetDlgItem(hwnd, kNameEditId));
                return 0;
            }
            if (LOWORD(wparam) == kPornDeleteId && g_config_selected >= 0) {
                if (MessageBoxW(hwnd, L"選択中のポーンを削除しますか？", kPluginName,
                                MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
                g_config_porns.erase(g_config_porns.begin() + g_config_selected);
                SendMessageW(g_porn_list, LB_DELETESTRING, g_config_selected, 0);
                int next = std::min(g_config_selected, static_cast<int>(g_config_porns.size()) - 1);
                if (next >= 0) SendMessageW(g_porn_list, LB_SETCURSEL, next, 0);
                load_config_porn(hwnd, next);
                return 0;
            }
            if (LOWORD(wparam) == kIconBrowseId) {
                if (auto path = choose_path(hwnd, false)) set_control_text(hwnd, kIconEditId, *path);
                return 0;
            }
            if (LOWORD(wparam) == kVcDirBrowseId || LOWORD(wparam) == kImgDirBrowseId) {
                if (auto path = choose_path(hwnd, true)) {
                    set_control_text(hwnd, LOWORD(wparam) == kVcDirBrowseId ? kVcDirEditId : kImgDirEditId, *path);
                }
                return 0;
            }
            if (LOWORD(wparam) == kAliasBrowseId && g_config_selected >= 0) {
                const int layer = control_layer(hwnd, kAliasLayerEditId);
                const auto paths = choose_alias_paths(hwnd);
                for (const auto& path : paths) {
                    g_config_porns[g_config_selected].aliases.push_back({layer, path});
                }
                if (!paths.empty()) {
                    refresh_alias_list();
                }
                return 0;
            }
            if (LOWORD(wparam) == kAliasDeleteId && g_config_selected >= 0) {
                int selected = ListView_GetNextItem(g_alias_list, -1, LVNI_SELECTED);
                if (selected >= 0) {
                    auto& aliases = g_config_porns[g_config_selected].aliases;
                    aliases.erase(aliases.begin() + selected);
                    refresh_alias_list();
                }
                return 0;
            }
            if (LOWORD(wparam) == kConfigSaveId) {
                commit_config_porn(hwnd);
                if (!write_binary(config_path(), serialize_config(g_config_porns))) {
                    MessageBoxW(hwnd, L"設定ファイルを保存できませんでした。", kPluginName, MB_ICONERROR);
                    return 0;
                }
                std::wstring error;
                if (!load_config(error)) {
                    MessageBoxW(hwnd, error.c_str(), L"JSONエラー", MB_ICONERROR);
                    return 0;
                }
                rebuild_icons();
                rebuild_file_list();
                DestroyWindow(hwnd);
                return 0;
            }
            if (LOWORD(wparam) == kConfigCancelId) {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLORBTN:
            if (g_dark_mode) {
                HDC dc = reinterpret_cast<HDC>(wparam);
                SetTextColor(dc, kDarkText);
                SetBkColor(dc, kDarkControl);
                return reinterpret_cast<LRESULT>(g_dark_brush);
            }
            break;
        case WM_DESTROY:
            g_config = nullptr;
            g_porn_list = nullptr;
            g_alias_list = nullptr;
            g_config_porns.clear();
            g_config_selected = -1;
            return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

void register_class(const wchar_t* name, WNDPROC proc, HBRUSH brush = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1)) {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpszClassName = name;
    wc.lpfnWndProc = proc;
    wc.hInstance = g_instance;
    wc.hbrBackground = brush;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);
}

COMMON_PLUGIN_TABLE g_plugin_table{
    kPluginName,
    L"Pawnhub version 0.1.0"
};

} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_instance = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}

EXTERN_C __declspec(dllexport) DWORD RequiredVersion() {
    return 2003300;
}

EXTERN_C __declspec(dllexport) COMMON_PLUGIN_TABLE* GetCommonPluginTable() {
    return &g_plugin_table;
}

EXTERN_C __declspec(dllexport) bool InitializePlugin(DWORD) {
    const HRESULT com_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    g_com_initialized = SUCCEEDED(com_result);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES | ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&controls);
    g_dark_mode = system_uses_dark_mode();
    g_dark_brush = CreateSolidBrush(kDarkControl);
    return true;
}

EXTERN_C __declspec(dllexport) void UninitializePlugin() {
    mciSendStringW(L"close pawnhub_preview", nullptr, 0, nullptr);
    if (g_dark_brush) DeleteObject(g_dark_brush);
    if (g_com_initialized) CoUninitialize();
}

EXTERN_C __declspec(dllexport) void RegisterPlugin(HOST_APP_TABLE* host) {
    register_class(kMainClass, main_proc,
                   g_dark_mode ? g_dark_brush : reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
    register_class(kIconBarClass, icon_bar_proc,
                   g_dark_mode ? g_dark_brush : reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1));
    register_class(kConfigClass, config_proc,
                   g_dark_mode ? g_dark_brush : reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));

    g_main = CreateWindowExW(0, kMainClass, kWindowName, WS_POPUP,
                             CW_USEDEFAULT, CW_USEDEFAULT, 600, 500,
                             nullptr, nullptr, g_instance, nullptr);
    if (!g_main) return;

    std::wstring error;
    if (!load_config(error)) MessageBoxW(nullptr, error.c_str(), L"Pawnhub JSONエラー", MB_ICONERROR);
    rebuild_icons();
    host->register_config_menu(L"Pawnhub", show_config);
    host->register_window_client(kWindowName, g_main);
    g_edit_handle = host->create_edit_handle();
}
