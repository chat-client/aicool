#include "stdafx.h"
#include "http_service.h"

#include <utility>

static bool http_not_found(const char* path, const HttpRequest& req, HttpResponse& res) {
	bool keep = req.isKeepAlive();

	res.setStatus(404);
	acl::string buf  = "404 ";
	buf += path;
	buf += " not found\r\n";
	res.setContentLength(static_cast<long long>(buf.size()));
	return res.write(buf.c_str(), buf.size()) && keep;
}

http_service::http_service() : handler_default_(http_not_found) {}

http_service &http_service::AfterHandle(after_handle_t fn) {
	after_handle_ = std::move(fn);
	return *this;
}

http_service &http_service::CheckPerm(check_perm_t fn) {
	check_perm_ = std::move(fn);
	return *this;
}

http_service& http_service::Default(http_default_handler_t fn) {
	handler_default_ = std::move(fn);
	return *this;
}

http_service& http_service::Get(const char* path, http_handler_t fn) {
	Service(http_handler_get, path, std::move(fn));
	return *this;
}

http_service& http_service::Post(const char* path, http_handler_t fn) {
	Service(http_handler_post, path, std::move(fn));
	return *this;
}

http_service& http_service::Head(const char* path, http_handler_t fn) {
	Service(http_handler_head, path, std::move(fn));
	return *this;
}

http_service& http_service::Put(const char* path, http_handler_t fn) {
	Service(http_handler_put, path, std::move(fn));
	return *this;
}

http_service& http_service::Patch(const char* path, http_handler_t fn) {
	Service(http_handler_patch, path, std::move(fn));
	return *this;
}

http_service& http_service::Connect(const char* path, http_handler_t fn) {
	Service(http_handler_connect, path, std::move(fn));
	return *this;
}

http_service& http_service::Purge(const char* path, http_handler_t fn) {
	Service(http_handler_purge, path, std::move(fn));
	return *this;
}

http_service& http_service::Delete(const char* path, http_handler_t fn) {
	Service(http_handler_delete, path, std::move(fn));
	return *this;
}

http_service& http_service::Options(const char* path, http_handler_t fn) {
	Service(http_handler_options, path, std::move(fn));
	return *this;
}

http_service& http_service::Propfind(const char* path, http_handler_t fn) {
	Service(http_handler_profind, path, std::move(fn));
	return *this;
}

http_service& http_service::Websocket(const char* path, http_handler_t fn) {
	Service(http_handler_websocket, path, std::move(fn));
	return *this;
}

http_service& http_service::Unknown(const char* path, http_handler_t fn) {
	Service(http_handler_unknown, path, std::move(fn));
	return *this;
}

http_service& http_service::Error(const char* path, http_handler_t fn) {
	Service(http_handler_error, path, std::move(fn));
	return *this;
}

void http_service::Service(int type, const char* path, http_handler_t fn) {
	if (type >= http_handler_get && type < http_handler_max && path && *path) {
		// The path should look up like as "/xxx/" with
		// lower characters.

		acl::string buf(path);
		if (buf[buf.size() - 1] != '/') {
			buf += '/';
		}
		buf.lower();
		handlers_[type][buf] = std::move(fn);
	}
}

bool http_service::doService(int type, HttpRequest& req, HttpResponse& res) {
	if (type < http_handler_get || type >= http_handler_max) {
		logger_error("invalid type=%d", type);
		return false;
	}

	res.setKeepAlive(req.isKeepAlive());
	bool keep = req.isKeepAlive();

	const char* path = req.getPathInfo();
	if (path == nullptr || *path == 0) {
		res.setStatus(400);
		acl::string buf("400 bad request\r\n");
		res.setContentLength(static_cast<long long>(buf.size()));
		return res.write(buf.c_str(), buf.size()) && keep;
	}

	size_t len = strlen(path);
	acl::string buf(path);
	if (path[len - 1] != '/') {
		buf += '/';
	}
	buf.lower();

	const auto it = handlers_[type].find(buf);
	if (it == handlers_[type].end()) {
		return handler_default_(buf, req, res) && keep;
	}

	if (check_perm_) {
		bool ok = false;
		const bool keepAlive = check_perm_(path, req, res, ok);
		if (!ok) {
			return keepAlive;
		}
	}

	timeval begin;
	gettimeofday(&begin, nullptr);

	if (!it->second(req, res)) {
		return false;
	}

	if (after_handle_) {
		after_handle_(path, req);
	}

	timeval end;
	gettimeofday(&end, nullptr);
	const double cost = acl::stamp_sub(end, begin);
	logger_debug(DEBUG_ACTION, 1, "Time cost %.2f m, path=%s", cost, path);
	return keep;
}
