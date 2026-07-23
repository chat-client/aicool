#import <Cocoa/Cocoa.h>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include "mac_gui.h"
#include "../action/action_util.h"
#include "../config.h"
#include "../win32/webcool_controller.h"

namespace {

static NSString* ns_text(const char* text) {
	return [NSString stringWithUTF8String:text ? text : ""] ?: @"";
}

static std::string utf8_text(NSTextField* field) {
	const char* text = field.stringValue.UTF8String;
	return text ? text : "";
}

static NSString* localized_text(BOOL english, NSString* chinese,
	  NSString* englishText) {
	return english ? englishText : chinese;
}

static std::string localized_utf8(BOOL english, NSString* chinese,
	  NSString* englishText) {
	const char* text = localized_text(english, chinese, englishText).UTF8String;
	return text ? text : "";
}

static NSString* browser_url(const std::string& address) {
	std::string value = address;
	if (value.find("://") == std::string::npos) {
		std::string host = value;
		std::string port;
		const std::string::size_type colon = value.rfind(':');
		if (colon != std::string::npos) {
			host = value.substr(0, colon);
			port = value.substr(colon);
		}
		if (host.empty() || host == "0.0.0.0" || host == "*") host = "127.0.0.1";
		value = "http://" + host + port + "/";
	}
	return ns_text(value.c_str());
}

static NSImage* load_webcool_app_icon() {
	NSMutableArray<NSString*>* candidates = [NSMutableArray array];
	NSString* bundledIcon = [NSBundle.mainBundle pathForResource:@"webcool"
		ofType:@"icns"];
	if (bundledIcon.length) [candidates addObject:bundledIcon];
	NSString* bundledPng = [NSBundle.mainBundle
		pathForResource:@"webcool-icon-1024" ofType:@"png"];
	if (bundledPng.length) [candidates addObject:bundledPng];
	if (g_html_home[0]) {
		[candidates addObject:[[ns_text(g_html_home)
			stringByAppendingPathComponent:@"icon/webcool-icon-1024.png"]
			stringByStandardizingPath]];
	}
	NSString* executable = NSBundle.mainBundle.executablePath;
	if (executable.length) {
		NSString* executableDir = executable.stringByDeletingLastPathComponent;
		[candidates addObject:[[executableDir
			stringByAppendingPathComponent:@"html/icon/webcool-icon-1024.png"]
			stringByStandardizingPath]];
		[candidates addObject:[[executableDir
			stringByAppendingPathComponent:@"../html/icon/webcool-icon-1024.png"]
			stringByStandardizingPath]];
	}
	[candidates addObject:@"/opt/soft/webcool/html/icon/webcool-icon-1024.png"];

	for (NSString* path in candidates) {
		NSImage* icon = [[NSImage alloc] initWithContentsOfFile:path];
		if (icon != nil && icon.isValid) return icon;
	}
	return nil;
}

static NSString* find_webcool_html_home(NSString* configured) {
	NSMutableArray<NSString*>* candidates = [NSMutableArray array];
	if (configured.length) [candidates addObject:configured];
	NSString* resourcePath = NSBundle.mainBundle.resourcePath;
	if (resourcePath.length) {
		[candidates addObject:[resourcePath stringByAppendingPathComponent:@"html"]];
	}
	NSString* executable = NSBundle.mainBundle.executablePath;
	if (executable.length) {
		NSString* executableDir = executable.stringByDeletingLastPathComponent;
		[candidates addObject:[executableDir
			stringByAppendingPathComponent:@"../../../html"]];
	}
	[candidates addObject:@"/opt/soft/webcool/html"];

	for (NSString* candidate in candidates) {
		NSString* path = candidate.stringByStandardizingPath;
		NSString* mainHtml = [path stringByAppendingPathComponent:@"main.html"];
		if ([NSFileManager.defaultManager isReadableFileAtPath:mainHtml]) return path;
	}
	return configured ?: @"";
}

static NSString* find_webcool_runtime_file(NSString* configured,
	  NSArray<NSString*>* relativeCandidates, BOOL executable) {
	NSMutableArray<NSString*>* candidates = [NSMutableArray array];
	if (configured.length) [candidates addObject:configured];
	NSString* executablePath = NSBundle.mainBundle.executablePath;
	if (executablePath.length) {
		NSString* executableDir = executablePath.stringByDeletingLastPathComponent;
		for (NSString* relative in relativeCandidates) {
			[candidates addObject:[executableDir stringByAppendingPathComponent:relative]];
		}
	}
	for (NSString* candidate in candidates) {
		NSString* path = candidate.stringByStandardizingPath;
		BOOL directory = NO;
		if (![NSFileManager.defaultManager fileExistsAtPath:path isDirectory:&directory]
			|| directory) continue;
		if (!executable || [NSFileManager.defaultManager isExecutableFileAtPath:path]) {
			return path;
		}
	}
	return configured ?: @"";
}

static NSString* nonempty_default(NSString* saved, NSString* fallback) {
	return saved.length ? saved : (fallback ?: @"");
}

static NSTextField* label(NSString* text, NSRect frame) {
	NSTextField* view = [[NSTextField alloc] initWithFrame:frame];
	view.stringValue = text;
	view.editable = NO;
	view.selectable = NO;
	view.bezeled = NO;
	view.drawsBackground = NO;
	view.textColor = NSColor.secondaryLabelColor;
	view.font = [NSFont systemFontOfSize:13.0];
	return view;
}

static NSTextField* edit(NSRect frame) {
	NSTextField* view = [[NSTextField alloc] initWithFrame:frame];
	view.font = [NSFont systemFontOfSize:13.0];
	return view;
}

static NSButton* button(NSString* title, id target, SEL action, NSRect frame) {
	NSButton* view = [[NSButton alloc] initWithFrame:frame];
	view.title = title;
	view.bezelStyle = NSBezelStyleRounded;
	view.target = target;
	view.action = action;
	return view;
}

} // namespace

@interface WebcoolMacDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate>
- (instancetype)initWithController:(webcool_controller*)controller;
@end

@implementation WebcoolMacDelegate {
	webcool_controller* _controller;
	NSWindow* _window;
	NSTextField* _titleView;
	NSTextField* _address;
	NSTextField* _upload;
	NSTextField* _html;
	NSTextField* _sqlite;
	NSTextField* _ffmpeg;
	NSTextField* _threads;
	NSTextField* _status;
	NSButton* _start;
	NSButton* _stop;
	NSButton* _browser;
	NSButton* _advancedToggle;
	NSButton* _minimizeButton;
	NSButton* _exitButton;
	NSMenuItem* _aboutMenuItem;
	NSMenuItem* _languageMenuItem;
	NSMenuItem* _chineseMenuItem;
	NSMenuItem* _englishMenuItem;
	NSMenuItem* _showMenuItem;
	NSMenuItem* _quitMenuItem;
	NSArray<NSTextField*>* _rowLabels;
	NSArray<NSButton*>* _browseButtons;
	NSArray<NSTextField*>* _configFields;
	NSArray<NSView*>* _advancedViews;
	NSTimer* _syncTimer;
	NSString* _showNotification;
	BOOL _english;
	BOOL _advancedExpanded;
	BOOL _quitting;
}

- (instancetype)initWithController:(webcool_controller*)controller {
	self = [super init];
	if (self) {
		_controller = controller;
		NSString* language = [NSUserDefaults.standardUserDefaults
			stringForKey:@"ui.language"];
		_english = language.length > 0
			&& [language caseInsensitiveCompare:@"en"] == NSOrderedSame;
	}
	return self;
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
	(void) notification;
	[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
	NSImage* appIcon = load_webcool_app_icon();
	if (appIcon != nil) NSApp.applicationIconImage = appIcon;
	_showNotification = [NSString stringWithFormat:@"cn.webcool.control.show.%u", getuid()];
	[NSDistributedNotificationCenter.defaultCenter addObserver:self
		selector:@selector(showWindow:) name:_showNotification object:nil];
	[self createMenu];
	[self createWindow];
	[self loadSettings];
	[self applyLanguage];
	[self updateStatus];
	[_window center];
	[_window makeKeyAndOrderFront:nil];
	[NSApp activateIgnoringOtherApps:YES];
	_syncTimer = [NSTimer scheduledTimerWithTimeInterval:1.0 target:self
		selector:@selector(syncRuntimeStorage:) userInfo:nil repeats:YES];
}

- (void)createMenu {
	NSMenu* bar = [[NSMenu alloc] init];
	NSMenuItem* appItem = [[NSMenuItem alloc] init];
	[bar addItem:appItem];
	NSMenu* appMenu = [[NSMenu alloc] init];
	_aboutMenuItem = [[NSMenuItem alloc]
		initWithTitle:@"" action:@selector(showAbout:)
		keyEquivalent:@""];
	_aboutMenuItem.target = self;
	[appMenu addItem:_aboutMenuItem];
	[appMenu addItem:[NSMenuItem separatorItem]];

	_languageMenuItem = [[NSMenuItem alloc] initWithTitle:@"" action:nil
		keyEquivalent:@""];
	NSMenu* languageMenu = [[NSMenu alloc] init];
	_chineseMenuItem = [[NSMenuItem alloc] initWithTitle:@"中文"
		action:@selector(changeLanguage:) keyEquivalent:@""];
	_chineseMenuItem.target = self;
	_chineseMenuItem.tag = 0;
	[languageMenu addItem:_chineseMenuItem];
	_englishMenuItem = [[NSMenuItem alloc] initWithTitle:@"English"
		action:@selector(changeLanguage:) keyEquivalent:@""];
	_englishMenuItem.target = self;
	_englishMenuItem.tag = 1;
	[languageMenu addItem:_englishMenuItem];
	_languageMenuItem.submenu = languageMenu;
	[appMenu addItem:_languageMenuItem];
	[appMenu addItem:[NSMenuItem separatorItem]];

	_showMenuItem = [[NSMenuItem alloc]
		initWithTitle:@"" action:@selector(showWindow:)
		keyEquivalent:@""];
	_showMenuItem.target = self;
	[appMenu addItem:_showMenuItem];
	[appMenu addItem:[NSMenuItem separatorItem]];
	_quitMenuItem = [[NSMenuItem alloc]
		initWithTitle:@"" action:@selector(requestQuit:)
		keyEquivalent:@"q"];
	_quitMenuItem.target = self;
	[appMenu addItem:_quitMenuItem];
	appItem.submenu = appMenu;
	NSApp.mainMenu = bar;
}

- (void)showAbout:(id)sender {
	(void) sender;
	NSDictionary* info = NSBundle.mainBundle.infoDictionary;
	NSString* version = info[@"CFBundleShortVersionString"];
	if (!version.length) version = @"2.0.0";
	NSString* build = info[@"CFBundleVersion"];
	if (!build.length) build = version;
	NSString* author = info[@"WebcoolAuthor"];
	if (!author.length) author = @"郑树新 (Zheng Shuxin)";
	NSString* copyright = info[@"NSHumanReadableCopyright"];
	if (!copyright.length) {
		copyright = @"Copyright © 2026 郑树新. All rights reserved.";
	}
	NSString* creditsText = [NSString stringWithFormat:@"%@：%@\n%@",
		localized_text(_english, @"作者", @"Author"), author,
		localized_text(_english, @"webcool 私有网盘与文件管理服务",
			@"webcool private cloud drive and file management service")];
	if (![info[@"NSHumanReadableCopyright"] length]) {
		creditsText = [creditsText stringByAppendingFormat:@"\n\n%@", copyright];
	}
	NSMutableParagraphStyle* paragraph = [[NSMutableParagraphStyle alloc] init];
	paragraph.alignment = NSTextAlignmentCenter;
	NSAttributedString* credits = [[NSAttributedString alloc]
		initWithString:creditsText attributes:@{
			NSFontAttributeName: [NSFont systemFontOfSize:12.0],
			NSParagraphStyleAttributeName: paragraph
		}];
	NSMutableDictionary<NSAboutPanelOptionKey, id>* options =
		[NSMutableDictionary dictionaryWithDictionary:@{
			NSAboutPanelOptionApplicationName: @"webcool",
			NSAboutPanelOptionApplicationVersion: version,
			NSAboutPanelOptionVersion: [NSString stringWithFormat:@"%@ %@ (Build %@)",
				localized_text(_english, @"版本", @"Version"), version, build],
			NSAboutPanelOptionCredits: credits
		}];
	NSImage* icon = load_webcool_app_icon();
	if (icon != nil) options[NSAboutPanelOptionApplicationIcon] = icon;
	[NSApp orderFrontStandardAboutPanelWithOptions:options];
	[NSApp activateIgnoringOtherApps:YES];
}

- (void)createWindow {
	const NSRect frame = NSMakeRect(0, 0, 720, 477);
	_window = [[NSWindow alloc] initWithContentRect:frame
		styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
			NSWindowStyleMaskMiniaturizable
		backing:NSBackingStoreBuffered defer:NO];
	_window.title = @"webcool 控制界面";
	_window.delegate = self;
	_window.releasedWhenClosed = NO;
	NSView* content = _window.contentView;

	_titleView = label(@"webcool 控制界面", NSMakeRect(24, 432, 320, 28));
	_titleView.font = [NSFont boldSystemFontOfSize:20.0];
	_titleView.textColor = NSColor.labelColor;
	[content addSubview:_titleView];

	NSArray<NSString*>* names = @[@"监听地址", @"文件保存目录", @"静态资源根目录",
		@"sqlite 动态库路径", @"ffmpeg 可执行文件", @"工作线程数"];
	NSMutableArray<NSTextField*>* fields = [NSMutableArray array];
	NSMutableArray<NSTextField*>* rowLabels = [NSMutableArray array];
	NSMutableArray<NSButton*>* browseButtons = [NSMutableArray array];
	NSMutableArray<NSView*>* advancedViews = [NSMutableArray array];
	for (NSUInteger i = 0; i < names.count; ++i) {
		const CGFloat y = 389 - i * 43;
		NSTextField* rowLabel = label(names[i], NSMakeRect(28, y + 3, 145, 22));
		[content addSubview:rowLabel];
		[rowLabels addObject:rowLabel];
		const CGFloat fieldWidth = i == 0 ? 300 : (i == 5 ? 110 : 438);
		NSTextField* field = edit(NSMakeRect(178, y, fieldWidth, 25));
		[content addSubview:field];
		[fields addObject:field];
		if (i >= 2) {
			[advancedViews addObject:rowLabel];
			[advancedViews addObject:field];
		}
		if (i >= 1 && i <= 4) {
			NSButton* browse = button(@"选择…", self, @selector(choosePath:),
				NSMakeRect(624, y - 2, 72, 30));
			browse.tag = i;
			[content addSubview:browse];
			[browseButtons addObject:browse];
			if (i >= 2) [advancedViews addObject:browse];
		}
	}
	_address = fields[0]; _upload = fields[1]; _html = fields[2];
	_sqlite = fields[3]; _ffmpeg = fields[4]; _threads = fields[5];
	_configFields = fields;
	_rowLabels = rowLabels;
	_browseButtons = browseButtons;
	_advancedViews = advancedViews;

	_advancedToggle = button(@"▶  高级设置", self, @selector(toggleAdvanced:),
		NSMakeRect(500, 387, 196, 30));
	[content addSubview:_advancedToggle];

	NSBox* statusBox = [[NSBox alloc] initWithFrame:NSMakeRect(24, 75, 672, 86)];
	statusBox.title = @"";
	// NSTextField draws multiline text from the top of its frame. Keep the
	// frame slightly lower so the three status lines are vertically balanced.
	_status = label(@"", NSMakeRect(14, 0, 640, 62));
	_status.maximumNumberOfLines = 3;
	[statusBox addSubview:_status];
	[content addSubview:statusBox];

	_browser = button(@"打开浏览器", self, @selector(openBrowser:), NSMakeRect(24, 24, 112, 32));
	_start = button(@"启动", self, @selector(startService:), NSMakeRect(148, 24, 84, 32));
	_stop = button(@"停止", self, @selector(stopService:), NSMakeRect(244, 24, 84, 32));
	_minimizeButton = button(@"最小化", self, @selector(minimize:),
		NSMakeRect(340, 24, 92, 32));
	_exitButton = button(@"退出", self, @selector(requestQuit:),
		NSMakeRect(444, 24, 84, 32));
	[content addSubview:_browser]; [content addSubview:_start]; [content addSubview:_stop];
	[content addSubview:_minimizeButton]; [content addSubview:_exitButton];
}

- (void)applyLanguage {
	NSString* windowTitle = localized_text(_english,
		@"webcool 控制界面", @"webcool Control Panel");
	_window.title = windowTitle;
	_titleView.stringValue = windowTitle;

	NSArray<NSString*>* names = _english
		? @[@"Listen address", @"File save directory", @"Static resource root",
			@"SQLite library path", @"ffmpeg executable", @"Worker threads"]
		: @[@"监听地址", @"文件保存目录", @"静态资源根目录",
			@"sqlite 动态库路径", @"ffmpeg 可执行文件", @"工作线程数"];
	for (NSUInteger i = 0; i < _rowLabels.count; ++i) {
		_rowLabels[i].stringValue = names[i];
	}
	for (NSButton* browse in _browseButtons) {
		browse.title = localized_text(_english, @"选择…", @"Choose…");
	}
	_advancedToggle.title = _advancedExpanded
		? localized_text(_english, @"▼  收起高级设置", @"▼  Hide Advanced Settings")
		: localized_text(_english, @"▶  高级设置", @"▶  Advanced Settings");
	_browser.title = localized_text(_english, @"打开浏览器", @"Open Browser");
	_start.title = localized_text(_english, @"启动", @"Start");
	_stop.title = localized_text(_english, @"停止", @"Stop");
	_minimizeButton.title = localized_text(_english, @"最小化", @"Minimize");
	_exitButton.title = localized_text(_english, @"退出", @"Exit");

	_aboutMenuItem.title = localized_text(_english, @"关于 webcool", @"About webcool");
	_languageMenuItem.title = localized_text(_english, @"语言", @"Language");
	_showMenuItem.title = localized_text(_english,
		@"显示 webcool 控制界面", @"Show webcool Control Panel");
	_quitMenuItem.title = localized_text(_english, @"退出 webcool", @"Quit webcool");
	_chineseMenuItem.state = _english ? NSControlStateValueOff : NSControlStateValueOn;
	_englishMenuItem.state = _english ? NSControlStateValueOn : NSControlStateValueOff;
	[self updateStatus];
}

- (void)changeLanguage:(NSMenuItem*)sender {
	_english = sender.tag == 1;
	[NSUserDefaults.standardUserDefaults setObject:_english ? @"en" : @"zh"
		forKey:@"ui.language"];
	[self applyLanguage];
}

- (void)loadSettings {
	NSUserDefaults* defaults = NSUserDefaults.standardUserDefaults;
	webcool_options options = _controller->options();
	NSString* address = [defaults stringForKey:@"service.address"];
	if (address.length) options.addr = address.UTF8String;
	NSInteger threads = [defaults integerForKey:@"service.threads"];
	if (threads > 0) options.nthreads = (int) threads;

	NSString* savedUpload = [defaults stringForKey:@"paths.upload"];
	std::string pathError;
	if (savedUpload.length) {
		set_config_text(g_upload_dir, sizeof(g_upload_dir),
			savedUpload.UTF8String ?: "", "file save directory", pathError);
		options.upload_dir_specified = true;
	}
	bool resolvedUploadSpecified = options.upload_dir_specified;
	if (!resolve_upload_dir(options.upload_dir_specified, nullptr,
		resolvedUploadSpecified, pathError)) {
		g_upload_dir[0] = '\0';
	}
	options.upload_dir_specified = resolvedUploadSpecified;
	_controller->configure(options);
	_address.stringValue = ns_text(options.addr.c_str());
	_threads.integerValue = options.nthreads;
	_upload.stringValue = ns_text(g_upload_dir);
	NSString* htmlHome = find_webcool_html_home(
		nonempty_default([defaults stringForKey:@"paths.html"], ns_text(g_html_home)));
	_html.stringValue = htmlHome;
	std::string htmlError;
	set_config_text(g_html_home, sizeof(g_html_home),
		htmlHome.UTF8String ?: "", "static resource root directory", htmlError);

	NSString* sqliteDefault = nonempty_default(ns_text(g_sqlite_lib),
		ns_text(action::choose_sqlite_lib_path().c_str()));
	NSString* sqlitePath = find_webcool_runtime_file(
		nonempty_default([defaults stringForKey:@"paths.sqlite"], sqliteDefault),
		@[@"../../../../third-party/sqlite/lib/sqlite3.so",
		  @"../third-party/sqlite/lib/sqlite3.so"], NO);
	_sqlite.stringValue = sqlitePath;
	set_config_text(g_sqlite_lib, sizeof(g_sqlite_lib),
		sqlitePath.UTF8String ?: "", "sqlite dynamic library path", pathError);

	NSString* ffmpegDefault = nonempty_default(ns_text(g_ffmpeg_path),
		ns_text(action::choose_ffmpeg_path().c_str()));
	NSString* ffmpegPath = find_webcool_runtime_file(
		nonempty_default([defaults stringForKey:@"paths.ffmpeg"], ffmpegDefault),
		@[@"../../../../tools/mac/ffmpeg", @"../tools/mac/ffmpeg"], YES);
	_ffmpeg.stringValue = ffmpegPath;
	set_config_text(g_ffmpeg_path, sizeof(g_ffmpeg_path),
		ffmpegPath.UTF8String ?: "", "ffmpeg executable path", pathError);
	[self setAdvancedExpanded:[defaults boolForKey:@"ui.advancedExpanded"] animated:NO];
}

- (void)saveSettings {
	NSUserDefaults* defaults = NSUserDefaults.standardUserDefaults;
	[defaults setObject:_address.stringValue forKey:@"service.address"];
	[defaults setInteger:_threads.integerValue forKey:@"service.threads"];
	[defaults setObject:_upload.stringValue forKey:@"paths.upload"];
	[defaults setObject:_html.stringValue forKey:@"paths.html"];
	[defaults setObject:_sqlite.stringValue forKey:@"paths.sqlite"];
	[defaults setObject:_ffmpeg.stringValue forKey:@"paths.ffmpeg"];
	[defaults setBool:_advancedExpanded forKey:@"ui.advancedExpanded"];
	[defaults setObject:_english ? @"en" : @"zh" forKey:@"ui.language"];
}

- (void)setAdvancedExpanded:(BOOL)expanded animated:(BOOL)animated {
	_advancedExpanded = expanded;
	const CGFloat contentHeight = expanded ? 477.0 : 330.0;
	const CGFloat oldTop = NSMaxY(_window.frame);
	[_window setContentSize:NSMakeSize(720, contentHeight)];
	NSRect windowFrame = _window.frame;
	windowFrame.origin.y = oldTop - windowFrame.size.height;
	[_window setFrame:windowFrame display:YES animate:animated];

	_titleView.frame = NSMakeRect(24, contentHeight - 45, 320, 28);
	for (NSUInteger i = 0; i < _configFields.count; ++i) {
		const CGFloat y = contentHeight - 88 - i * 43;
		const CGFloat fieldWidth = i == 0 ? 300 : (i == 5 ? 110 : 438);
		_rowLabels[i].frame = NSMakeRect(28, y + 3, 145, 22);
		_configFields[i].frame = NSMakeRect(178, y, fieldWidth, 25);
	}
	for (NSButton* browse in _browseButtons) {
		const CGFloat y = contentHeight - 88 - browse.tag * 43;
		browse.frame = NSMakeRect(624, y - 2, 72, 30);
	}
	_advancedToggle.frame = NSMakeRect(500, contentHeight - 90, 196, 30);
	_advancedToggle.title = expanded
		? localized_text(_english, @"▼  收起高级设置", @"▼  Hide Advanced Settings")
		: localized_text(_english, @"▶  高级设置", @"▶  Advanced Settings");
	for (NSView* view in _advancedViews) view.hidden = !expanded;
}

- (void)toggleAdvanced:(id)sender {
	(void) sender;
	[self setAdvancedExpanded:!_advancedExpanded animated:YES];
	[NSUserDefaults.standardUserDefaults setBool:_advancedExpanded
		forKey:@"ui.advancedExpanded"];
}

- (BOOL)readOptions:(webcool_options&)options error:(std::string&)error {
	const std::string address = utf8_text(_address);
	const std::string upload = utf8_text(_upload);
	std::string html = normalize_static_home_path(utf8_text(_html));
	if (address.empty()) {
		error = localized_utf8(_english, @"监听地址不能为空",
			@"Listen address cannot be empty"); return NO;
	}
	if (upload.empty()) {
		error = localized_utf8(_english, @"文件保存目录不能为空",
			@"File save directory cannot be empty"); return NO;
	}
	if (html.empty() || !readable_regular_file(join_config_path(html, "main.html"))) {
		error = localized_utf8(_english, @"静态资源根目录无效，无法读取 main.html",
			@"Invalid static resource root: main.html is not readable"); return NO;
	}
	const NSInteger threads = _threads.integerValue;
	if (threads <= 0) {
		error = localized_utf8(_english, @"工作线程数必须大于 0",
			@"Worker threads must be greater than 0"); return NO;
	}
	if (!set_config_text(g_upload_dir, sizeof(g_upload_dir), upload,
		"file save directory", error) ||
		!set_config_text(g_html_home, sizeof(g_html_home), html,
		"static resource root directory", error) ||
		!set_config_text(g_sqlite_lib, sizeof(g_sqlite_lib), utf8_text(_sqlite),
		"sqlite dynamic library path", error) ||
		!set_config_text(g_ffmpeg_path, sizeof(g_ffmpeg_path), utf8_text(_ffmpeg),
		"ffmpeg executable path", error)) return NO;
	options.addr = address.c_str();
	options.nthreads = (int) threads;
	_html.stringValue = ns_text(g_html_home);
	return YES;
}

- (void)showError:(NSString*)prefix detail:(const std::string&)detail {
	NSAlert* alert = [[NSAlert alloc] init];
	alert.alertStyle = NSAlertStyleCritical;
	alert.messageText = prefix;
	alert.informativeText = ns_text(detail.c_str());
	[alert beginSheetModalForWindow:_window completionHandler:nil];
}

- (void)startService:(id)sender {
	(void) sender;
	webcool_options options = _controller->options();
	std::string error;
	if (![self readOptions:options error:error]) {
		[self showError:localized_text(_english, @"配置无效", @"Invalid Configuration")
			detail:error]; return;
	}
	_controller->configure(options);
	if (!_controller->start(error)) {
		[self showError:localized_text(_english,
			@"启动 webcool 失败", @"Failed to Start webcool") detail:error];
	}
	[self saveSettings];
	[self updateStatus];
}

- (void)stopService:(id)sender {
	(void) sender;
	_controller->stop();
	[self updateStatus];
}

- (void)openBrowser:(id)sender {
	(void) sender;
	if (!_controller->running()) return;
	NSURL* url = [NSURL URLWithString:browser_url(_controller->options().addr.c_str())];
	if (url) [NSWorkspace.sharedWorkspace openURL:url];
}

- (void)choosePath:(NSButton*)sender {
	NSOpenPanel* panel = [NSOpenPanel openPanel];
	NSArray<NSString*>* chineseTitles = @[@"", @"选择文件保存目录",
		@"选择静态资源根目录", @"选择 sqlite 动态库", @"选择 ffmpeg 可执行文件"];
	NSArray<NSString*>* englishTitles = @[@"", @"Choose File Save Directory",
		@"Choose Static Resource Root", @"Choose SQLite Library",
		@"Choose ffmpeg Executable"];
	if (sender.tag >= 1 && sender.tag <= 4) {
		panel.message = (_english ? englishTitles : chineseTitles)[sender.tag];
	}
	panel.canChooseDirectories = sender.tag == 1 || sender.tag == 2;
	panel.canChooseFiles = !panel.canChooseDirectories;
	panel.allowsMultipleSelection = NO;
	panel.canCreateDirectories = panel.canChooseDirectories;
	if ([panel runModal] != NSModalResponseOK) return;
	NSTextField* target = sender.tag == 1 ? _upload : sender.tag == 2 ? _html :
		sender.tag == 3 ? _sqlite : _ffmpeg;
	target.stringValue = panel.URL.path ?: @"";
}

- (void)updateStatus {
	const BOOL running = _controller->running();
	_status.stringValue = [NSString stringWithFormat:@"%@：%@\n%@：%@\n%@：%@",
		localized_text(_english, @"状态", @"Status"),
		running ? localized_text(_english, @"运行中", @"Running")
			: localized_text(_english, @"已停止", @"Stopped"),
		localized_text(_english, @"监听地址", @"Listen address"),
		ns_text(_controller->options().addr.c_str()),
		localized_text(_english, @"文件保存目录", @"File save directory"),
		ns_text(g_upload_dir)];
	_status.textColor = running ? NSColor.systemGreenColor : NSColor.secondaryLabelColor;
	_start.enabled = !running; _stop.enabled = running; _browser.enabled = running;
	for (NSTextField* field in _configFields) field.enabled = !running;
}

- (void)syncRuntimeStorage:(NSTimer*)timer {
	(void) timer;
	if (!_controller->running()) return;
	const std::string path = action::runtime_upload_dir_get();
	if (!path.empty() && path != g_upload_dir) {
		std::string error;
		if (set_config_text(g_upload_dir, sizeof(g_upload_dir), path,
			"file save directory", error)) {
			_upload.stringValue = ns_text(g_upload_dir);
			[self saveSettings];
			[self updateStatus];
		}
	}
}

- (void)minimize:(id)sender { (void) sender; [_window miniaturize:nil]; }
- (void)showWindow:(id)sender {
	(void) sender; [_window deminiaturize:nil]; [_window makeKeyAndOrderFront:nil];
	[NSApp activateIgnoringOtherApps:YES];
}

- (void)requestQuit:(id)sender {
	(void) sender;
	_quitting = YES;
	[_syncTimer invalidate];
	_controller->stop();
	[self saveSettings];
	[NSApp terminate:nil];
}

- (BOOL)windowShouldClose:(NSWindow*)sender {
	(void) sender;
	if (_quitting) return YES;
	if (_controller->running()) {
		NSAlert* alert = [[NSAlert alloc] init];
		alert.messageText = localized_text(_english,
			@"webcool 正在运行", @"webcool Is Running");
		alert.informativeText = localized_text(_english,
			@"可以保持服务运行并最小化窗口，或停止服务并退出。",
			@"Keep the service running and minimize the window, or stop it and quit.");
		[alert addButtonWithTitle:localized_text(_english, @"最小化", @"Minimize")];
		[alert addButtonWithTitle:localized_text(_english,
			@"停止并退出", @"Stop and Quit")];
		if ([alert runModal] == NSAlertFirstButtonReturn) {
			[_window miniaturize:nil]; return NO;
		}
	}
	[self requestQuit:nil];
	return NO;
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender {
	(void) sender;
	if (!_quitting) {
		_quitting = YES;
		[_syncTimer invalidate];
		_controller->stop();
		[self saveSettings];
	}
	return NSTerminateNow;
}

- (void)applicationWillTerminate:(NSNotification*)notification {
	(void) notification;
	[NSDistributedNotificationCenter.defaultCenter removeObserver:self];
}

@end

int run_mac_control_gui(webcool_controller& controller) {
	@autoreleasepool {
		char lock_path[128];
		snprintf(lock_path, sizeof(lock_path), "/tmp/webcool-control-%u.lock", getuid());
		const int lock_fd = open(lock_path, O_CREAT | O_RDWR, 0600);
		if (lock_fd < 0) return 1;
		if (flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
			NSString* notification = [NSString stringWithFormat:
				@"cn.webcool.control.show.%u", getuid()];
			[NSDistributedNotificationCenter.defaultCenter
				postNotificationName:notification object:nil];
			close(lock_fd);
			return 0;
		}
		NSApplication* app = NSApplication.sharedApplication;
		WebcoolMacDelegate* delegate = [[WebcoolMacDelegate alloc]
			initWithController:&controller];
		app.delegate = delegate;
		[app run];
		flock(lock_fd, LOCK_UN);
		close(lock_fd);
	}
	return 0;
}
