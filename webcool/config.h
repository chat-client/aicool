#pragma once
#include <atomic>
#include <string>

extern size_t                g_stack_size;
extern int                   g_rw_timeout;
extern acl::fiber_event_t    g_event_type;
extern char                  g_upload_dir[4096];
extern char                  g_html_home[4096];
extern char                  g_sqlite_lib[4096];
extern char                  g_ffmpeg_path[4096];
extern std::atomic<bool>     g_service_stopping;

size_t default_fiber_stack_size();
size_t minimum_fiber_stack_size();
size_t normalize_fiber_stack_size(size_t stack_size);
bool set_config_text(char* dst, size_t dst_size,
	const std::string& value, const char* label, std::string& err);
std::string join_config_path(const std::string& parent, const char* name);
bool read_primary_storage_path(std::string& path, std::string& err);
bool write_primary_storage_path(const std::string& canonical_path, std::string& err);
int connection_rw_timeout(int cfg_io_timeout);
bool resolve_upload_dir(bool cli_specified, const char* cfg_upload_dir,
	bool& upload_dir_specified, std::string& err);
bool readable_regular_file(const std::string& path);
std::string normalize_static_home_path(const std::string& path);
