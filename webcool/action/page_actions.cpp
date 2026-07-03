#include "stdafx.h"
#include "actions.h"
#include "action_util.h"
#include "../template/html_renderer.h"
#ifdef _WIN32
#include "../platform_compat.h"
#endif
#include <cerrno>
#include <cstdio>
#include <ctime>
#include <vector>
#include <sys/stat.h>

namespace action {

namespace {

std::string html_home = "/opt/soft/webcool/html";

const char* kStaticCacheControl = "public, max-age=0, must-revalidate";

std::string trim_static_home(const char* path) {
	std::string value = path;
	while (value.size() > 1) {
		const char tail = value[value.size() - 1];
		if (tail != '/' && tail != '\\') {
			break;
		}
#ifdef _WIN32
		if (value.size() == 3 && value[1] == ':') {
			break;
		}
#endif
		value.resize(value.size() - 1);
	}
	return value;
}

std::string join_static_path(const std::string& parent, const char* name) {
	if (parent.empty()) {
		return name;
	}
	const char tail = parent[parent.size() - 1];
	if (tail == '/' || tail == '\\') {
		return parent + name;
	}
	return parent + "/" + name;
}

bool load_file_utf8_path(const char* filepath, acl::string& data, std::string& err) {
	data.clear();
	FILE* fp = fopen(filepath, "rb");
	if (fp == nullptr) {
		err = strerror(errno);
		return false;
	}
	if (fseek(fp, 0, SEEK_END) != 0) {
		err = strerror(errno);
		fclose(fp);
		return false;
	}
	const long size = ftell(fp);
	if (size < 0) {
		err = strerror(errno);
		fclose(fp);
		return false;
	}
	if (fseek(fp, 0, SEEK_SET) != 0) {
		err = strerror(errno);
		fclose(fp);
		return false;
	}
	if (size == 0) {
		fclose(fp);
		return true;
	}
	std::vector<char> buf(static_cast<size_t>(size) + 1, '\0');
	const size_t n = fread(&buf[0], 1, static_cast<size_t>(size), fp);
	const bool ok = n == static_cast<size_t>(size) && ferror(fp) == 0;
	if (!ok) {
		err = strerror(errno);
	}
	fclose(fp);
	if (!ok) {
		return false;
	}
	data.append(&buf[0], n);
	return true;
}

bool get_file_mtime(const char* filepath, time_t& mtime, std::string& err) {
	struct stat st;
	if (stat(filepath, &st) != 0) {
		err = strerror(errno);
		return false;
	}
	mtime = st.st_mtime;
	return true;
}

bool gmt_time(time_t value, struct tm& out) {
#ifdef _WIN32
	return gmtime_s(&out, &value) == 0;
#else
	struct tm* tm = gmtime_r(&value, &out);
	return tm != nullptr;
#endif
}

std::string format_http_date(time_t value) {
	struct tm tm;
	if (!gmt_time(value, tm)) {
		return "";
	}
	char buf[64];
	if (strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &tm) == 0) {
		return "";
	}
	return buf;
}

int month_index(const char* text) {
	static const char* months[] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
	};
	for (int i = 0; i < 12; ++i) {
#ifdef _WIN32
		if (_strnicmp(text, months[i], 3) == 0) {
#else
		if (strncasecmp(text, months[i], 3) == 0) {
#endif
			return i + 1;
		}
	}
	return 0;
}

long long days_from_civil(int year, unsigned month, unsigned day) {
	year -= month <= 2;
	const int era = (year >= 0 ? year : year - 399) / 400;
	const unsigned yoe = static_cast<unsigned>(year - era * 400);
	const int shifted_month = static_cast<int>(month) + (month > 2 ? -3 : 9);
	const unsigned doy = (153 * static_cast<unsigned>(shifted_month) + 2) / 5
		+ day - 1;
	const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097LL + static_cast<long long>(doe) - 719468LL;
}

bool parse_http_date(const char* value, time_t& out) {
	if (value == nullptr || *value == '\0') {
		return false;
	}

	char month_text[4] = { 0 };
	int day = 0;
	int year = 0;
	int hour = 0;
	int minute = 0;
	int second = 0;
	if (sscanf(value, "%*3s, %d %3s %d %d:%d:%d GMT",
			&day, month_text, &year, &hour, &minute, &second) != 6)
	{
		return false;
	}

	const int month = month_index(month_text);
	if (month <= 0 || day < 1 || day > 31 || year < 1970
		|| hour < 0 || hour > 23 || minute < 0 || minute > 59
		|| second < 0 || second > 60)
	{
		return false;
	}

	const long long days = days_from_civil(year,
		static_cast<unsigned>(month), static_cast<unsigned>(day));
	if (days < 0) {
		return false;
	}

	out = static_cast<time_t>(days * 86400LL + hour * 3600LL
		+ minute * 60LL + second);
	return true;
}

bool client_cache_is_fresh(const request_t& req, time_t file_mtime) {
	const char* value = req.getHeader("If-Modified-Since");
	time_t client_time = 0;
	if (!parse_http_date(value, client_time)) {
		return false;
	}
	return client_time >= file_mtime;
}

bool send_not_modified(response_t& res, const char* last_modified,
	  const bool keep_alive) {
	res.setStatus(304)
		.setKeepAlive(keep_alive)
		.setHeader("Cache-Control", kStaticCacheControl)
		.setHeader("Last-Modified", last_modified)
		.setContentLength(0);
	return res.write(nullptr, 0);
}

bool send_static_data(response_t& res, const acl::string& data,
	  const char* type, const char* last_modified, const bool keep_alive) {
	res.setStatus(200)
		.setContentType(type)
		.setKeepAlive(keep_alive)
		.setHeader("Cache-Control", kStaticCacheControl)
		.setHeader("Last-Modified", last_modified)
		.setContentLength(static_cast<long long>(data.size()));
	return res.write(data) && res.write(nullptr, 0);
}

const char* get_index_html_path(acl::string& buff) {
	const std::string path = join_static_path(html_home, "main.html");
	buff = path.c_str();
	return buff.c_str();
}

const char* get_static_file_path(const char* path, acl::string& buff,
	  std::string& ctype) {
	buff.clear();

	const auto static_prefix = "/webcool/html";
	const size_t prefix_len = strlen(static_prefix);
	if (path == nullptr || strncmp(path, static_prefix, prefix_len) != 0) {
		return nullptr;
	}
	if (path[prefix_len] != '\0' && path[prefix_len] != '/'
		&& path[prefix_len] != '\\')
	{
		return nullptr;
	}

	std::string relative_path = path + prefix_len;
	for (char & ch : relative_path) {
		if (ch == '\\') {
			ch = '/';
		}
	}

	while (!relative_path.empty() && relative_path[0] == '/') {
		relative_path.erase(0, 1);
	}
	if (relative_path.empty()) {
		return get_index_html_path(buff);
	}

	std::string safe_path;
	size_t start = 0;
	while (start <= relative_path.size()) {
		size_t end = relative_path.find('/', start);
		if (end == std::string::npos) {
			end = relative_path.size();
		}
		const std::string part = relative_path.substr(start, end - start);
		if (!part.empty() && part != "." && part != "..") {
			if (!safe_path.empty()) {
				safe_path += '/';
			}
			safe_path += part;
		}
		if (end == relative_path.size()) {
			break;
		}
		start = end + 1;
	}

	buff = join_static_path(html_home, safe_path.c_str()).c_str();

	static std::map<std::string, std::string> types = {
		{ ".text", "text/plain; charset=utf-8"              },
		{ ".txt",  "text/plain; charset=utf-8"              },
		{ ".html", "text/html; charset=utf-8"              },
		{ ".js",   "application/javascript; charset=utf-8" },
		{ ".css",  "text/css; charset=utf-8"               },
		{ ".png",  "image/png"                             },
		{ ".jpg",  "image/jpg"                             },
		{ ".jpeg", "image/jpeg"                            },
		{ ".gif",  "image/gif"                             },
		{ ".heic", "image/heic"                            },
		{ ".heif", "image/heif"                            },
	};

	char* pos = buff.rfind(".");
	if (pos == nullptr) {
		ctype = "application/octet-stream";
	} else {
		const auto it = types.find(pos);
		if (it == types.end()) {
			ctype = "application/octet-stream";
		} else {
			ctype = it->second;
		}
	}

	return buff.c_str();
}

} // namespace

void IndexAction::set_static_home_path(const char* html_home_path) {
	if (html_home_path != nullptr && *html_home_path != '\0') {
		html_home = trim_static_home(html_home_path);
	}
}

bool IndexAction::run(const request_t& req, response_t& res) {
	const char* path = req.getPathInfo();
	const char* filepath = nullptr;
	std::string ctype = "text/html";
	acl::string buff;
	if (path == nullptr || *path == 0 || strcmp(path, "/") == 0) {
		filepath = get_index_html_path(buff);
	} else {
		filepath = get_static_file_path(path, buff, ctype);
	}

	const char* request_path = path ? path : "";
	const char* file_for_log = filepath ? filepath : "";
	logger_debug(DEBUG_PAGE, 1, "path=%s, filepath=%s",
		request_path, file_for_log);

	acl::string data;
	std::string err;
	time_t file_mtime = 0;
	if (filepath == nullptr || !get_file_mtime(filepath, file_mtime, err)) {
		buff.format("load %s from %s failed(%s)\r\n", request_path,
			file_for_log, err.empty() ? "bad static path" : err.c_str());
		return sendText(res, 500, buff.c_str(), req.isKeepAlive());
	}

	const std::string last_modified = format_http_date(file_mtime);
	if (!last_modified.empty()
		&& client_cache_is_fresh(req, file_mtime))
	{
		logger_debug(DEBUG_PAGE, 1, "static cache hit: path=%s,"
			" last_modified=%s", request_path, last_modified.c_str());
		return send_not_modified(res, last_modified.c_str(), req.isKeepAlive());
	}

	if (!load_file_utf8_path(filepath, data, err)) {
		buff.format("load %s from %s failed(%s)\r\n", request_path,
			file_for_log, err.empty() ? "bad static path" : err.c_str());
		return sendText(res, 500, buff.c_str(), req.isKeepAlive());
	}

	return send_static_data(res, data, ctype.c_str(), last_modified.c_str(),
		req.isKeepAlive());
}

bool TemplateReloadAction::run(const request_t& req, response_t& res) {
	tpl::html_renderer::clear_cache();
	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	root.add_text("message", "template cache cleared");
	return sendJson(res, 200, root, req.isKeepAlive());
}

} // namespace action
