#include "stdafx.h"
#include "admin_internal.h"
#include "../../config.h"

#ifdef _WIN32
#include "../../platform_compat.h"
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <cerrno>

#include <fstream>

namespace action {
namespace admin_internal {

webcool::mutex g_runtime_upload_mutex;
std::string g_runtime_upload_dir;
webcool::mutex g_settings_mutex;

// 创建管理设置的默认值。
webcool_settings_t default_settings()
{
	webcool_settings_t settings;
	settings.local_disk_admin = true;
	settings.local_disk_user = true;
	settings.backup_upload_auto_sync = true;
	settings.backup_paths.clear();
	return settings;
}

#if 0
// 生成统一格式的 JSON 错误响应。
void json_error(response_t& res, int status, const char* msg, bool keep_alive)
{
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", false);
	root.add_text("error", msg ? msg : "unknown error");
	sendJson(res, status, root, keep_alive);
}
#endif

// 解析已存在路径的真实路径，失败时返回空字符串。
std::string real_path_or_empty(const std::string& input, std::string& err)
{
	std::string real_path;
	if (!resolve_existing_real_path(input, real_path, err)) {
		return "";
	}
	return real_path;
}

// 使用 realpath 解析已存在路径，得到系统真实路径。
bool resolve_existing_real_path(const std::string& input,
	std::string& out, std::string& err)
{
	char resolved[PATH_MAX];
	if (realpath(input.c_str(), resolved) == nullptr) {
		err = strerror(errno);
		return false;
	}
	out = resolved;
	return true;
}

// 判断存储路径是否为平台绝对路径。
bool is_absolute_storage_path(const std::string& path)
{
#ifdef _WIN32
	return (path.size() >= 3
			&& ((path[0] >= 'A' && path[0] <= 'Z')
				|| (path[0] >= 'a' && path[0] <= 'z'))
			&& path[1] == ':'
			&& (path[2] == '/' || path[2] == '\\'))
		|| (path.size() >= 2
			&& (path[0] == '/' || path[0] == '\\')
			&& (path[1] == '/' || path[1] == '\\'));
#else
	return !path.empty() && path[0] == '/';
#endif
}

// 确保存储目标路径存在，并返回其真实路径。
bool ensure_storage_target_path(const std::string& input,
	std::string& out, std::string& err)
{
	if (!is_absolute_storage_path(input)) {
		err = "target path must be absolute";
		return false;
	}
	if (!make_dir_recursive(input.c_str())) {
		err = "cannot create target storage path";
		return false;
	}
	return resolve_existing_real_path(input, out, err);
}

// 判断 candidate 是否与 base 相同，或位于 base 子目录下。
bool is_same_or_child_path(const std::string& base,
	const std::string& candidate)
{
#ifdef _WIN32
	std::string left = base;
	std::string right = candidate;
	for (size_t i = 0; i < left.size(); ++i) {
		if (left[i] == '\\') {
			left[i] = '/';
		} else if (left[i] >= 'A' && left[i] <= 'Z') {
			left[i] = (char) (left[i] - 'A' + 'a');
		}
	}
	for (size_t i = 0; i < right.size(); ++i) {
		if (right[i] == '\\') {
			right[i] = '/';
		} else if (right[i] >= 'A' && right[i] <= 'Z') {
			right[i] = (char) (right[i] - 'A' + 'a');
		}
	}
	while (left.size() > 3 && left[left.size() - 1] == '/') {
		left.erase(left.size() - 1);
	}
	while (right.size() > 3 && right[right.size() - 1] == '/') {
		right.erase(right.size() - 1);
	}
	if (left == right) {
		return true;
	}
	return right.size() > left.size()
		&& right.compare(0, left.size(), left) == 0
		&& right[left.size()] == '/';
#else
	if (base == candidate) {
		return true;
	}
	if (base == "/") {
		return !candidate.empty() && candidate[0] == '/';
	}
	return candidate.size() > base.size()
		&& candidate.compare(0, base.size(), base) == 0
		&& candidate[base.size()] == '/';
#endif
}

// 初始化主存储目录下需要的各类数据库。
bool init_storage_databases(const std::string& path, std::string& err)
{
	return init_video_resume_db(path, err)
		&& init_tag_db(path, err)
		&& init_recycle_bin_db(path, err)
		&& init_category_folder_db(path, err);
}

// 返回管理设置目录路径。
std::string settings_dir(const std::string& upload_dir)
{
	return join_upload_path(upload_dir, ".webcool_settings");
}

// 返回管理设置文件路径。
std::string settings_file(const std::string& upload_dir)
{
	return settings_dir(upload_dir) + "/settings.db";
}

// 将文本解析为布尔值，无法识别时返回 fallback。
bool parse_bool_text(const std::string& text, bool fallback)
{
	if (text == "1" || text == "true" || text == "yes" || text == "on") {
		return true;
	}
	if (text == "0" || text == "false" || text == "no" || text == "off") {
		return false;
	}
	return fallback;
}

// 从请求参数中读取布尔值，缺失时返回 fallback。
bool request_bool_param(request_t& req, const char* name, bool fallback)
{
	const char* value = req.getParameter(name);
	return value ? parse_bool_text(value, fallback) : fallback;
}

// 判断备份路径列表中是否已有完全相同的路径字符串。
bool backup_vector_contains(const std::vector<storage_backup_path_t>& items,
	const std::string& value)
{
	for (const auto & item : items) {
		if (item.path == value) {
			return true;
		}
	}
	return false;
}

// 创建一个备份路径配置项。
storage_backup_path_t make_backup_path(const std::string& path, bool enabled)
{
	storage_backup_path_t item;
	item.path = path;
	item.enabled = enabled;
	return item;
}

// 在持有 settings 锁的前提下加载管理设置。
bool load_settings_unlocked(const std::string& upload_dir,
	webcool_settings_t& settings, std::string& err)
{
	settings = default_settings();
	std::ifstream in(settings_file(upload_dir).c_str(), std::ios::in);
	if (!in.good()) {
		return true;
	}
	std::string line;
	while (std::getline(in, line)) {
		if (line.empty()) {
			continue;
		}
		const std::string::size_type pos = line.find('=');
		if (pos == std::string::npos) {
			err = "invalid settings database";
			return false;
		}
		const std::string key = line.substr(0, pos);
		const std::string value = line.substr(pos + 1);
		if (key == "local_disk_admin") {
			settings.local_disk_admin = parse_bool_text(value, settings.local_disk_admin);
		} else if (key == "local_disk_user") {
			settings.local_disk_user = parse_bool_text(value, settings.local_disk_user);
		} else if (key == "backup_upload_auto_sync") {
			settings.backup_upload_auto_sync = parse_bool_text(value,
				settings.backup_upload_auto_sync);
		} else if (key == "backup_path") {
			if (!value.empty() && !backup_vector_contains(settings.backup_paths, value)) {
				settings.backup_paths.push_back(make_backup_path(value, true));
			}
		} else if (key == "backup_path_disabled") {
			if (!value.empty() && !backup_vector_contains(settings.backup_paths, value)) {
				settings.backup_paths.push_back(make_backup_path(value, false));
			}
		}
	}
	return true;
}

// 在持有 settings 锁的前提下保存管理设置。
bool save_settings_unlocked(const std::string& upload_dir,
	const webcool_settings_t& settings, std::string& err)
{
	const std::string dir = settings_dir(upload_dir);
	if (!make_dir_recursive(dir.c_str())) {
		err = "cannot create settings directory";
		return false;
	}
	const std::string path = settings_file(upload_dir);
	const std::string tmp = path + ".tmp";
	std::ofstream out(tmp.c_str(), std::ios::out | std::ios::trunc);
	if (!out.good()) {
		err = "cannot write settings database";
		return false;
	}
	out << "local_disk_admin=" << (settings.local_disk_admin ? "1" : "0") << '\n';
	out << "local_disk_user=" << (settings.local_disk_user ? "1" : "0") << '\n';
	out << "backup_upload_auto_sync="
		<< (settings.backup_upload_auto_sync ? "1" : "0") << '\n';
	for (const auto & backup_path : settings.backup_paths) {
		out << (backup_path.enabled
			? "backup_path=" : "backup_path_disabled=")
			<< backup_path.path << '\n';
	}
	out.close();
	if (!out.good()) {
		err = "cannot flush settings database";
		return false;
	}
	if (rename(tmp.c_str(), path.c_str()) != 0) {
		err = strerror(errno);
		return false;
	}
	return true;
}

// 判断备份路径列表中是否已有指向同一真实目录的路径。
bool backup_vector_contains_dir(const std::vector<storage_backup_path_t>& items,
	const std::string& dir)
{
	for (const auto & item : items) {
		std::string existing_dir, err;
		if (ensure_storage_target_path(item.path, existing_dir, err)
			&& existing_dir == dir)
		{
			return true;
		}
	}
	return false;
}

// 将主存储的备份路径列表统一为真实路径，并移除与主存储重复的项。
bool normalize_backup_real_paths_for_primary(const std::string& upload_dir,
	webcool_settings_t& settings, bool& changed, std::string& err)
{
	std::string primary_real_path;
	if (!resolve_existing_real_path(upload_dir, primary_real_path, err)) {
		return false;
	}
	std::vector<storage_backup_path_t> next;
	for (auto & backup_path : settings.backup_paths) {
		std::string backup_real_path;
		if (!ensure_storage_target_path(backup_path.path, backup_real_path, err)) {
			return false;
		}
		if (backup_real_path == primary_real_path) {
			changed = true;
			continue;
		}
		bool exists = false;
		for (auto & path : next) {
			if (path.path == backup_real_path) {
				exists = true;
				if (backup_path.enabled && !path.enabled) {
					path.enabled = true;
				}
				break;
			}
		}
		if (!exists) {
			next.push_back(make_backup_path(backup_real_path, backup_path.enabled));
		}
		if (backup_path.path != backup_real_path) {
			changed = true;
		}
	}
	if (next.size() != settings.backup_paths.size()) {
		changed = true;
	}
	settings.backup_paths = next;
	return true;
}

// 校验备份路径不能与主存储或其他备份路径互相包含。
bool validate_backup_path_for_settings(const std::string& upload_dir,
	const std::string& target_dir, const std::vector<storage_backup_path_t>& existing,
	const std::string& skip_path, std::string& err)
{
	std::string source_dir;
	if (!resolve_existing_real_path(upload_dir, source_dir, err)) {
		return false;
	}
	if (source_dir == target_dir) {
		err = "backup path cannot be current storage path";
		return false;
	}
	if (is_same_or_child_path(source_dir, target_dir)
		|| is_same_or_child_path(target_dir, source_dir))
	{
		err = "backup path cannot be inside current storage path or contain it";
		return false;
	}
	for (const auto & path : existing) {
		if (!skip_path.empty() && path.path == skip_path) {
			continue;
		}
		std::string backup_dir;
		if (!ensure_storage_target_path(path.path, backup_dir, err)) {
			return false;
		}
		if (backup_dir == target_dir) {
			err = "backup path already exists";
			return false;
		}
		if (is_same_or_child_path(backup_dir, target_dir)
			|| is_same_or_child_path(target_dir, backup_dir))
		{
			err = "backup paths cannot contain each other";
			return false;
		}
	}
	return true;
}

// 按当前平台规则拼接存储路径。
std::string join_storage_path(const std::string& parent, const char* name)
{
#ifdef _WIN32
	if (parent.empty()) {
		return name ? name : "";
	}
	const char tail = parent[parent.size() - 1];
	if (tail == '/' || tail == '\\') {
		return parent + name;
	}
	return parent + "\\" + name;
#else
	if (parent == "/") {
		return std::string("/") + name;
	}
	return parent + "/" + name;
#endif
}

// 将当前主存储路径写入 primary_storage.path。
bool record_primary_storage_path(const std::string& upload_dir, std::string& err)
{
	if (upload_dir.empty()) {
		logger("upload dir is empty");
		return true;
	}
	std::string real_path;
	if (!resolve_existing_real_path(upload_dir, real_path, err)) {
		logger_error("resolve_existing_real_path error, upload_dir=%s", upload_dir.c_str());
		return false;
	}
	if (is_absolute_storage_path(upload_dir)) {
		return write_primary_storage_path(upload_dir, err);
	}
	return write_primary_storage_path(real_path, err);
}

// 确保存储目录中存在共享目录及其固定子目录。
void ensure_shared_folder_for_storage(const std::string& upload_dir)
{
	if (upload_dir.empty()) {
		return;
	}
	const std::vector<std::string>& names = shared_fixed_subfolder_names();
	for (std::vector<std::string>::const_iterator it = names.begin();
		it != names.end(); ++it)
	{
		(void) make_dir_recursive(join_storage_path(upload_dir, it->c_str()).c_str());
	}
	const std::string path = join_storage_path(upload_dir, shared_folder_name());
	if (!make_dir_recursive(path.c_str())) {
		return;
	}
	for (std::vector<std::string>::const_iterator it = names.begin();
		it != names.end(); ++it)
	{
		(void) make_dir_recursive(join_storage_path(path, it->c_str()).c_str());
	}
}

// 生成用于备份文件名的时间后缀。
std::string storage_backup_date_suffix()
{
	char buf[32];
	time_t now = time(nullptr);
	tm tm_now{};

	acl_localtime_r(&now, &tm_now);

	strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &tm_now);
	return buf;
}

// 直接复制单个存储文件。
bool copy_storage_file_plain(const std::string& source,
	const std::string& dest, std::string& err)
{
	FILE* in = fopen(source.c_str(), "rb");
	if (in == nullptr) {
		err = strerror(errno);
		return false;
	}
	FILE* out = fopen(dest.c_str(), "wb");
	if (out == nullptr) {
		err = strerror(errno);
		fclose(in);
		return false;
	}
	char buf[1024 * 64];
	bool ok = true;
	while (true) {
		const size_t n = fread(buf, 1, sizeof(buf), in);
		if (n > 0 && fwrite(buf, 1, n, out) != n) {
			err = strerror(errno);
			ok = false;
			break;
		}
		if (n < sizeof(buf)) {
			if (ferror(in)) {
				err = strerror(errno);
				ok = false;
			}
			break;
		}
	}
	if (fclose(out) != 0 && ok) {
		err = strerror(errno);
		ok = false;
	}
	fclose(in);
	if (!ok) {
		(void) unlink(dest.c_str());
	}
	return ok;
}

// 递归复制存储路径下的普通文件和目录。
bool copy_storage_path_plain(const std::string& source,
	const std::string& dest, std::string& err)
{
	struct stat st{};
	if (lstat(source.c_str(), &st) != 0) {
		err = strerror(errno);
		return false;
	}
	if (S_ISREG(st.st_mode)) {
		const std::string parent = dest.substr(0, dest.find_last_of("/\\"));
		if (!parent.empty() && !make_dir_recursive(parent.c_str())) {
			err = "cannot create backup parent directory";
			return false;
		}
		if (!copy_storage_file_plain(source, dest, err)) {
			return false;
		}
		(void) chmod(dest.c_str(), st.st_mode & 0777);
		return true;
	}
	if (!S_ISDIR(st.st_mode)) {
		return true;
	}
	if (!make_dir_recursive(dest.c_str())) {
		err = "cannot create backup directory";
		return false;
	}
	(void) chmod(dest.c_str(), st.st_mode & 0777);
	DIR* dir = opendir(source.c_str());
	if (dir == nullptr) {
		err = strerror(errno);
		return false;
	}
	dirent* entry = nullptr;
	while ((entry = readdir(dir)) != nullptr) {
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
			continue;
		}
		if (!copy_storage_path_plain(join_storage_path(source, entry->d_name),
			join_storage_path(dest, entry->d_name), err))
		{
			closedir(dir);
			return false;
		}
	}
	closedir(dir);
	return true;
}

// 收集迁移需要处理的文件和目录项。
bool collect_move_items(const std::string& source,
	const std::string& target, std::vector<storage_move_item_t>& items,
	std::string& err)
{
	DIR* dir = opendir(source.c_str());
	if (dir == nullptr) {
		err = strerror(errno);
		return false;
	}
	dirent* entry = nullptr;
	while ((entry = readdir(dir)) != nullptr) {
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
			continue;
		}
		if (strcmp(entry->d_name, ".backup") == 0) {
			continue;
		}
		const std::string child_source = join_storage_path(source, entry->d_name);
		const std::string child_target = join_storage_path(target, entry->d_name);
		struct stat st{};
		if (lstat(child_source.c_str(), &st) != 0) {
			err = strerror(errno);
			closedir(dir);
			return false;
		}
		struct stat target_st{};
		if (lstat(child_target.c_str(), &target_st) == 0) {
			if (!(S_ISDIR(st.st_mode) && S_ISDIR(target_st.st_mode))) {
				if (!(S_ISREG(st.st_mode) && S_ISREG(target_st.st_mode))) {
					err = "target already contains: ";
					err += entry->d_name;
					closedir(dir);
					return false;
				}
			}
		} else if (errno != ENOENT) {
			err = strerror(errno);
			closedir(dir);
			return false;
		}
		if (S_ISDIR(st.st_mode)) {
			storage_move_item_t dir_item;
			dir_item.source = child_source;
			dir_item.target = child_target;
			dir_item.directory = true;
			dir_item.size = 0;
			items.push_back(dir_item);
			if (!collect_move_items(child_source, child_target, items, err)) {
				closedir(dir);
				return false;
			}
		} else if (S_ISREG(st.st_mode)) {
			storage_move_item_t file_item;
			file_item.source = child_source;
			file_item.target = child_target;
			file_item.directory = false;
			file_item.size = regular_file_size(child_source);
			items.push_back(file_item);
		}
	}
	closedir(dir);
	return true;
}

// 递归删除指定路径。
bool delete_path_recursive_plain(const std::string& path, std::string& err)
{
	struct stat st{};
	if (lstat(path.c_str(), &st) != 0) {
		if (errno == ENOENT) {
			return true;
		}
		err = strerror(errno);
		return false;
	}
	if (S_ISDIR(st.st_mode)) {
		DIR* dir = opendir(path.c_str());
		if (dir == nullptr) {
			err = strerror(errno);
			return false;
		}
		struct dirent* entry = nullptr;
		while ((entry = readdir(dir)) != nullptr) {
			if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
				continue;
			}
			if (!delete_path_recursive_plain(join_storage_path(path, entry->d_name), err)) {
				closedir(dir);
				return false;
			}
		}
		closedir(dir);
		if (rmdir(path.c_str()) != 0) {
			err = strerror(errno);
			return false;
		}
		return true;
	}
	if (unlink(path.c_str()) != 0) {
		err = strerror(errno);
		return false;
	}
	return true;
}

// 删除存储目录内容，但保留 .backup 目录。
bool delete_storage_contents_except_backup(const std::string& path, std::string& err)
{
	DIR* dir = opendir(path.c_str());
	if (dir == nullptr) {
		err = strerror(errno);
		return false;
	}
	const dirent* entry = nullptr;
	while ((entry = readdir(dir)) != nullptr) {
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0
			|| strcmp(entry->d_name, ".backup") == 0)
		{
			continue;
		}
		if (!delete_path_recursive_plain(join_storage_path(path, entry->d_name), err)) {
			closedir(dir);
			return false;
		}
	}
	closedir(dir);
	return true;
}

// 删除存储目录下的全部内容。
bool delete_storage_contents_all(const std::string& path, std::string& err)
{
	DIR* dir = opendir(path.c_str());
	if (dir == nullptr) {
		err = strerror(errno);
		return false;
	}
	struct dirent* entry = nullptr;
	while ((entry = readdir(dir)) != nullptr) {
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
			continue;
		}
		if (!delete_path_recursive_plain(join_storage_path(path, entry->d_name), err)) {
			closedir(dir);
			return false;
		}
	}
	closedir(dir);
	return true;
}

// 获取存储路径最后一级名称。
std::string storage_base_name(const std::string& path)
{
#ifdef _WIN32
	if (path.empty()) {
		return path;
	}
	std::string text = path;
	for (size_t i = 0; i < text.size(); ++i) {
		if (text[i] == '\\') {
			text[i] = '/';
		}
	}
	while (text.size() > 3 && text[text.size() - 1] == '/') {
		text.erase(text.size() - 1);
	}
	if (text.size() >= 2 && text.size() <= 3 && text[1] == ':') {
		return text;
	}
	std::string::size_type pos = text.rfind('/');
	return pos == std::string::npos ? text : text.substr(pos + 1);
#else
	if (path.empty() || path == "/") {
		return path;
	}
	std::string text = path;
	while (text.size() > 1 && text[text.size() - 1] == '/') {
		text.erase(text.size() - 1);
	}
	std::string::size_type pos = text.rfind('/');
	return pos == std::string::npos ? text : text.substr(pos + 1);
#endif
}

// 需要时把历史主存储加入备份路径列表。
bool add_previous_primary_backup_if_needed(const std::string& current_real_path,
	const std::string& previous_path, webcool_settings_t& settings,
	bool& settings_changed, std::string& err)
{
	if (previous_path.empty()) {
		return true;
	}

	std::string previous_real_path;
	if (!ensure_storage_target_path(previous_path, previous_real_path, err)) {
		return false;
	}
	if (previous_real_path == current_real_path
		|| backup_vector_contains_dir(settings.backup_paths, previous_real_path))
	{
		return true;
	}
	if (!validate_backup_path_for_settings(current_real_path, previous_real_path,
		settings.backup_paths, "", err))
	{
		return false;
	}
	settings.backup_paths.push_back(make_backup_path(previous_real_path, true));
	settings_changed = true;
	return true;
}

// 准备启动期主存储相关设置，包含备份路径规范化和旧主存储处理。
bool prepare_primary_settings(const std::string& current_real_path,
	const std::string& previous_path, bool add_previous_backup, std::string& err)
{
	std::lock_guard<webcool::mutex> settings_guard(g_settings_mutex);
	webcool_settings_t settings;
	if (!load_settings_unlocked(current_real_path, settings, err)) {
		return false;
	}

	bool settings_changed = false;
	if (!normalize_backup_real_paths_for_primary(current_real_path, settings,
		settings_changed, err))
	{
		return false;
	}
	if (add_previous_backup
		&& !add_previous_primary_backup_if_needed(current_real_path, previous_path,
			settings, settings_changed, err))
	{
		return false;
	}
	if (settings_changed
		&& !save_settings_unlocked(current_real_path, settings, err))
	{
		return false;
	}
	return true;
}

} // namespace admin_internal

using namespace admin_internal;

// 初始化运行期主存储路径，只在首次设置时写入。
void runtime_upload_dir_init(const std::string& upload_dir)
{
	bool changed = false;
	{
		std::lock_guard<webcool::mutex> guard(g_runtime_upload_mutex);
		if (g_runtime_upload_dir.empty()) {
			g_runtime_upload_dir = upload_dir;
			changed = true;
		}
	}
	if (changed) {
		ensure_shared_folder_for_storage(upload_dir);
		std::string err;
		(void) record_primary_storage_path(upload_dir, err);
	}
}

// 获取当前运行期主存储路径。
std::string runtime_upload_dir_get()
{
	std::lock_guard<webcool::mutex> guard(g_runtime_upload_mutex);
	return g_runtime_upload_dir;
}

// 更新当前运行期主存储路径并持久化。
void runtime_upload_dir_set(const std::string& upload_dir)
{
	{
		std::lock_guard<webcool::mutex> guard(g_runtime_upload_mutex);
		g_runtime_upload_dir = upload_dir;
	}
	ensure_shared_folder_for_storage(upload_dir);
	std::string err;
	(void) record_primary_storage_path(upload_dir, err);
}

// 启动时准备主存储路径：
// 1. 用真实路径比较当前存储和历史存储，避免软链接导致误判；
// 2. 规范化 settings.db 中的备份路径；
// 3. 命令行 -d 显式切换存储时，将旧主存储加入备份列表；
// 4. 持久化当前生效路径，并保留用户配置的绝对软链接路径。
bool storage_prepare_startup_primary(const std::string& upload_dir,
	bool upload_dir_specified, std::string& err)
{
	err.clear();
	std::string current_real_path;
	if (!resolve_existing_real_path(upload_dir, current_real_path, err)) {
		return false;
	}

	std::string previous_path;
	if (!read_primary_storage_path(previous_path, err)) {
		return false;
	}

	if (!prepare_primary_settings(current_real_path, previous_path,
		upload_dir_specified, err))
	{
		return false;
	}
	return record_primary_storage_path(upload_dir, err);
}

// 判断当前用户是否允许访问服务器本地磁盘。
bool local_disk_access_allowed(const std::string& upload_dir, bool admin,
	std::string& err)
{
	std::lock_guard<webcool::mutex> guard(g_settings_mutex);
	webcool_settings_t settings;
	if (!load_settings_unlocked(upload_dir, settings, err)) {
		return false;
	}
	return admin ? settings.local_disk_admin : settings.local_disk_user;
}

// 返回存储管理页面需要的主存储、备份和权限信息。
bool AdminStorageInfoAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	std::string err;
	webcool_settings_t settings;
	{
		std::lock_guard<webcool::mutex> guard(g_settings_mutex);
		if (!load_settings_unlocked(upload_dir, settings, err)) {
			json_error(res, 500, err.c_str(), req.isKeepAlive());
			return true;
		}
	}
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("path", upload_dir.c_str());
	root.add_text("backup_path", settings.backup_paths.empty()
		? "" : settings.backup_paths[0].path.c_str());
	root.add_bool("backup_upload_auto_sync", settings.backup_upload_auto_sync);
	acl::json_node& backups = json.create_array();
	root.add_child("backup_paths", backups);
	for (auto & backup_path : settings.backup_paths) {
		acl::json_node& item = backups.add_child(false, true);
		item.add_text("path", backup_path.path.c_str());
		item.add_bool("enabled", backup_path.enabled);
	}
	return sendJson(res, 200, root, req.isKeepAlive());
}

} // namespace action
