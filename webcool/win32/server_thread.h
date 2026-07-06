#pragma once
#include <map>
#include "common/webcool_mutex.h"
#include <thread>

// ──────────────────────────────────────
// 线程：每个线程独立运行一个协程调度器
// ──────────────────────────────────────
class http_service;

class server_thread final : public acl::thread {
public:
	// 共享监听模式：传入已 open 的 server_socket
	explicit server_thread(http_service& service, acl::server_socket &server);
	~server_thread() override = default;

	bool opened() const { return opened_; }

	void stop();

private:
	http_service& service_;
	acl::server_socket *server_;
	bool opened_;
	webcool::mutex conns_mutex_;
	std::map<unsigned, std::shared_ptr<acl::fiber>> fibers_;
	acl::fiber_tbox2<bool> stop_signal_;
	acl::wait_group stop_wait_group_;

	// 每个连接的处理逻辑，运行在独立协程中
	void handle_conn(acl::socket_stream &conn) const;

	// @override
	void *run() override;
};

