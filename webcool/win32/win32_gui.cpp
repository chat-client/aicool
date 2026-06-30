#include "stdafx.h"
#include "../action/actions.h"
#include "../action/action_util.h"
#include "../platform_compat.h"
#include "../config.h"
#include "server_thread.h"
#include "webcool_controller.h"
#include "resource.h"
#include "win32_gui.h"

#ifdef _WIN32
#include <commctrl.h>
#include <commdlg.h>
#include <objbase.h>
#include <shlobj.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(linker, \
	"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' " \
	"version='6.0.0.0' processorArchitecture='*' " \
	"publicKeyToken='6595b64144ccf1df' language='*'\"")

enum {
	IDC_STATUS_TEXT = 1001,
	IDC_START_BTN = 1002,
	IDC_STOP_BTN = 1003,
	IDC_MINIMIZE_BTN = 1004,
	IDC_EXIT_BTN = 1005,
	IDC_BROWSER_BTN = 1006,
	IDC_ADDR_EDIT = 1010,
	IDC_UPLOAD_EDIT = 1011,
	IDC_HTML_EDIT = 1012,
	IDC_SQLITE_EDIT = 1013,
	IDC_FFMPEG_EDIT = 1014,
	IDC_THREADS_EDIT = 1015,
	IDC_LANG_COMBO = 1016,
	IDC_UPLOAD_BROWSE = 1021,
	IDC_HTML_BROWSE = 1022,
	IDC_SQLITE_BROWSE = 1023,
	IDC_FFMPEG_BROWSE = 1024,
	IDC_TITLE_TEXT = 1030,
	IDC_GROUP_TEXT = 1031,
	IDC_STATUS_FRAME = 1032,
	IDC_ADDR_LABEL = 1040,
	IDC_UPLOAD_LABEL = 1041,
	IDC_HTML_LABEL = 1042,
	IDC_SQLITE_LABEL = 1043,
	IDC_FFMPEG_LABEL = 1044,
	IDC_THREADS_LABEL = 1045,
	IDC_LANG_LABEL = 1046,
	IDC_ADVANCED_TOGGLE_BTN = 1047,
	IDM_TRAY_OPEN = 1101,
	IDM_TRAY_EXIT = 1102
};

static const UINT WM_WEBCOOL_TRAY = WM_APP + 88;
static const UINT WEBCOOL_TRAY_ID = 1;
static const UINT_PTR WEBCOOL_SYNC_TIMER_ID = 2;
static const int kWinWidth = 720;
static const int kLayoutGroupTop = 48;
static const int kLayoutGroupHCollapsed = 68;
static const int kLayoutGroupHExpanded = 210;
static const int kLayoutFormY0 = 72;
static const int kLayoutFormEditX = 188;
static const int kLayoutAddrEditWidth = 256;
static const int kLayoutToggleX = 452;
static const int kLayoutToggleWidth = 196;
static const int kLayoutAdvancedY0 = 102;
static const int kLayoutFormRowH = 30;
static const int kLayoutStatusH = 96;
static const int kLayoutBtnH = 30;
static const int kLayoutSectionGap = 12;
static const int kLayoutFooterGap = 16;
static const int kLayoutBottomMargin = 28;
static const COLORREF kColorBg = RGB(243, 244, 246);
static const COLORREF kColorPanel = RGB(255, 255, 255);
static const COLORREF kColorText = RGB(31, 41, 55);
static const COLORREF kColorTextMuted = RGB(107, 114, 128);
static const COLORREF kColorRunning = RGB(22, 163, 74);
static const COLORREF kColorStopped = RGB(156, 163, 175);
static HBRUSH g_control_bg_brush = NULL;
static HBRUSH g_control_panel_brush = NULL;
static HBRUSH g_edit_bg_brush = NULL;
static HFONT g_control_font = NULL;
static HFONT g_control_title_font = NULL;
static bool g_service_running = false;
static bool g_config_advanced_expanded = false;
static std::wstring g_control_config_path;
static const wchar_t* k_control_window_class = L"WebCoolControlWindow";
static const wchar_t* k_control_single_instance_mutex =
	L"Local\\AicoolWebcoolControlWindow";
enum gui_language_t {
	GUI_LANG_ZH = 0,
	GUI_LANG_EN = 1
};
static gui_language_t g_gui_language = GUI_LANG_ZH;

enum ui_text_t {
	UI_TITLE,
	UI_STATUS_RUNNING,
	UI_STATUS_STOPPED,
	UI_LISTEN_ADDR,
	UI_UPLOAD_DIR,
	UI_HTML_HOME,
	UI_SQLITE_LIB,
	UI_FFMPEG_EXE,
	UI_THREADS,
	UI_LANGUAGE,
	UI_SERVICE_CONFIG,
	UI_ADVANCED_EXPAND,
	UI_ADVANCED_COLLAPSE,
	UI_START,
	UI_STOP,
	UI_MINIMIZE,
	UI_EXIT,
	UI_OPEN_BROWSER,
	UI_INVALID_CONFIG,
	UI_START_FAILED,
	UI_SELECT_UPLOAD,
	UI_SELECT_HTML,
	UI_SELECT_SQLITE,
	UI_SELECT_FFMPEG,
	UI_SQLITE_FILTER,
	UI_FFMPEG_FILTER,
	UI_TRAY_OPEN,
	UI_CLOSE_WHILE_RUNNING
};

static const wchar_t* tr(ui_text_t id) {
	const bool en = g_gui_language == GUI_LANG_EN;
	switch (id) {
	case UI_TITLE: return en ? L"webcool Control Panel" : L"webcool 控制界面";
	case UI_STATUS_RUNNING: return en ? L"Status: Running" : L"状态：运行中";
	case UI_STATUS_STOPPED: return en ? L"Status: Stopped" : L"状态：已停止";
	case UI_LISTEN_ADDR: return en ? L"Listen address" : L"监听地址";
	case UI_UPLOAD_DIR: return en ? L"File save directory" : L"文件保存目录";
	case UI_HTML_HOME: return en ? L"Static resource root" : L"静态资源根目录";
	case UI_SQLITE_LIB: return en ? L"sqlite library path" : L"sqlite动态库路径";
	case UI_FFMPEG_EXE: return en ? L"ffmpeg executable path" : L"ffmpeg可执行文件路径";
	case UI_THREADS: return en ? L"Worker threads" : L"工作线程数";
	case UI_LANGUAGE: return en ? L"Language" : L"界面语言";
	case UI_SERVICE_CONFIG: return en ? L"Service configuration" : L"服务配置";
	case UI_ADVANCED_EXPAND: return en ? L"\x25B6  Advanced settings" : L"\x25B6  高级设置";
	case UI_ADVANCED_COLLAPSE: return en ? L"\x25BC  Hide advanced settings" : L"\x25BC  收起高级设置";
	case UI_START: return en ? L"Start" : L"启动";
	case UI_STOP: return en ? L"Stop" : L"停止";
	case UI_MINIMIZE: return en ? L"Minimize" : L"最小化";
	case UI_EXIT: return en ? L"Exit" : L"退出";
	case UI_OPEN_BROWSER: return en ? L"Open Browser" : L"打开浏览器";
	case UI_INVALID_CONFIG: return en ? L"Invalid configuration: " : L"配置无效：";
	case UI_START_FAILED: return en ? L"Failed to start webcool: " : L"启动 webcool 失败：";
	case UI_SELECT_UPLOAD: return en ? L"Select file save directory" : L"选择文件保存目录";
	case UI_SELECT_HTML: return en ? L"Select static resource root" : L"选择静态资源根目录";
	case UI_SELECT_SQLITE: return en ? L"Select sqlite library" : L"选择 sqlite 动态库";
	case UI_SELECT_FFMPEG: return en ? L"Select ffmpeg executable" : L"选择 ffmpeg 可执行文件";
	case UI_SQLITE_FILTER: return en ? L"sqlite library\0*.dll\0All files\0*.*\0" : L"sqlite 动态库\0*.dll\0所有文件\0*.*\0";
	case UI_FFMPEG_FILTER: return en ? L"ffmpeg executable\0*.exe\0All files\0*.*\0" : L"ffmpeg 可执行文件\0*.exe\0所有文件\0*.*\0";
	case UI_TRAY_OPEN: return en ? L"Open" : L"打开";
	case UI_CLOSE_WHILE_RUNNING:
		return en
			? L"The service is running.\nMinimize to the system tray?"
			: L"当前服务正在运行，是否最小化到系统托盘？";
	default: return L"";
	}
}

static std::wstring utf8_to_wide_text(const char* text) {
	std::wstring wide;
	if (text == nullptr || !webcool_utf8_to_wide(text, wide)) {
		return L"";
	}
	return wide;
}

static std::string wide_to_utf8_text(const wchar_t* text) {
	std::string out;
	if (text == nullptr || !webcool_wide_to_utf8(text, out)) {
		return "";
	}
	return out;
}

static std::string get_window_utf8(HWND hwnd, int id) {
	HWND item = GetDlgItem(hwnd, id);
	if (item == NULL) {
		return "";
	}
	const int len = GetWindowTextLengthW(item);
	std::vector<wchar_t> buf((size_t) len + 1, L'\0');
	GetWindowTextW(item, &buf[0], len + 1);
	return wide_to_utf8_text(&buf[0]);
}

static void set_window_utf8(HWND hwnd, int id, const char* text) {
	SetWindowTextW(GetDlgItem(hwnd, id), utf8_to_wide_text(text).c_str());
}

static void set_control_font(HWND hwnd, int id, HFONT font) {
	SendMessageW(GetDlgItem(hwnd, id), WM_SETFONT, (WPARAM) font, TRUE);
}

static void ensure_control_theme_gdi() {
	if (g_control_bg_brush == NULL) {
		g_control_bg_brush = CreateSolidBrush(kColorBg);
	}
	if (g_control_panel_brush == NULL) {
		g_control_panel_brush = CreateSolidBrush(kColorPanel);
	}
	if (g_edit_bg_brush == NULL) {
		g_edit_bg_brush = CreateSolidBrush(RGB(255, 255, 255));
	}
	if (g_control_font == NULL) {
		g_control_font = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
			DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");
	}
	if (g_control_title_font == NULL) {
		g_control_title_font = CreateFontW(-18, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
			DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");
	}
}

static std::wstring app_config_path() {
	if (!g_control_config_path.empty()) {
		return g_control_config_path;
	}
	wchar_t path[32768];
	memset(path, 0, sizeof(path));
	GetModuleFileNameW(NULL, path, (DWORD) (sizeof(path) / sizeof(path[0])));
	std::wstring dir(path);
	const std::wstring::size_type pos = dir.find_last_of(L"\\/");
	if (pos != std::wstring::npos) {
		dir.resize(pos);
	}
	if (!dir.empty() && dir[dir.size() - 1] != L'\\' && dir[dir.size() - 1] != L'/') {
		dir += L"\\";
	}
	g_control_config_path = dir + L"webcool-control.ini";
	return g_control_config_path;
}

static std::wstring read_profile_text(const wchar_t* section,
	  const wchar_t* key, const wchar_t* fallback) {
	wchar_t buf[32768];
	memset(buf, 0, sizeof(buf));
	GetPrivateProfileStringW(section, key, fallback ? fallback : L"",
		buf, (DWORD)(sizeof(buf) / sizeof(buf[0])), app_config_path().c_str());
	return std::wstring(buf);
}

static int read_profile_int(const wchar_t* section,
	  const wchar_t* key, int fallback) {
	return (int)GetPrivateProfileIntW(section, key, fallback,
		app_config_path().c_str());
}

static void write_profile_text(const wchar_t* section,
	  const wchar_t* key, const std::wstring& value) {
	WritePrivateProfileStringW(section, key, value.c_str(),
		app_config_path().c_str());
}

static void write_profile_int(const wchar_t* section,
	  const wchar_t* key, int value) {
	wchar_t buf[32];
	swprintf(buf, sizeof(buf) / sizeof(buf[0]), L"%d", value);
	write_profile_text(section, key, buf);
}

static void set_config_text_from_wide(char* dst, size_t dst_size,
	  const std::wstring& value) {
	std::string utf8;
	std::string err;
	if (webcool_wide_to_utf8(value.c_str(), utf8)) {
		set_config_text(dst, dst_size, utf8, "control config", err);
	}
}

static void load_control_config(webcool_controller& controller) {
	const std::wstring lang = read_profile_text(L"ui", L"language", L"zh");
	g_gui_language = _wcsicmp(lang.c_str(), L"en") == 0
		? GUI_LANG_EN : GUI_LANG_ZH;
	g_config_advanced_expanded =
		read_profile_int(L"ui", L"advanced_expanded", 0) != 0;

	webcool_options options = controller.options();
	const std::wstring addr = read_profile_text(L"service", L"addr",
		utf8_to_wide_text(options.addr.c_str()).c_str());
	std::string addr_utf8;
	if (webcool_wide_to_utf8(addr.c_str(), addr_utf8) && !addr_utf8.empty()) {
		options.addr = addr_utf8.c_str();
	}
	options.nthreads = read_profile_int(L"service", L"threads",
		options.nthreads > 0 ? options.nthreads : 2);
	options.reuse_port = read_profile_int(L"service", L"reuse_port",
		options.reuse_port ? 1 : 0) != 0;
	controller.configure(options);

	set_config_text_from_wide(g_upload_dir, sizeof(g_upload_dir),
		read_profile_text(L"paths", L"upload_dir", utf8_to_wide_text(g_upload_dir).c_str()));
	set_config_text_from_wide(g_html_home, sizeof(g_html_home),
		read_profile_text(L"paths", L"html_home", utf8_to_wide_text(g_html_home).c_str()));
	set_config_text_from_wide(g_sqlite_lib, sizeof(g_sqlite_lib),
		read_profile_text(L"paths", L"sqlite_lib", utf8_to_wide_text(g_sqlite_lib).c_str()));
	set_config_text_from_wide(g_ffmpeg_path, sizeof(g_ffmpeg_path),
		read_profile_text(L"paths", L"ffmpeg_path", utf8_to_wide_text(g_ffmpeg_path).c_str()));
}

static void save_control_config(HWND hwnd) {
	webcool_controller* controller =
		(webcool_controller*) GetWindowLongPtrW(hwnd, GWLP_USERDATA);
	write_profile_text(L"ui", L"language",
		g_gui_language == GUI_LANG_EN ? L"en" : L"zh");
	write_profile_int(L"ui", L"advanced_expanded",
		g_config_advanced_expanded ? 1 : 0);
	write_profile_text(L"service", L"addr",
		utf8_to_wide_text(get_window_utf8(hwnd, IDC_ADDR_EDIT).c_str()));
	write_profile_int(L"service", L"threads",
		atoi(get_window_utf8(hwnd, IDC_THREADS_EDIT).c_str()));
	if (controller != nullptr) {
		write_profile_int(L"service", L"reuse_port",
			controller->options().reuse_port ? 1 : 0);
	}
	write_profile_text(L"paths", L"upload_dir",
		utf8_to_wide_text(get_window_utf8(hwnd, IDC_UPLOAD_EDIT).c_str()));
	write_profile_text(L"paths", L"html_home",
		utf8_to_wide_text(get_window_utf8(hwnd, IDC_HTML_EDIT).c_str()));
	write_profile_text(L"paths", L"sqlite_lib",
		utf8_to_wide_text(get_window_utf8(hwnd, IDC_SQLITE_EDIT).c_str()));
	write_profile_text(L"paths", L"ffmpeg_path",
		utf8_to_wide_text(get_window_utf8(hwnd, IDC_FFMPEG_EDIT).c_str()));
}

static void update_control_window2(HWND hwnd);

static void sync_upload_dir_from_runtime(HWND hwnd) {
	webcool_controller* controller =
		(webcool_controller*) GetWindowLongPtrW(hwnd, GWLP_USERDATA);
	if (controller == nullptr || !controller->running()) {
		return;
	}
	const std::string runtime_upload_dir = action::runtime_upload_dir_get();
	if (runtime_upload_dir.empty() || runtime_upload_dir == g_upload_dir) {
		return;
	}
	std::string err;
	if (!set_config_text(g_upload_dir, sizeof(g_upload_dir),
		runtime_upload_dir, "file save directory", err))
	{
		return;
	}
	set_window_utf8(hwnd, IDC_UPLOAD_EDIT, g_upload_dir);
	write_profile_text(L"paths", L"upload_dir", utf8_to_wide_text(g_upload_dir));
	update_control_window2(hwnd);
}

static HICON load_webcool_icon() {
	HICON icon = (HICON) LoadImageW(GetModuleHandleW(NULL),
		MAKEINTRESOURCEW(IDI_WEBCOOL), IMAGE_ICON, 0, 0,
		LR_DEFAULTSIZE | LR_SHARED);
	return icon != NULL ? icon : LoadIconW(NULL, MAKEINTRESOURCEW(32512));
}

static bool choose_folder(HWND hwnd, int edit_id, const wchar_t* title) {
	BROWSEINFOW bi;
	memset(&bi, 0, sizeof(bi));
	bi.hwndOwner = hwnd;
	bi.lpszTitle = title;
	bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_USENEWUI;
	LPITEMIDLIST item = SHBrowseForFolderW(&bi);
	if (item == NULL) {
		return false;
	}
	wchar_t path[32768];
	memset(path, 0, sizeof(path));
	const BOOL ok = SHGetPathFromIDListW(item, path);
	CoTaskMemFree(item);
	if (!ok) {
		return false;
	}
	SetWindowTextW(GetDlgItem(hwnd, edit_id), path);
	return true;
}

static bool choose_file(HWND hwnd, int edit_id, const wchar_t* title,
	  const wchar_t* filter) {
	wchar_t path[32768];
	memset(path, 0, sizeof(path));
	GetWindowTextW(GetDlgItem(hwnd, edit_id), path,
		(int) (sizeof(path) / sizeof(path[0])));
	OPENFILENAMEW ofn;
	memset(&ofn, 0, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = hwnd;
	ofn.lpstrTitle = title;
	ofn.lpstrFilter = filter;
	ofn.lpstrFile = path;
	ofn.nMaxFile = (DWORD) (sizeof(path) / sizeof(path[0]));
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
	if (!GetOpenFileNameW(&ofn)) {
		return false;
	}
	SetWindowTextW(GetDlgItem(hwnd, edit_id), path);
	return true;
}

static std::wstring browser_url_from_addr(const std::string& addr) {
	std::string url = addr;
	if (url.find("://") != std::string::npos) {
		return utf8_to_wide_text(url.c_str());
	}
	std::string host = url;
	std::string port;
	const std::string::size_type colon = url.rfind(':');
	if (colon != std::string::npos) {
		host = url.substr(0, colon);
		port = url.substr(colon);
	}
	if (host.empty() || host == "0.0.0.0" || host == "*") {
		host = "127.0.0.1";
	}
	return utf8_to_wide_text((std::string("http://") + host + port + "/").c_str());
}

static void open_service_browser(HWND hwnd) {
	webcool_controller* controller =
		(webcool_controller*) GetWindowLongPtrW(hwnd, GWLP_USERDATA);
	if (controller == nullptr || !controller->running()) {
		return;
	}
	const std::wstring url = browser_url_from_addr(controller->options().addr.c_str());
	ShellExecuteW(hwnd, L"open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
}

static void add_tray_icon(HWND hwnd) {
	NOTIFYICONDATAW nid;
	memset(&nid, 0, sizeof(nid));
	nid.cbSize = sizeof(nid);
	nid.hWnd = hwnd;
	nid.uID = WEBCOOL_TRAY_ID;
	nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
	nid.uCallbackMessage = WM_WEBCOOL_TRAY;
	nid.hIcon = load_webcool_icon();
	wcscpy_s(nid.szTip, tr(UI_TITLE));
	Shell_NotifyIconW(NIM_ADD, &nid);
}

static void remove_tray_icon(HWND hwnd) {
	NOTIFYICONDATAW nid;
	memset(&nid, 0, sizeof(nid));
	nid.cbSize = sizeof(nid);
	nid.hWnd = hwnd;
	nid.uID = WEBCOOL_TRAY_ID;
	Shell_NotifyIconW(NIM_DELETE, &nid);
}

static void hide_to_tray(HWND hwnd) {
	add_tray_icon(hwnd);
	ShowWindow(hwnd, SW_HIDE);
}

static void restore_from_tray(HWND hwnd) {
	remove_tray_icon(hwnd);
	ShowWindow(hwnd, SW_SHOWNORMAL);
	SetForegroundWindow(hwnd);
}

static void shutdown_control_window(HWND hwnd) {
	webcool_controller* controller =
		(webcool_controller*) GetWindowLongPtrW(hwnd, GWLP_USERDATA);
	if (controller != nullptr) {
		controller->stop();
	}
	save_control_config(hwnd);
	remove_tray_icon(hwnd);
	DestroyWindow(hwnd);
}

static bool activate_existing_control_window() {
	HWND hwnd = FindWindowW(k_control_window_class, NULL);
	if (hwnd == NULL) {
		return false;
	}
	remove_tray_icon(hwnd);
	ShowWindow(hwnd, SW_RESTORE);
	ShowWindow(hwnd, SW_SHOWNORMAL);
	SetForegroundWindow(hwnd);
	return true;
}

static void show_tray_menu(HWND hwnd) {
	HMENU menu = CreatePopupMenu();
	if (menu == NULL) {
		return;
	}
	AppendMenuW(menu, MF_STRING, IDM_TRAY_OPEN, tr(UI_TRAY_OPEN));
	AppendMenuW(menu, MF_STRING, IDM_TRAY_EXIT, tr(UI_EXIT));
	POINT pt;
	GetCursorPos(&pt);
	SetForegroundWindow(hwnd);
	TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN,
		pt.x, pt.y, 0, hwnd, NULL);
	DestroyMenu(menu);
}

static void enable_config_controls(HWND hwnd, bool enabled) {
	const int ids[] = {
		IDC_ADDR_EDIT, IDC_UPLOAD_EDIT, IDC_HTML_EDIT,
		IDC_SQLITE_EDIT, IDC_FFMPEG_EDIT, IDC_THREADS_EDIT,
		IDC_UPLOAD_BROWSE, IDC_HTML_BROWSE,
		IDC_SQLITE_BROWSE, IDC_FFMPEG_BROWSE
	};
	for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
		EnableWindow(GetDlgItem(hwnd, ids[i]), enabled ? TRUE : FALSE);
	}
}

static const int kAdvancedControlIds[] = {
	IDC_UPLOAD_LABEL, IDC_UPLOAD_EDIT, IDC_UPLOAD_BROWSE,
	IDC_HTML_LABEL, IDC_HTML_EDIT, IDC_HTML_BROWSE,
	IDC_SQLITE_LABEL, IDC_SQLITE_EDIT, IDC_SQLITE_BROWSE,
	IDC_FFMPEG_LABEL, IDC_FFMPEG_EDIT, IDC_FFMPEG_BROWSE,
	IDC_THREADS_LABEL, IDC_THREADS_EDIT
};

static int config_group_height() {
	return g_config_advanced_expanded ? kLayoutGroupHExpanded : kLayoutGroupHCollapsed;
}

static int config_status_top() {
	return kLayoutGroupTop + config_group_height() + kLayoutSectionGap;
}

static int config_btn_top() {
	return config_status_top() + kLayoutStatusH + kLayoutFooterGap;
}

static int config_window_outer_height() {
	const int client_h = config_btn_top() + kLayoutBtnH + kLayoutBottomMargin;
	RECT rect = { 0, 0, kWinWidth, client_h };
	AdjustWindowRect(&rect, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
	return rect.bottom - rect.top;
}

static void update_advanced_toggle_text(HWND hwnd) {
	HWND toggle = GetDlgItem(hwnd, IDC_ADVANCED_TOGGLE_BTN);
	if (toggle == NULL) {
		return;
	}
	SetWindowTextW(toggle, g_config_advanced_expanded
		? tr(UI_ADVANCED_COLLAPSE) : tr(UI_ADVANCED_EXPAND));
}

static void set_advanced_controls_visible(HWND hwnd, bool visible) {
	const int show = visible ? SW_SHOW : SW_HIDE;
	for (size_t i = 0; i < sizeof(kAdvancedControlIds) / sizeof(kAdvancedControlIds[0]); ++i) {
		HWND ctrl = GetDlgItem(hwnd, kAdvancedControlIds[i]);
		if (ctrl != NULL) {
			ShowWindow(ctrl, show);
		}
	}
}

static void relayout_control_window(HWND hwnd) {
	const int group_h = config_group_height();
	const int status_top = config_status_top();
	const int btn_top = config_btn_top();

	HWND group = GetDlgItem(hwnd, IDC_GROUP_TEXT);
	if (group != NULL) {
		SetWindowPos(group, NULL, 24, kLayoutGroupTop, 672, group_h, SWP_NOZORDER);
	}
	HWND status_frame = GetDlgItem(hwnd, IDC_STATUS_FRAME);
	if (status_frame != NULL) {
		SetWindowPos(status_frame, NULL, 24, status_top, 672, kLayoutStatusH, SWP_NOZORDER);
	}
	HWND status = GetDlgItem(hwnd, IDC_STATUS_TEXT);
	if (status != NULL) {
		SetWindowPos(status, NULL, 40, status_top + 12, 640, kLayoutStatusH - 24, SWP_NOZORDER);
	}
	const int btn_ids[] = {
		IDC_BROWSER_BTN, IDC_START_BTN, IDC_STOP_BTN,
		IDC_MINIMIZE_BTN, IDC_EXIT_BTN
	};
	const int btn_x[] = { 24, 148, 244, 340, 448 };
	const int btn_w[] = { 112, 84, 84, 92, 84 };
	for (int i = 0; i < 5; ++i) {
		HWND btn = GetDlgItem(hwnd, btn_ids[i]);
		if (btn != NULL) {
			SetWindowPos(btn, NULL, btn_x[i], btn_top, btn_w[i], kLayoutBtnH, SWP_NOZORDER);
		}
	}

	RECT wr;
	GetWindowRect(hwnd, &wr);
	const int outer_h = config_window_outer_height();
	SetWindowPos(hwnd, NULL, wr.left, wr.top, kWinWidth, outer_h,
		SWP_NOZORDER | SWP_NOMOVE);
	InvalidateRect(hwnd, NULL, TRUE);
}

static void apply_config_advanced_state(HWND hwnd, bool expanded, bool persist) {
	g_config_advanced_expanded = expanded;
	set_advanced_controls_visible(hwnd, expanded);
	update_advanced_toggle_text(hwnd);
	relayout_control_window(hwnd);
	if (persist) {
		write_profile_int(L"ui", L"advanced_expanded", expanded ? 1 : 0);
	}
}

static void toggle_config_advanced(HWND hwnd) {
	apply_config_advanced_state(hwnd, !g_config_advanced_expanded, true);
}

static bool read_control_config(HWND hwnd, webcool_options& options,
	  std::string& err) {
	err.clear();
	const std::string addr = get_window_utf8(hwnd, IDC_ADDR_EDIT);
	const std::string upload = get_window_utf8(hwnd, IDC_UPLOAD_EDIT);
	std::string html = get_window_utf8(hwnd, IDC_HTML_EDIT);
	const std::string sqlite = get_window_utf8(hwnd, IDC_SQLITE_EDIT);
	const std::string ffmpeg = get_window_utf8(hwnd, IDC_FFMPEG_EDIT);
	const std::string threads_text = get_window_utf8(hwnd, IDC_THREADS_EDIT);

	if (addr.empty()) {
		err = "监听地址不能为空";
		return false;
	}
	if (upload.empty()) {
		err = "文件保存目录不能为空";
		return false;
	}
	if (html.empty()) {
		err = "静态资源根目录不能为空";
		return false;
	}
	html = normalize_static_home_path(html);
	const std::string index_html = join_config_path(html, "main.html");
	if (!readable_regular_file(index_html)) {
		err = "静态资源根目录无效，无法读取: ";
		err += index_html;
		return false;
	}
	const int threads = atoi(threads_text.c_str());
	if (threads <= 0) {
		err = "工作线程数必须大于 0";
		return false;
	}

	if (!set_config_text(g_upload_dir, sizeof(g_upload_dir),
		upload, "file save directory", err)
		|| !set_config_text(g_html_home, sizeof(g_html_home),
			html, "static resource root directory", err)
		|| !set_config_text(g_sqlite_lib, sizeof(g_sqlite_lib),
			sqlite, "sqlite dynamic library path", err)
		|| !set_config_text(g_ffmpeg_path, sizeof(g_ffmpeg_path),
			ffmpeg, "ffmpeg executable path", err))
	{
		return false;
	}
	options.addr = addr.c_str();
	options.nthreads = threads;
	set_window_utf8(hwnd, IDC_HTML_EDIT, g_html_home);
	return true;
}

static void update_control_window2(HWND hwnd) {
	webcool_controller* controller =
		(webcool_controller*) GetWindowLongPtrW(hwnd, GWLP_USERDATA);
	if (controller == nullptr) {
		return;
	}
	const bool running = controller->running();
	g_service_running = running;
	std::wstring status = running ? tr(UI_STATUS_RUNNING) : tr(UI_STATUS_STOPPED);
	status += L"\r\n";
	status += tr(UI_LISTEN_ADDR);
	status += L": ";
	status += utf8_to_wide_text(controller->options().addr.c_str());
	status += L"\r\n";
	status += tr(UI_UPLOAD_DIR);
	status += L": ";
	status += utf8_to_wide_text(g_upload_dir);
	SetWindowTextW(GetDlgItem(hwnd, IDC_STATUS_TEXT), status.c_str());
	EnableWindow(GetDlgItem(hwnd, IDC_BROWSER_BTN), running ? TRUE : FALSE);
	EnableWindow(GetDlgItem(hwnd, IDC_START_BTN), running ? FALSE : TRUE);
	EnableWindow(GetDlgItem(hwnd, IDC_STOP_BTN), running ? TRUE : FALSE);
	enable_config_controls(hwnd, !running);
	InvalidateRect(GetDlgItem(hwnd, IDC_STATUS_TEXT), NULL, TRUE);
}

static void apply_control_language(HWND hwnd) {
	SetWindowTextW(hwnd, tr(UI_TITLE));
	SetWindowTextW(GetDlgItem(hwnd, IDC_TITLE_TEXT), tr(UI_TITLE));
	SetWindowTextW(GetDlgItem(hwnd, IDC_GROUP_TEXT), tr(UI_SERVICE_CONFIG));
	SetWindowTextW(GetDlgItem(hwnd, IDC_ADDR_LABEL), tr(UI_LISTEN_ADDR));
	SetWindowTextW(GetDlgItem(hwnd, IDC_UPLOAD_LABEL), tr(UI_UPLOAD_DIR));
	SetWindowTextW(GetDlgItem(hwnd, IDC_HTML_LABEL), tr(UI_HTML_HOME));
	SetWindowTextW(GetDlgItem(hwnd, IDC_SQLITE_LABEL), tr(UI_SQLITE_LIB));
	SetWindowTextW(GetDlgItem(hwnd, IDC_FFMPEG_LABEL), tr(UI_FFMPEG_EXE));
	SetWindowTextW(GetDlgItem(hwnd, IDC_THREADS_LABEL), tr(UI_THREADS));
	SetWindowTextW(GetDlgItem(hwnd, IDC_LANG_LABEL), tr(UI_LANGUAGE));
	SetWindowTextW(GetDlgItem(hwnd, IDC_START_BTN), tr(UI_START));
	SetWindowTextW(GetDlgItem(hwnd, IDC_STOP_BTN), tr(UI_STOP));
	SetWindowTextW(GetDlgItem(hwnd, IDC_MINIMIZE_BTN), tr(UI_MINIMIZE));
	SetWindowTextW(GetDlgItem(hwnd, IDC_EXIT_BTN), tr(UI_EXIT));
	SetWindowTextW(GetDlgItem(hwnd, IDC_BROWSER_BTN), tr(UI_OPEN_BROWSER));
	update_advanced_toggle_text(hwnd);
	HWND lang = GetDlgItem(hwnd, IDC_LANG_COMBO);
	if (lang != NULL) {
		SendMessageW(lang, CB_RESETCONTENT, 0, 0);
		SendMessageW(lang, CB_ADDSTRING, 0, (LPARAM)L"中文");
		SendMessageW(lang, CB_ADDSTRING, 0, (LPARAM)L"English");
		SendMessageW(lang, CB_SETCURSEL,
			g_gui_language == GUI_LANG_EN ? 1 : 0, 0);
	}
	update_control_window2(hwnd);
}

static LRESULT CALLBACK control_window_proc(HWND hwnd, UINT msg,
	  WPARAM wparam, LPARAM lparam) {
	switch (msg) {
	case WM_CREATE:
	{
		CREATESTRUCTW* cs = (CREATESTRUCTW*) lparam;
		HMODULE module = GetModuleHandleW(NULL);
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR) cs->lpCreateParams);
		SetWindowTextW(hwnd, tr(UI_TITLE));
		SetTimer(hwnd, WEBCOOL_SYNC_TIMER_ID, 1000, NULL);
		ensure_control_theme_gdi();
		HFONT ui_font = g_control_font;
		HFONT title_font = g_control_title_font;

		HWND title = CreateWindowW(L"STATIC", tr(UI_TITLE),
			WS_CHILD | WS_VISIBLE | SS_LEFT, 28, 16, 320, 26, hwnd,
			(HMENU) IDC_TITLE_TEXT, module, NULL);
		SendMessageW(title, WM_SETFONT, (WPARAM) title_font, TRUE);

		HWND lang_label = CreateWindowW(L"STATIC", tr(UI_LANGUAGE),
			WS_CHILD | WS_VISIBLE | SS_LEFT, 480, 20, 72, 20, hwnd,
			(HMENU) IDC_LANG_LABEL, module, NULL);
		SendMessageW(lang_label, WM_SETFONT, (WPARAM) ui_font, TRUE);
		HWND lang_combo = CreateWindowW(L"COMBOBOX", L"",
			WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
			556, 16, 128, 260, hwnd, (HMENU) IDC_LANG_COMBO, module, NULL);
		SendMessageW(lang_combo, WM_SETFONT, (WPARAM) ui_font, TRUE);

		HWND group = CreateWindowW(L"BUTTON", tr(UI_SERVICE_CONFIG),
			WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
			24, kLayoutGroupTop, 672, kLayoutGroupHCollapsed, hwnd,
			(HMENU) IDC_GROUP_TEXT, module, NULL);
		SendMessageW(group, WM_SETFONT, (WPARAM) ui_font, TRUE);

		const wchar_t* labels[] = {
			tr(UI_LISTEN_ADDR), tr(UI_UPLOAD_DIR), tr(UI_HTML_HOME),
			tr(UI_SQLITE_LIB), tr(UI_FFMPEG_EXE), tr(UI_THREADS)
		};
		const int label_ids[] = {
			IDC_ADDR_LABEL, IDC_UPLOAD_LABEL, IDC_HTML_LABEL,
			IDC_SQLITE_LABEL, IDC_FFMPEG_LABEL, IDC_THREADS_LABEL
		};
		const int edit_ids[] = {
			IDC_ADDR_EDIT, IDC_UPLOAD_EDIT, IDC_HTML_EDIT,
			IDC_SQLITE_EDIT, IDC_FFMPEG_EDIT, IDC_THREADS_EDIT
		};
		const int browse_ids[] = {
			IDC_UPLOAD_BROWSE, IDC_HTML_BROWSE,
			IDC_SQLITE_BROWSE, IDC_FFMPEG_BROWSE
		};
		for (int i = 0; i < 6; ++i) {
			const int y = i == 0
				? kLayoutFormY0
				: kLayoutAdvancedY0 + (i - 1) * kLayoutFormRowH;
			const DWORD style = WS_CHILD | WS_BORDER | ES_AUTOHSCROLL;
			const DWORD visible = (i == 0) ? WS_VISIBLE : 0;
			HWND label = CreateWindowW(L"STATIC", labels[i],
				WS_CHILD | visible | SS_LEFT,
				44, y + 4, 132, 20, hwnd, (HMENU)(INT_PTR) label_ids[i], module, NULL);
			SendMessageW(label, WM_SETFONT, (WPARAM) ui_font, TRUE);
			const bool has_browse = i >= 1 && i <= 4;
			const int edit_width = i == 0 ? kLayoutAddrEditWidth
				: (i == 5 ? 96 : (has_browse ? 416 : 456));
			HWND edit = CreateWindowW(L"EDIT", L"",
				WS_CHILD | visible | style,
				kLayoutFormEditX, y, edit_width, 24, hwnd, (HMENU) (INT_PTR) edit_ids[i], module, NULL);
			SendMessageW(edit, WM_SETFONT, (WPARAM) ui_font, TRUE);
			if (has_browse) {
				HWND browse = CreateWindowW(L"BUTTON", L"...",
					WS_CHILD | visible | BS_PUSHBUTTON,
					612, y, 36, 24, hwnd, (HMENU) (INT_PTR) browse_ids[i - 1], module, NULL);
				SendMessageW(browse, WM_SETFONT, (WPARAM) ui_font, TRUE);
			}
		}

		HWND toggle = CreateWindowW(L"BUTTON", tr(UI_ADVANCED_EXPAND),
			WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
			kLayoutToggleX, kLayoutFormY0, kLayoutToggleWidth, 24, hwnd,
			(HMENU) IDC_ADVANCED_TOGGLE_BTN, module, NULL);
		SendMessageW(toggle, WM_SETFONT, (WPARAM) ui_font, TRUE);

		CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ETCHEDFRAME,
			24, config_status_top(), 672, kLayoutStatusH, hwnd,
			(HMENU) IDC_STATUS_FRAME, module, NULL);
		HWND status = CreateWindowW(L"STATIC", L"",
			WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
			40, config_status_top() + 12, 640, kLayoutStatusH - 24, hwnd,
			(HMENU) IDC_STATUS_TEXT, module, NULL);
		SendMessageW(status, WM_SETFONT, (WPARAM) ui_font, TRUE);

		const int btn_ids[] = {
			IDC_BROWSER_BTN, IDC_START_BTN, IDC_STOP_BTN,
			IDC_MINIMIZE_BTN, IDC_EXIT_BTN
		};
		const ui_text_t btn_texts[] = {
			UI_OPEN_BROWSER, UI_START, UI_STOP, UI_MINIMIZE, UI_EXIT
		};
		const int btn_x[] = { 24, 148, 244, 340, 448 };
		const int btn_w[] = { 112, 84, 84, 92, 84 };
		for (int i = 0; i < 5; ++i) {
			HWND btn = CreateWindowW(L"BUTTON", tr(btn_texts[i]),
				WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
				btn_x[i], config_btn_top(), btn_w[i], kLayoutBtnH, hwnd,
				(HMENU) (INT_PTR) btn_ids[i], module, NULL);
			SendMessageW(btn, WM_SETFONT, (WPARAM) ui_font, TRUE);
		}

		webcool_controller* controller =
			(webcool_controller*) cs->lpCreateParams;
		if (controller != nullptr) {
			set_window_utf8(hwnd, IDC_ADDR_EDIT, controller->options().addr.c_str());
			char threads_buf[32];
			snprintf(threads_buf, sizeof(threads_buf), "%d", controller->options().nthreads);
			set_window_utf8(hwnd, IDC_THREADS_EDIT, threads_buf);
		}
		set_window_utf8(hwnd, IDC_UPLOAD_EDIT, g_upload_dir);
		set_window_utf8(hwnd, IDC_HTML_EDIT, g_html_home);
		set_window_utf8(hwnd, IDC_SQLITE_EDIT, g_sqlite_lib);
		set_window_utf8(hwnd, IDC_FFMPEG_EDIT, g_ffmpeg_path);
		apply_control_language(hwnd);
		apply_config_advanced_state(hwnd, g_config_advanced_expanded, false);
		return 0;
	}
	case WM_COMMAND:
	{
		webcool_controller* controller =
			(webcool_controller*) GetWindowLongPtrW(hwnd, GWLP_USERDATA);
		const int id = LOWORD(wparam);
		if (id == IDC_LANG_COMBO && HIWORD(wparam) == CBN_SELCHANGE) {
			const LRESULT sel = SendMessageW(GetDlgItem(hwnd, IDC_LANG_COMBO),
				CB_GETCURSEL, 0, 0);
			g_gui_language = sel == 1 ? GUI_LANG_EN : GUI_LANG_ZH;
			apply_control_language(hwnd);
			save_control_config(hwnd);
			return 0;
		}
		if (id == IDC_ADVANCED_TOGGLE_BTN) {
			toggle_config_advanced(hwnd);
			return 0;
		}
		if (id == IDC_START_BTN && controller != nullptr) {
			std::string err;
			webcool_options options = controller->options();
			if (!read_control_config(hwnd, options, err)) {
				std::wstring message = tr(UI_INVALID_CONFIG);
				message += utf8_to_wide_text(err.c_str());
				MessageBoxW(hwnd, message.c_str(), L"webcool", MB_OK | MB_ICONWARNING);
				return 0;
			}
			controller->configure(options);
			if (!controller->start(err)) {
				std::wstring start_message = tr(UI_START_FAILED);
				start_message += utf8_to_wide_text(err.c_str());
				MessageBoxW(hwnd, start_message.c_str(), L"webcool", MB_OK | MB_ICONERROR);
				update_control_window2(hwnd);
				return 0;
			}
			save_control_config(hwnd);
			update_control_window2(hwnd);
			return 0;
		}
		if (id == IDC_BROWSER_BTN) {
			open_service_browser(hwnd);
			return 0;
		}
		if (id == IDC_UPLOAD_BROWSE) {
			choose_folder(hwnd, IDC_UPLOAD_EDIT, tr(UI_SELECT_UPLOAD));
			return 0;
		}
		if (id == IDC_HTML_BROWSE) {
			choose_folder(hwnd, IDC_HTML_EDIT, tr(UI_SELECT_HTML));
			return 0;
		}
		if (id == IDC_SQLITE_BROWSE) {
			choose_file(hwnd, IDC_SQLITE_EDIT, tr(UI_SELECT_SQLITE),
				tr(UI_SQLITE_FILTER));
			return 0;
		}
		if (id == IDC_FFMPEG_BROWSE) {
			choose_file(hwnd, IDC_FFMPEG_EDIT, tr(UI_SELECT_FFMPEG),
				tr(UI_FFMPEG_FILTER));
			return 0;
		}
		if (id == IDC_STOP_BTN && controller != nullptr) {
			controller->stop();
			update_control_window2(hwnd);
			return 0;
		}
		if (id == IDC_MINIMIZE_BTN) {
			hide_to_tray(hwnd);
			return 0;
		}
		if (id == IDC_EXIT_BTN) {
			shutdown_control_window(hwnd);
			return 0;
		}
		if (id == IDM_TRAY_OPEN) {
			restore_from_tray(hwnd);
			return 0;
		}
		if (id == IDM_TRAY_EXIT) {
			shutdown_control_window(hwnd);
			return 0;
		}
		break;
	}
	case WM_SIZE:
		if (wparam == SIZE_MINIMIZED) {
			hide_to_tray(hwnd);
			return 0;
		}
		break;
	case WM_TIMER:
		if (wparam == WEBCOOL_SYNC_TIMER_ID) {
			sync_upload_dir_from_runtime(hwnd);
			return 0;
		}
		break;
	case WM_WEBCOOL_TRAY:
		if (lparam == WM_LBUTTONUP || lparam == WM_LBUTTONDBLCLK) {
			restore_from_tray(hwnd);
			return 0;
		}
		if (lparam == WM_RBUTTONUP || lparam == WM_CONTEXTMENU) {
			show_tray_menu(hwnd);
			return 0;
		}
		break;
	case WM_CTLCOLORSTATIC:
	{
		HDC hdc = (HDC) wparam;
		HWND ctrl = (HWND) lparam;
		SetBkMode(hdc, TRANSPARENT);
		const int ctrl_id = GetDlgCtrlID(ctrl);
		if (ctrl_id == IDC_TITLE_TEXT) {
			SetTextColor(hdc, kColorText);
		} else if (ctrl_id == IDC_STATUS_TEXT) {
			SetTextColor(hdc, g_service_running ? kColorRunning : kColorStopped);
		} else if (ctrl_id >= IDC_ADDR_LABEL && ctrl_id <= IDC_LANG_LABEL) {
			SetTextColor(hdc, kColorTextMuted);
		} else {
			SetTextColor(hdc, kColorText);
		}
		return (LRESULT) (g_control_bg_brush ? g_control_bg_brush : GetStockObject(WHITE_BRUSH));
	}
	case WM_CTLCOLOREDIT:
	{
		HDC hdc = (HDC) wparam;
		SetBkColor(hdc, RGB(255, 255, 255));
		SetTextColor(hdc, kColorText);
		return (LRESULT) (g_edit_bg_brush ? g_edit_bg_brush : GetStockObject(WHITE_BRUSH));
	}
	case WM_CLOSE:
	{
		webcool_controller* controller =
			(webcool_controller*) GetWindowLongPtrW(hwnd, GWLP_USERDATA);
		if (controller != nullptr && controller->running()) {
			const int choice = MessageBoxW(hwnd, tr(UI_CLOSE_WHILE_RUNNING),
				tr(UI_TITLE), MB_YESNO | MB_ICONQUESTION);
			if (choice == IDYES) {
				hide_to_tray(hwnd);
				return 0;
			}
			if (choice == IDNO) {
				shutdown_control_window(hwnd);
				return 0;
			}
			return 0;
		}
		shutdown_control_window(hwnd);
		return 0;
	}
	case WM_DESTROY:
		KillTimer(hwnd, WEBCOOL_SYNC_TIMER_ID);
		remove_tray_icon(hwnd);
		PostQuitMessage(0);
		return 0;
	default:
		break;
	}
	return DefWindowProcW(hwnd, msg, wparam, lparam);
}

int run_windows_control_gui(webcool_controller& controller) {
	HANDLE single_instance = CreateMutexW(NULL, FALSE,
		k_control_single_instance_mutex);
	if (single_instance == NULL) {
		return 1;
	}
	if (GetLastError() == ERROR_ALREADY_EXISTS) {
		activate_existing_control_window();
		CloseHandle(single_instance);
		return 0;
	}

	load_control_config(controller);
	ensure_control_theme_gdi();
	INITCOMMONCONTROLSEX icc;
	memset(&icc, 0, sizeof(icc));
	icc.dwSize = sizeof(icc);
	icc.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
	InitCommonControlsEx(&icc);
	WNDCLASSW wc;
	memset(&wc, 0, sizeof(wc));
	wc.lpfnWndProc = control_window_proc;
	wc.hInstance = GetModuleHandleW(NULL);
	wc.hIcon = load_webcool_icon();
	wc.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
	wc.hbrBackground = g_control_bg_brush ? g_control_bg_brush : (HBRUSH) (COLOR_WINDOW + 1);
	wc.lpszClassName = k_control_window_class;
	if (!RegisterClassW(&wc)) {
		CloseHandle(single_instance);
		return 1;
	}

	HWND hwnd = CreateWindowExW(0, k_control_window_class, tr(UI_TITLE),
		WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
		CW_USEDEFAULT, CW_USEDEFAULT, kWinWidth, config_window_outer_height(),
		NULL, NULL, GetModuleHandleW(NULL), &controller);
	if (hwnd == NULL) {
		CloseHandle(single_instance);
		return 1;
	}
	ShowWindow(hwnd, SW_SHOW);
	UpdateWindow(hwnd);

	MSG msg;
	while (GetMessageW(&msg, NULL, 0, 0) > 0) {
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
	CloseHandle(single_instance);
	return (int) msg.wParam;
}

void ensure_console_for_cli() {
	if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
		AllocConsole();
	}
	FILE* fp = nullptr;
	freopen_s(&fp, "CONOUT$", "w", stdout);
	freopen_s(&fp, "CONOUT$", "w", stderr);
	freopen_s(&fp, "CONIN$", "r", stdin);
}

#endif // _WIN32
