#include "stdafx.h"
#include <csignal>
#include "action/action_util.h"
#include "action/convert/convert_common.h"
#include "win32/win32_gui.h"
#include "win32/webcool_controller.h"
#ifdef MACOSX
#include "mac/mac_gui.h"
#endif
#include "config.h"
#include "platform_compat.h"
#include "http_router.h"
#include "master_service.h"

static auto g_webcool_version = "2.0.1";

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
	printf("  CodeFormer目录: %s\n", g_codeformer_dir[0] ? g_codeformer_dir : "(自动检测)");
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

static bool validate_codeformer_dir(std::string& err) {
	if (!g_codeformer_dir[0]) return true;
	std::string root = g_codeformer_dir;
	const std::string nested = join_config_path(root, "codeformer");
	if (readable_regular_file(join_config_path(
		join_config_path(nested, "CodeFormer"), "inference_codeformer.py"))) {
		root = nested;
		if (!set_config_text(g_codeformer_dir, sizeof(g_codeformer_dir), root,
			"CodeFormer runtime directory", err)) return false;
	}
	const std::string repository = join_config_path(root, "CodeFormer");
	const std::string weights = join_config_path(repository, "weights");
	const std::string codeformer_weights = join_config_path(weights, "CodeFormer");
	const std::string facelib_weights = join_config_path(weights, "facelib");
	const std::vector<std::string> required = {
		join_config_path(join_config_path(join_config_path(root, "venv"), "bin"), "python3"),
		join_config_path(repository, "inference_codeformer.py"),
		join_config_path(repository, "inference_inpainting.py"),
		join_config_path(codeformer_weights, "codeformer.pth"),
		join_config_path(codeformer_weights, "codeformer_inpainting.pth"),
		join_config_path(facelib_weights, "detection_Resnet50_Final.pth"),
		join_config_path(facelib_weights, "parsing_parsenet.pth")
	};
	for (size_t i = 0; i < required.size(); ++i) {
		if (access(required[i].c_str(), i == 0 ? X_OK : R_OK) != 0) {
			err = "CodeFormer runtime is incomplete; missing: " + required[i];
			return false;
		}
	}
	return true;
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
		"  -K codeformer_dir\n"
		"                  CodeFormer运行目录，需包含 venv/ 和 CodeFormer/\n"
		"                  也可写为 --codeformer-dir codeformer_dir\n"
		"  -T threads      工作线程数 (默认 2)\n"
		"  -r rw_timeout   读写超时秒数 (默认 0=无超时)\n"
		"  -z stack_size   协程栈大小 (默认 %zu，最小 %zu)\n"
#ifdef _WIN32
		"  -e event_type   事件引擎: kernel|poll|select (默认 poll)\n"
		"  -C              以命令行终端模式运行\n"
		"  -G              打开图形控制界面 (Windows/macOS 默认)\n",
#elif defined(MACOSX)
		"  -e event_type   事件引擎: kernel|poll|select (默认 kernel)\n"
		"  -C              以命令行终端模式运行\n"
		"  -G              打开图形控制界面 (Windows/macOS 默认)\n",
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
#if defined(_WIN32) || defined(MACOSX)
	bool gui_mode = true;
#endif
	int  ch;
	bool upload_dir_specified = false;
	std::string config_err, conf;

	static char codeformer_short_option[] = "-K";
	for (int i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "--codeformer-dir") == 0) argv[i] = codeformer_short_option;
	}

	while ((ch = acl_getopt(argc, argv, "hvVDGCs:d:w:S:F:K:f:T:r:z:e:")) > 0) {
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
#ifdef MACOSX
			// Preserve the historical `webcool -D` headless behavior on macOS.
			gui_mode = false;
#endif
			break;
		case 'G':
#if defined(_WIN32) || defined(MACOSX)
			gui_mode = true;
#endif
			break;
		case 'C':
#if defined(_WIN32) || defined(MACOSX)
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
		case 'K':
			if (!set_config_text(g_codeformer_dir, sizeof(g_codeformer_dir),
				  acl_optarg ? acl_optarg : "", "CodeFormer runtime directory", config_err)) {
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
	if (!validate_codeformer_dir(config_err)) {
		fprintf(stderr, "%s\n", config_err.c_str());
		return 1;
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
		action::terminate_running_transcode_processes();
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
#ifdef MACOSX
	const char* gui_master_log = acl_getenv("MASTER_LOG");
	if (gui_mode && !(gui_master_log && *gui_master_log)) {
		webcool_options service_options;
		service_options.addr = addr;
		service_options.nthreads = nthreads;
		service_options.upload_dir_specified = upload_dir_specified;
		service_options.service = &service;
		webcool_controller controller(service_options);
		return run_mac_control_gui(controller);
	}
#endif
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
	action::terminate_running_transcode_processes();
#endif

	return 0;
}
