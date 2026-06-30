#include "stdafx.h"
#include "actions.h"
#include "action_util.h"
#include "../template/html_renderer.h"
#ifdef _WIN32
#include "../platform_compat.h"
#endif
#include <cerrno>
#include <cstdio>
#include <vector>

namespace action {

namespace {

std::string html_home = "/opt/soft/webcool/html";

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
	if (filepath == nullptr || !load_file_utf8_path(filepath, data, err)) {
		buff.format("load %s from %s failed(%s)\r\n", request_path,
			file_for_log, err.empty() ? "bad static path" : err.c_str());
		return sendText(res, 500, buff.c_str(), req.isKeepAlive());
	}

	return sendData(res, data, ctype.c_str(), req.isKeepAlive());
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
