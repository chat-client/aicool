#pragma once

class http_service;

struct webcool_options;

class master_service final : public acl::master_fiber {
public:
	master_service(bool upload_dir_specified);
	~master_service() override = default;

	http_service& get_service() const;

	static bool prepare_runtime(bool& upload_dir_specified);

protected:
	// @override
	void on_accept(acl::socket_stream& conn) override;

	// @override
	void proc_pre_jail() override;

	// @override
	void proc_on_listen(acl::server_socket& ss) override;

	// @override
	void proc_on_init() override;

	// @override
	void proc_on_exit() override;

	// @override
	bool proc_on_sighup(acl::string&) override;

private:
	acl::sslbase_conf* conf_;
	http_service* service_;
	bool upload_dir_specified_;

	static acl::sslbase_io* setup_ssl(acl::socket_stream& conn,
		acl::sslbase_conf& conf);
};

extern char *var_cfg_upload_dir;
extern char *var_cfg_html_home;
extern char *var_cfg_sqlite_lib;
extern char *var_cfg_ffmpeg_path;

extern acl::master_str_tbl var_conf_str_tab[];
extern acl::master_bool_tbl var_conf_bool_tab[];
extern acl::master_int_tbl var_conf_int_tab[];
extern acl::master_int64_tbl var_conf_int64_tab[];
