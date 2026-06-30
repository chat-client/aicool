#include "stdafx.h"
#include "http_service.h"
#include "http_servlet.h"

http_servlet::http_servlet(http_service& service, acl::socket_stream* conn,
	acl::session* session)
: HttpServlet(conn, session), service_(service)
{
	this->setLocalCharset("utf8");
}

bool http_servlet::doGet(HttpRequest& req, HttpResponse& res) {
	return doService(http_handler_get, req, res);
}

bool http_servlet::doPost(HttpRequest& req, HttpResponse& res) {
	return doService(http_handler_post, req, res);
}

bool http_servlet::doHead(HttpRequest& req, HttpResponse& res) {
	return doService(http_handler_head, req, res);
}

bool http_servlet::doPut(HttpRequest& req, HttpResponse& res) {
	return doService(http_handler_put, req, res);
}

bool http_servlet::doPatch(HttpRequest& req, HttpResponse& res) {
	return doService(http_handler_patch, req, res);
}

bool http_servlet::doConnect(HttpRequest& req, HttpResponse& res) {
	return doService(http_handler_connect, req, res);
}

bool http_servlet::doPurge(HttpRequest& req, HttpResponse& res) {
	return doService(http_handler_purge, req, res);
}

bool http_servlet::doDelete(HttpRequest& req, HttpResponse& res) {
	return doService(http_handler_delete, req, res);
}

bool http_servlet::doOptions(HttpRequest& req, HttpResponse& res) {
	return doService(http_handler_options, req, res);
}

bool http_servlet::doPropfind(HttpRequest& req, HttpResponse& res) {
	return doService(http_handler_profind, req, res);
}

bool http_servlet::doWebSocket(HttpRequest& req, HttpResponse& res) {
	return doService(http_handler_websocket, req, res);
}

bool http_servlet::doUnknown(HttpRequest& req, HttpResponse& res) {
	return doService(http_handler_unknown, req, res);
}

bool http_servlet::doError(HttpRequest& req, HttpResponse& res) {
	return doService(http_handler_error, req, res);
}

bool http_servlet::doOther(HttpRequest&, HttpResponse& res, const char* method) {
	res.setStatus(400);
	res.setContentType("text/xml; charset=utf-8");
	acl::string buf;
	buf.format("<root error='unkown method %s' />\r\n", method);
	res.write(buf);
	res.write(nullptr, 0);
	return false;
}

bool http_servlet::doService(int type, HttpRequest& req, HttpResponse& res) const {
	/*
	const char* sid = req.getSession().getAttribute("sid");
	if (*sid == 0) {
		req.getSession().setAttribute("sid", "xxxxxx");
	}
	sid = req.getSession().getAttribute("sid");
	*/

	const char* expect = req.getHeader("Expect");
	if (expect && strcasecmp(expect, "100-continue") == 0) {
		const char* reply = "HTTP/1.1 100 Continue\r\n\r\n";
		acl::ostream& out = res.getOutputStream();
		if (out.write(reply, strlen(reply)) == -1) {
			return false;
		}
	}

	return service_.doService(type, req, res);
}
