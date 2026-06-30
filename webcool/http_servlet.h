#pragma once

class http_service;

class http_servlet final : public acl::HttpServlet {
public:
	http_servlet(http_service& service, acl::socket_stream* conn,
		acl::session* session);
	~http_servlet() override = default;

protected:
	// @override
	bool doGet(HttpRequest& req, HttpResponse& res) override;

	// @override
	bool doPost(HttpRequest& req, HttpResponse& res) override;

	// @override
	bool doHead(HttpRequest& req, HttpResponse& res) override;

	// @override
	bool doPut(HttpRequest& req, HttpResponse& res) override;

	// @override
	bool doPatch(HttpRequest& req, HttpResponse& res) override;

	// @override
	bool doConnect(HttpRequest& req, HttpResponse& res) override;

	// @override
	bool doPurge(HttpRequest& req, HttpResponse& res) override;

	// @override
	bool doDelete(HttpRequest& req, HttpResponse& res) override;

	// @override
	bool doOptions(HttpRequest& req, HttpResponse& res) override;

	// @override
	bool doPropfind(HttpRequest& req, HttpResponse& res) override;

	// @override
	bool doWebSocket(HttpRequest& req, HttpResponse& res) override;

	// @override
	bool doUnknown(HttpRequest& req, HttpResponse& res) override;

	// @override
	bool doError(HttpRequest& req, HttpResponse& res) override;

	// @override
	bool doOther(HttpRequest&, HttpResponse& res, const char* method) override;

private:
	http_service& service_;

	bool doService(int type, HttpRequest& req, HttpResponse& res) const;
};
