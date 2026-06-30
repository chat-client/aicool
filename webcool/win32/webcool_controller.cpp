#include "stdafx.h"
#include "../config.h"
#include "../action/action_util.h"
#include "../master_service.h"
#include "server_thread.h"
#include "webcool_controller.h"

webcool_controller::webcool_controller(const webcool_options& options)
: options_(options), server_(nullptr), running_(false)
{
}

webcool_controller::~webcool_controller() {
	stop();
}

bool webcool_controller::start(std::string& err) {
	std::lock_guard<std::mutex> guard(mutex_);
	err.clear();
	if (running_) {
		return true;
	}
	g_service_stopping.store(false);

	bool upload_dir_specified = options_.upload_dir_specified;
	if (!master_service::prepare_runtime(upload_dir_specified)) {
		logger_error("parepare runtime failed");
		return false;
	}

	server_ = new acl::server_socket;
	if (!server_->open(options_.addr)) {
		err = acl::last_serror();
		delete server_;
		server_ = nullptr;
		return false;
	}
	printf("监听 %s 成功\n", options_.addr.c_str());

	assert(options_.service);
	for (int i = 0; i < options_.nthreads; i++) {
		auto thr = new server_thread(*options_.service, *server_);
		if (!thr->opened()) {
			err = acl::last_serror();
			delete thr;
			stop_locked();
			return false;
		}
		thr->set_detachable(false);
		thr->start();
		threads_.push_back(thr);
	}

	running_ = true;
	return true;
}

void webcool_controller::stop() {
	std::lock_guard<std::mutex> guard(mutex_);
	stop_locked();
}

void webcool_controller::wait() {
	std::vector<server_thread*> threads;
	{
		std::lock_guard<std::mutex> guard(mutex_);
		threads.swap(threads_);
	}
	for (size_t i = 0; i < threads.size(); ++i) {
		threads[i]->wait();
		delete threads[i];
	}
	{
		std::lock_guard<std::mutex> guard(mutex_);
		if (server_ != nullptr) {
			delete server_;
			server_ = nullptr;
		}
		running_ = false;
		g_service_stopping.store(false);
	}
}

bool webcool_controller::running() const {
	std::lock_guard<std::mutex> guard(mutex_);
	return running_;
}

void webcool_controller::configure(const webcool_options& options) {
	std::lock_guard<std::mutex> guard(mutex_);
	if (!running_) {
		options_ = options;
	}
}

const webcool_options &webcool_controller::options() const {
	return options_;
}

void webcool_controller::stop_locked() {
	if (!running_ && threads_.empty() && server_ == nullptr) {
		return;
	}

	g_service_stopping.store(true);
	for (size_t i = 0; i < threads_.size(); ++i) {
		threads_[i]->stop();
	}
	for (size_t i = 0; i < threads_.size(); ++i) {
		threads_[i]->wait();
		delete threads_[i];
	}
	threads_.clear();

	if (server_ != nullptr) {
		server_->close();
		delete server_;
		server_ = nullptr;
	}
	running_ = false;
	g_service_stopping.store(false);
}
