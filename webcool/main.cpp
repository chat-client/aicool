#include "stdafx.h"
#include <csignal>
#include "action/action_util.h"
#include "win32/win32_gui.h"
#include "win32/webcool_controller.h"
#include "config.h"
#include "platform_compat.h"
#include "http_router.h"
#include "master_service.h"

static auto g_webcool_version = "1.6.4";

static const char* event_type_name(acl::fiber_event_t event_type) {
	switch (event_type) {
	case acl::FIBER_EVENT_T_POLL:
		return "poll";
	case acl::FIBER_EVENT_T_SELECT:
		return "select";
	default:
		return "kernel";
	}
}

static void print_detail_info(const acl::string& addr,
	  int nthreads, bool daemon_mode) {
	std::string sqlite_path;
	std::string ffmpeg_path;
	if (g_sqlite_lib[0] != '\0') {
		sqlite_path = g_sqlite_lib;
	} else {
		sqlite_path = action::choose_sqlite_lib_path();
	}

	if (g_ffmpeg_path[0] != '\0') {
		ffmpeg_path = g_ffmpeg_path;
	} else {
		ffmpeg_path = action::choose_ffmpeg_path();
	}

	printf("webcool 详细信息\n");
	printf("  版本号: %s\n", g_webcool_version);
	printf("  构建时间: %s %s\n", __DATE__, __TIME__);
	printf("  平台: %s\n",
#ifdef _WIN32
		"Windows"
#elif defined(MACOSX)
		"macOS"
#else
		"Linux/Unix"
#endif
	);
	printf("  监听地址: %s\n", addr.c_str());
	printf("  存储路径: %s\n", g_upload_dir);
	printf("  sqlite.so路径: %s\n", sqlite_path.empty() ? "(未找到)" : sqlite_path.c_str());
	printf("  ffmpeg路径: %s\n", ffmpeg_path.empty() ? "(未找到)" : ffmpeg_path.c_str());
	printf("  工作线程: %d\n", nthreads);
	printf("  后台模式: %s\n", daemon_mode ? "on" : "off");
	printf("  读写超时(秒): %d\n", g_rw_timeout);
	printf("  协程栈大小: %zu\n", g_stack_size);
	printf("  事件引擎: %s\n", event_type_name(g_event_type));
}

static bool daemonize_process() {
#ifdef _WIN32
	return false;
#else
	pid_t pid = fork();
	if (pid < 0) {
		return false;
	}
	if (pid > 0) {
		exit(0);
	}

	if (setsid() < 0) {
		return false;
	}

	signal(SIGHUP, SIG_IGN);

	pid = fork();
	if (pid < 0) {
		return false;
	}
	if (pid > 0) {
		exit(0);
	}

#if 0
	int fd = open("/dev/null", O_RDWR);
	if (fd < 0) {
		return false;
	}

	if (dup2(fd, STDIN_FILENO) < 0 ||
		dup2(fd, STDOUT_FILENO) < 0 ||
		dup2(fd, STDERR_FILENO) < 0) {
		if (fd > STDERR_FILENO) {
			close(fd);
		}
		return false;
	}

	if (fd > STDERR_FILENO) {
		close(fd);
	}
#endif

	return true;
#endif
}

// ──────────────────────────────────────
// 用法帮助
// ──────────────────────────────────────
static void usage(const char* prog) {
	printf(
		"用法: %s [选项]\n"
		"  -h              显示帮助\n"
		"  -v              显示版本号\n"
		"  -V              显示详细信息\n"
		"  -f configure    配置文件\n"
		"  -D              以后台服务(守护进程)方式启动\n"
		"  -s addr         监听地址 (默认 0.0.0.0:8080)\n"
		"  -d upload_dir   上传文件保存目录\n"
		"                  优先级: $HOME/.webcool/primary_storage.path > -d > 配置文件 upload_dir\n"
		"                  (均未指定时 macOS 默认 ~/Library/Application Support/webcool/data, 其他平台 ./uploads)\n"
		"  -w html_home     静态资源根目录 (默认 ./html)\n"
		"  -S sqlite_lib   sqlite 动态库路径 (例如 /usr/local/lib/sqlite3.so)\n"
		"  -F ffmpeg_path  ffmpeg 可执行文件路径 (例如 /opt/webcool/bin/ffmpeg)\n"
		"  -T threads      工作线程数 (默认 2)\n"
		"  -r rw_timeout   读写超时秒数 (默认 0=无超时)\n"
		"  -z stack_size   协程栈大小 (默认 %zu，最小 %zu)\n"
#ifdef _WIN32
		"  -e event_type   事件引擎: kernel|poll|select (默认 poll)\n"
		"  -C              进入 DOS 终端模式\n"
		"  -G              打开 Windows 控制界面 (Windows 默认)\n",
#else
		"  -e event_type   事件引擎: kernel|poll|select (默认 kernel)\n",
#endif
		prog, default_fiber_stack_size(), minimum_fiber_stack_size());
}

// ───────────────────────────────────────
// main
// ───────────────────────────────────────
int main(int argc, char* argv[]) {
#ifdef _WIN32
	SetConsoleCP(CP_UTF8);
	SetConsoleOutputCP(CP_UTF8);

	int utf8_argc = 0;
	std::vector<std::string> utf8_args;
	std::vector<char*> utf8_argv;
	LPWSTR* wide_argv = CommandLineToArgvW(GetCommandLineW(), &utf8_argc);
	if (wide_argv != NULL) {
		utf8_args.reserve((size_t) utf8_argc);
		utf8_argv.reserve((size_t) utf8_argc + 1);
		for (int i = 0; i < utf8_argc; ++i) {
			std::string text;
			if (webcool_wide_to_utf8(wide_argv[i], text)) {
				utf8_args.push_back(text);
			} else {
				utf8_args.push_back("");
			}
		}
		LocalFree(wide_argv);
		for (size_t i = 0; i < utf8_args.size(); ++i) {
			utf8_argv.push_back(&utf8_args[i][0]);
		}
		utf8_argv.push_back(NULL);
		argc = (int) utf8_args.size();
		argv = utf8_argv.data();
	}
#endif

	acl::string addr("0.0.0.0:8080");
	int  nthreads    = 2;
	bool daemon_mode = false;
	bool show_version = false;
	bool show_detail = false;
#ifdef _WIN32
	bool gui_mode = true;
#endif
	int  ch;
	bool upload_dir_specified = false;
	std::string config_err, conf;

	while ((ch = acl_getopt(argc, argv, "hvVDGCs:d:w:S:F:f:T:r:z:e:")) > 0) {
		switch (ch) {
		case 'h':
			usage(argv[0]);
			return 0;
		case 'v':
			show_version = true;
			break;
		case 'V':
			show_detail = true;
			break;
		case 'D':
			daemon_mode = true;
			break;
		case 'G':
#ifdef _WIN32
			gui_mode = true;
#endif
			break;
		case 'C':
#ifdef _WIN32
			gui_mode = false;
#endif
			break;
		case 's':
			addr = acl_optarg;
			break;
		case 'd':
			if (!set_config_text(g_upload_dir, sizeof(g_upload_dir),
				  acl_optarg ? acl_optarg : "", "file save directory", config_err)) {
				fprintf(stderr, "%s\n", config_err.c_str());
				return 1;
			}
			upload_dir_specified = true;
			break;
		case 'w':
			if (!set_config_text(g_html_home, sizeof(g_html_home),
				  acl_optarg ? acl_optarg : "",
				  "static resource root directory", config_err)) {
				fprintf(stderr, "%s\n", config_err.c_str());
				return 1;
			}
			break;
		case 'S':
			if (!set_config_text(g_sqlite_lib, sizeof(g_sqlite_lib),
				  acl_optarg ? acl_optarg : "", "sqlite dynamic library path", config_err)) {
				fprintf(stderr, "%s\n", config_err.c_str());
				return 1;
			}
			break;
		case 'F':
			if (!set_config_text(g_ffmpeg_path, sizeof(g_ffmpeg_path),
				  acl_optarg ? acl_optarg : "", "ffmpeg executable path", config_err)) {
				fprintf(stderr, "%s\n", config_err.c_str());
				return 1;
			}
			break;
		case 'f':
			conf = acl_optarg ? acl_optarg : "";
			break;
		case 'T':
			nthreads = atoi(acl_optarg);
			break;
		case 'r':
			g_rw_timeout = atoi(acl_optarg);
			break;
		case 'z':
			g_stack_size = normalize_fiber_stack_size(
				acl_optarg ? (size_t) strtoull(acl_optarg, NULL, 10) : 0);
			break;
		case 'e':
			if (strcasecmp(acl_optarg, "poll") == 0) {
				g_event_type = acl::FIBER_EVENT_T_POLL;
			} else if (strcasecmp(acl_optarg, "select") == 0) {
				g_event_type = acl::FIBER_EVENT_T_SELECT;
			} else {
				g_event_type = acl::FIBER_EVENT_T_KERNEL;
			}
			break;
		default:
			break;
		}
	}

	acl::acl_cpp_init();

	if (show_detail) {
		std::string resolve_err;
		if (!resolve_upload_dir(upload_dir_specified, nullptr,
			upload_dir_specified, resolve_err)) {
			fprintf(stderr, "%s\n", resolve_err.c_str());
			return 1;
		}
		print_detail_info(addr, nthreads, daemon_mode);
		return 0;
	}

	if (show_version) {
		printf("%s\n", g_webcool_version);
		return 0;
	}

	master_service ms(upload_dir_specified);
	http_service& service = ms.get_service();
	ms.set_cfg_int(var_conf_int_tab)
		.set_cfg_int64(var_conf_int64_tab)
		.set_cfg_str(var_conf_str_tab)
		.set_cfg_bool(var_conf_bool_tab);

	const http_router router(service);
	router.setup();

#ifdef _WIN32
	if (!gui_mode) {
		acl::log::stdout_open(true);
		ensure_console_for_cli();
		ms.run_alone(addr, conf.empty() ? nullptr : conf.c_str());
		return 0;
	}

	webcool_options service_options;
	service_options.addr = addr;
	service_options.nthreads = nthreads;
	service_options.upload_dir_specified = upload_dir_specified;
	service_options.service = &service;
	webcool_controller controller(service_options);

	return run_windows_control_gui(controller);
#else
	// Check if be managed by acl_master
	const char *master_log = acl_getenv("MASTER_LOG");
	if (master_log && *master_log) {
		acl_getopt_init();
		ms.run_daemon(argc, argv);
	} else {
		if (daemon_mode) {
			if (!daemonize_process()) {
				fprintf(stderr, "切换到后台模式失败\n");
				return 1;
			}
		} else {
			acl::log::stdout_open(true);
		}
		ms.run_alone(addr, conf.empty() ? nullptr : conf.c_str());
	}
#endif

	return 0;
}
