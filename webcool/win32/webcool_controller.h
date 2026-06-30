#pragma once
#include <mutex>
#include <vector>

class http_service;

struct webcool_options {
	acl::string addr;
	int  nthreads;
	bool reuse_port = false;
	bool upload_dir_specified;
	http_service *service = nullptr;
};

class server_thread;

class webcool_controller {
public:
	explicit webcool_controller(const webcool_options& options);
	~webcool_controller();

	bool start(std::string& err);
	void stop();
	void wait();
	bool running() const;
	void configure(const webcool_options& options);
	const webcool_options& options() const;

private:
	void stop_locked();

	webcool_options options_;
	acl::server_socket* server_;
	std::vector<server_thread*> threads_;
	mutable std::mutex mutex_;
	bool running_;
};
