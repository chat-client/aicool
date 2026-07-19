#pragma once

#include "../actions.h"
#include "../action_util.h"
#ifdef _WIN32
#include "../../platform_compat.h"
#else
#include <dirent.h>
#include <sys/stat.h>
#include <csignal>
#include <strings.h>
#include <unistd.h>
#endif
#include <atomic>
#include <map>
#include <memory>
#include "common/webcool_mutex.h"
#include <set>
#include <vector>

namespace action {

struct transcode_task_t {
	std::string id;
	std::string scope;
	std::string file_name;
	std::string output_name;
	std::string secondary_output_name;
	std::string error;
	std::string message;
	double progress;
	long process_pid;
	bool done;
	bool success;
	bool cancel_requested;
	bool local;
	long long size;
	transcode_task_t()
	: progress(0), process_pid(-1), done(false), success(false)
	, cancel_requested(false), local(false), size(-1) {}
};

struct transcode_task_snapshot_t {
	std::string id;
	std::string scope;
	std::string file_name;
	std::string output_name;
	std::string secondary_output_name;
	std::string error;
	std::string message;
	double progress;
	long process_pid;
	bool done;
	bool success;
	bool cancel_requested;
	bool local;
	long long size;
	transcode_task_snapshot_t()
	: progress(0), process_pid(-1), done(false), success(false)
	, cancel_requested(false), local(false), size(-1) {}
};

struct transcode_strategy_t {
	enum mode_t {
		full_mp4,
		audio_only,
		audio_split,
	} mode;
	transcode_strategy_t() : mode(full_mp4) {}
};

struct ffmpeg_process_t {
#ifdef _WIN32
	HANDLE process;
	HANDLE thread;
	HANDLE read_pipe;
	DWORD pid;
	std::string pending;
	ffmpeg_process_t() : process(nullptr), thread(nullptr), read_pipe(nullptr), pid(0) {}
#else
	ACL_VSTREAM* stream;
	ffmpeg_process_t() : stream(nullptr) {}
#endif
};
using ffmpeg_process_ptr = std::shared_ptr<ffmpeg_process_t>;

extern webcool::mutex g_transcode_mutex;
extern std::map<std::string, std::shared_ptr<transcode_task_t> > g_transcode_tasks;
extern std::map<std::string, std::string> g_running_task_by_file;
extern std::set<std::string> g_active_stream_sidecars;
extern std::atomic<unsigned long> g_transcode_seq;

bool is_video_name(const char* filename);
bool is_local_convertible_video_name(const char* filename);
bool is_audio_split_candidate_video_name(const char* filename);
long long file_size_of(const char* path);
std::string local_parent_path(const std::string& path);
std::string local_join_path(const std::string& parent, const char* name);
std::string local_base_name(const std::string& path);
std::string local_file_lock_key(const std::string& path);
bool ensure_video_transcode_lock_policy(const std::string& upload_dir,
	const std::string& file_lock_key, std::string& err, int& status);
bool ensure_remote_video_transcode_lock_policy(const std::string& upload_dir,
	const std::string& file_path, std::string& err, int& status);
bool ensure_local_video_transcode_lock_policy(const std::string& upload_dir,
	const std::string& local_path, std::string& err, int& status);
bool normalize_local_video_path(const char* input, std::string& path,
	std::string& err);
std::string local_stream_state_path(const std::string& local_path);
std::string local_stream_tmp_mp4_path(const std::string& local_path);
long long read_local_stream_position_ms(const std::string& local_path);
bool write_local_stream_position_ms(const std::string& local_path,
	long long position_ms, std::string& err);
void remove_local_stream_position(const std::string& local_path);

std::string shell_quote(const std::string& s);
int run_command_capture_in_thread(const std::string& command, std::string& output);
int run_program_capture_in_thread(const std::string& program,
	const std::vector<std::string>& args, std::string& output);
std::string trim_text(const std::string& s);
long long parse_duration_ms_from_text(const std::string& text);
long long probe_duration_ms_in_thread(const std::string& ffmpeg,
	const std::string& input_file);
long long parse_progress_ms_line(const std::string& line);

bool is_browser_friendly_h264_video_line(const char* line);
bool extract_primary_video_stream_line(const char* text, std::string& line);
bool browser_can_play_video_by_probe(const std::string& ffmpeg,
	const std::string& input_file, std::string& reason);
bool probe_transcode_strategy(const std::string& ffmpeg,
	const std::string& input_file, transcode_strategy_t& strategy,
	bool allow_audio_split);
bool probe_has_subtitle_stream_in_thread(const std::string& ffmpeg,
	const std::string& input_file);
std::string replace_ext(const std::string& name, const char* new_ext);
int export_vtt_sidecar_in_thread(const std::string& ffmpeg,
	const std::string& input_file, const std::string& output_file,
	std::string& vtt_file, std::string& err);

std::string make_task_id();
void update_task_progress(const std::shared_ptr<transcode_task_t>& task,
	double percent, const char* msg);
void set_task_process_pid(const std::shared_ptr<transcode_task_t>& task,
	long process_pid);
bool is_task_cancel_requested(const std::shared_ptr<transcode_task_t>& task);
std::string scoped_task_key(const std::string& scope,
	const std::string& file_name);
void finish_task(const std::shared_ptr<transcode_task_t>& task,
	bool success, const char* msg, const char* err, long long size);
bool snapshot_task_by_id(const char* task_id, const std::string& scope,
	transcode_task_snapshot_t& snapshot);
void snapshot_running_tasks(const std::string& scope,
	std::vector<transcode_task_snapshot_t>& out);
bool request_cancel_task(const char* task_id, const std::string& scope,
	transcode_task_snapshot_t& snapshot, bool& signal_sent);
void terminate_running_transcode_processes();

int wait_transcode_progress_in_thread(const std::shared_ptr<transcode_task_t>& task,
	ffmpeg_process_t& proc, long long duration_ms,
	double start_percent, double progress_span,
	const char* progress_msg, double end_percent,
	const char* end_msg);
int wait_transcode_progress(const std::shared_ptr<transcode_task_t>& task,
	ffmpeg_process_t& proc, long long duration_ms,
	double start_percent, double progress_span,
	const char* progress_msg, double end_percent,
	const char* end_msg);
ffmpeg_process_ptr start_ffmpeg_process_in_thread(ACL_ARGV* args);

void run_video_transcode_task(const std::shared_ptr<transcode_task_t>& task,
	const std::string& ffmpeg, const std::string& input_file,
	const std::string& tmp_file, const std::string& output_file);
void run_transcode_task_in_thread(const std::shared_ptr<transcode_task_t>& task,
	const std::string& ffmpeg, const std::string& input_file,
	const std::string& tmp_file, const std::string& output_file,
	const std::string& secondary_output_file,
	const transcode_strategy_t& strategy);

std::string replace_ext_with_mp4(const std::string& name);
bool path_exists(const std::string& path);
std::string make_unique_transcoded_name(const std::string& upload_dir,
	const std::string& input_name);
std::string make_unique_audio_only_name(const std::string& upload_dir,
	const std::string& input_name);
void make_unique_split_output_names(const std::string& upload_dir,
	const std::string& input_name, std::string& video_name,
	std::string& audio_name);
std::string make_unique_local_transcoded_path(const std::string& input_path);
bool send_existing_local_mp4(const std::string& path, response_t& res);

bool is_active_stream_sidecar(const std::string& path);
void register_stream_sidecar(const std::string& path);
void unregister_stream_sidecar(const std::string& path);
void cleanup_local_stream_sidecars(const std::string& parent);
void cleanup_current_stream_sidecars(const std::string& tmp_path,
	const std::string& progress_path, bool keep_files);
bool remux_mp4_faststart(const std::string& ffmpeg,
	const std::string& input_path, const std::string& output_path,
	std::string& err);
void update_stream_task_progress_from_file(
	const std::shared_ptr<transcode_task_t>& task,
	const std::string& progress_path, long long duration_ms);

bool validate_local_stream_state_request(const request_t& req,
	const std::string& upload_dir, std::string& local_path,
	std::string& err, int& status);

#if 0
inline void json_error(response_t& res, int status, const char* msg,
	bool keep_alive)
{
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", false);
	root.add_text("error", msg);
	sendJson(res, status, root, keep_alive);
}
#endif

} // namespace action
