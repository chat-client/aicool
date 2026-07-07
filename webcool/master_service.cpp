#include "stdafx.h"
#include "http_service.h"
#include "http_servlet.h"
#include "action/action_util.h"
#include "platform_compat.h"
#include "config.h"
#include "master_service.h"

#ifndef PATH_MAX
# define PATH_MAX 4096
#endif

char *var_cfg_libcrypto_path;	// For OpenSSL, MbedTLS
char *var_cfg_libx509_path;	// For MbedTLS
char *var_cfg_libssl_path;	// For OpenSSL, MbedTLS, and PolarSSL
char *var_cfg_crt_file;		// For OpenSSL, MbedTLS, and PolarSSL
char *var_cfg_key_file;		// For OpenSSL, MbedTLS, and PolarSSL
char *var_cfg_key_pass;		// For OpenSSL, MbedTLS, and PolarSSL

char *var_cfg_upload_dir;
char *var_cfg_html_home;
char *var_cfg_sqlite_lib;
char *var_cfg_ffmpeg_path;

acl::master_str_tbl var_conf_str_tab[] = {
	{ "libcrypto_path",	"",	&var_cfg_libcrypto_path	},
	{ "libx509_path",	"",	&var_cfg_libx509_path	},
	{ "libssl_path",	"",	&var_cfg_libssl_path	},
	{ "crt_file",		"",	&var_cfg_crt_file	},
	{ "key_file",		"",	&var_cfg_key_file	},
	{ "key_pass",		"",	&var_cfg_key_pass	},
	{ "upload_dir",    "",  &var_cfg_upload_dir },
	{ "html_home",     "",  &var_cfg_html_home },
	{ "lib_sqlite",    "",  &var_cfg_sqlite_lib },
	{ "ffmpeg_path",   "",  &var_cfg_ffmpeg_path },

	{ nullptr, nullptr, nullptr }
};

int   var_cfg_ssl_session_cache;

acl::master_bool_tbl var_conf_bool_tab[] = {
	{ "ssl_session_cache",	1,	&var_cfg_ssl_session_cache },

	{ nullptr, 0, nullptr }
};

static int  var_cfg_io_timeout;

acl::master_int_tbl var_conf_int_tab[] = {
	{ "io_timeout",		0,	&var_cfg_io_timeout, 0, 0 },

	{ nullptr, 0 , nullptr , 0, 0 }
};

acl::master_int64_tbl var_conf_int64_tab[] = {
	{ nullptr, 0 , nullptr , 0, 0 }
};

//////////////////////////////////////////////////////////////////////////

master_service::master_service(bool upload_dir_specified)
: conf_(nullptr)
, upload_dir_specified_(upload_dir_specified)
{
	service_ = new http_service;
}

http_service& master_service::get_service() const {
	return *service_;
}

bool master_service::prepare_runtime(bool& upload_dir_specified) {
	std::string resolve_err;
	if (!resolve_upload_dir(upload_dir_specified, var_cfg_upload_dir,
		upload_dir_specified, resolve_err)) {
		logger_error("resolve upload dir error=%s", resolve_err.c_str());
		return false;
	}

	if (!action::make_dir_recursive(g_upload_dir)) {
		logger_error("create dirs error=%s, dir=%s", acl::last_serror(), g_upload_dir);
		return false;
	}

	char resolved_upload_dir[PATH_MAX];
	if (realpath(g_upload_dir, resolved_upload_dir) == nullptr) {
		logger_error("realpath error=%s, dir=%s", acl::last_serror(), g_upload_dir);
		return false;
	}

	action::runtime_upload_dir_init(g_upload_dir);

	std::string err;

	if (!action::storage_prepare_startup_primary(g_upload_dir,
		upload_dir_specified, err))
	{
		logger_error("storage_prepare_startup_primary error=%s, dir=%s",
			err.c_str(), g_upload_dir);
		return false;
	}
	logger("upload dir=%s", g_upload_dir);

	if (g_html_home[0] != '\0') {
		std::string html_home = normalize_static_home_path(g_html_home);
		const std::string index_html = join_config_path(html_home, "main.html");
		if (!readable_regular_file(index_html)) {
			logger_error("read static resource error=%s, html=%s",
				acl::last_serror(), html_home.c_str());
			return false;
		}
		if (!set_config_text(g_html_home, sizeof(g_html_home),
			html_home, "static resource root directory", err))
		{
			logger_error("set html home error=%s, html=%s",
				err.c_str(), html_home.c_str());
			return false;
		}
		action::IndexAction::set_static_home_path(g_html_home);
	}

	if (g_ffmpeg_path[0] != '\0') {
		action::runtime_ffmpeg_path_set(g_ffmpeg_path);
	}

	////////////////////////////////////////////////////////////////////////////

	if (g_sqlite_lib[0] != '\0') {
		action::runtime_sqlite_lib_set(g_sqlite_lib);
	}

	const std::string sqlite_path = action::choose_sqlite_lib_path();
	if (sqlite_path.empty()) {
		logger_error("sqlite dynamic library not found, path=%s", g_sqlite_lib);
		return false;
	}
	acl::db_handle::set_loadpath(sqlite_path.c_str());

	std::string db_err;
	if (!action::init_video_resume_db(g_upload_dir, db_err)) {
		logger_error("init sqlite db error=%s, db path=%s",
			db_err.c_str(), g_upload_dir);
		return false;
	}
	if (!action::init_category_folder_db(g_upload_dir, db_err)) {
		logger_error("init_category_folder_db error=%s, dir=%s",
			err.c_str(), g_upload_dir);
		return false;
	}
	if (!action::init_recycle_bin_db(g_upload_dir, db_err)) {
		logger_error("init recycle db error=%s, dir=%s",
			db_err.c_str(), g_upload_dir);
		return false;
	}

	logger("OK, the upload path: %s\n", g_upload_dir);
	return true;
}

acl::sslbase_io* master_service::setup_ssl(acl::socket_stream& conn,
	  acl::sslbase_conf& conf) {
	auto* hook = dynamic_cast<acl::sslbase_io *>(conn.get_hook());
	if (hook != nullptr) {
		return hook;
	}

	//logger("begin setup ssl hook...");

	acl::sslbase_io* ssl = conf.create(false);
	if (conn.setup_hook(ssl) == ssl) {
		logger_error("setup_hook error!");
		ssl->destroy();
		return nullptr;
	}

	if (!ssl->handshake()) {
		logger_error("ssl handshake failed");
		ssl->destroy();
		return nullptr;
	}

	if (!ssl->handshake_ok()) {
		logger("handshake trying again...");
		ssl->destroy();
		return nullptr;
	}

	logger("handshake_ok");

	return ssl;
}

void master_service::on_accept(acl::socket_stream& conn) {
	logger("connect from %s, fd %d", conn.get_peer(), conn.sock_handle());

	if (conf_) {
		acl::sslbase_io* ssl = setup_ssl(conn, *conf_);
		if (ssl == nullptr) {
			return;
		}
	}

	const int rw_timeout = connection_rw_timeout(var_cfg_io_timeout);
	conn.set_rw_timeout(rw_timeout);

	acl::memcache_session session("127.0.0.1:11211");
	http_servlet servlet(*service_, &conn, &session);

	// charset: big5, gb2312, gb18030, gbk, utf-8
	servlet.setLocalCharset("utf-8");
	servlet.setRwTimeout(rw_timeout);

	while(servlet.doRun()) {}

	logger("disconnect from %s, method=%s, url=%s, keep-alive=%s",
		conn.get_peer(), servlet.get_method().c_str(),
		servlet.get_url().c_str(), servlet.isKeepAlive() ? "true" : "false");
}

void master_service::proc_pre_jail() {
	logger(">>>proc_pre_jail<<<");
}

void master_service::proc_on_listen(acl::server_socket& ss) {
	logger(">>>listen %s ok<<<", ss.get_addr());
}

void master_service::proc_on_init() {
	if (g_html_home[0] == '\0' && *var_cfg_html_home) {
		ACL_SAFE_STRCPY(g_html_home, var_cfg_html_home);
	}

	if (g_sqlite_lib[0] == '\0' && *var_cfg_sqlite_lib) {
		ACL_SAFE_STRCPY(g_sqlite_lib, var_cfg_sqlite_lib);
	}

	if (g_ffmpeg_path[0] == '\0' && *var_cfg_ffmpeg_path) {
		ACL_SAFE_STRCPY(g_ffmpeg_path, var_cfg_ffmpeg_path);
	}

	if (!prepare_runtime(upload_dir_specified_)) {
		logger_error("prepare_runtime error");
		exit(1);
	}

	///////////////////////////////////////////////////////////////////////////

	if (var_cfg_crt_file == nullptr || *var_cfg_crt_file == 0
		|| var_cfg_key_file == nullptr || *var_cfg_key_file == 0) {
		logger("not use SSL mode");
		return;
	}

	if (strstr(var_cfg_libssl_path, "mbedtls")) {
		acl::mbedtls_conf::set_libpath(var_cfg_libcrypto_path,
			var_cfg_libx509_path, var_cfg_libssl_path);
		if (!acl::mbedtls_conf::load()) {
			logger_error("load %s error", var_cfg_libssl_path);
			return;
		}

		logger("MbedTLS loaded, crypto=%s, x509=%s, ssl=%s",
			var_cfg_libcrypto_path, var_cfg_libx509_path,
			var_cfg_libssl_path);

		conf_ = new acl::mbedtls_conf(true);
	} else if (strstr(var_cfg_libssl_path, "polarssl")) {
		acl::polarssl_conf::set_libpath(var_cfg_libssl_path);
		if (!acl::polarssl_conf::load()) {
			logger_error("load %s error", var_cfg_libssl_path);
			return;
		}

		logger("PolarSSL loaded, ssl=%s", var_cfg_libssl_path);

		conf_ = new acl::polarssl_conf();
	} else if (strstr(var_cfg_libssl_path, "libssl")) {
		acl::openssl_conf::set_libpath(var_cfg_libcrypto_path,
			var_cfg_libssl_path);
		if (!acl::openssl_conf::load()) {
			logger_error("load %s error", var_cfg_libssl_path);
			return;
		}

		logger("OpenSSL loaded, crypto=%s, ssl=%s",
			var_cfg_libcrypto_path, var_cfg_libssl_path);

		conf_ = new acl::openssl_conf(true);
	} else {
		logger("unsupported ssl=%s", var_cfg_libssl_path);
		return;
	}

	conf_->enable_cache(var_cfg_ssl_session_cache);

	if (!conf_->add_cert(var_cfg_crt_file, var_cfg_key_file,
			var_cfg_key_pass)) {

		logger_error("add cert failed, crt: %s, key: %s",
			var_cfg_crt_file, var_cfg_key_file);
		delete conf_;
		conf_ = nullptr;
		return;
	}

	logger("load cert ok, crt: %s, key: %s", var_cfg_crt_file, var_cfg_key_file);
}

void master_service::proc_on_exit() {
	logger(">>>proc_on_exit<<<");

	delete conf_;
	delete service_;
}

bool master_service::proc_on_sighup(acl::string&) {
	logger(">>>proc_on_sighup<<<");
	return true;
}
