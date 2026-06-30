#include "actions.h"
#include "action_util.h"
#include <sys/stat.h>
#include <time.h>
#include <strings.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace action {

bool UploadAction::run(request_t& req, response_t& res,
	const std::string& upload_dir)
{
	if (req.getRequestType() != acl::HTTP_REQUEST_MULTIPART_FORM) {
		acl::json json;
		acl::json_node& root = json.create_node();
		root.add_bool("ok", false);
		root.add_text("error", "request type must be multipart/form-data");
		return sendJson(res, 400, root, req.isKeepAlive());
	}

	acl::http_mime* mime = req.getHttpMime();
	if (mime == NULL) {
		acl::json json;
		acl::json_node& root = json.create_node();
		root.add_bool("ok", false);
		root.add_text("error", "getHttpMime failed");
		return sendJson(res, 400, root, req.isKeepAlive());
	}

	long long content_length = req.getContentLength();
	if (content_length <= 0) {
		acl::json json;
		acl::json_node& root = json.create_node();
		root.add_bool("ok", false);
		root.add_text("error", "empty request body");
		return sendJson(res, 400, root, req.isKeepAlive());
	}

	if (!make_dir(upload_dir.c_str())) {
		fprintf(stderr, "Cannot create upload dir: %s\n", upload_dir.c_str());
		acl::json json;
		acl::json_node& root = json.create_node();
		root.add_bool("ok", false);
		root.add_text("error", "cannot access upload dir");
		return sendJson(res, 500, root, req.isKeepAlive());
	}

	acl::string tmp_path;
	tmp_path.format("%s/.mime_tmp.%u.%d", upload_dir.c_str(),
		(unsigned) getpid(), acl::fiber::self());

	acl::ofstream fp;
	if (!fp.open_write(tmp_path.c_str())) {
		fprintf(stderr, "open tmp file %s error: %s\n",
			tmp_path.c_str(), acl::last_serror());
		acl::json json;
		acl::json_node& root = json.create_node();
		root.add_bool("ok", false);
		root.add_text("error", "open temp file failed");
		return sendJson(res, 500, root, req.isKeepAlive());
	}

	mime->set_saved_path(tmp_path.c_str());

	if (!readBody(req, content_length, fp, *mime)) {
		fp.close();
		::unlink(tmp_path.c_str());
		acl::json json;
		acl::json_node& root = json.create_node();
		root.add_bool("ok", false);
		root.add_text("error", "read request body failed");
		return sendJson(res, 500, root, req.isKeepAlive());
	}
	fp.close();

	acl::json json;
	acl::json_node& root = json.create_node();
	root.add_bool("ok", true);
	acl::json_node& files = json.create_array();
	root.add_child("files", files);

	int saved_count = 0;
	bool ok = saveFiles(*mime, upload_dir, files, saved_count);

	//::unlink(tmp_path.c_str());

	if (!ok) {
		acl::json err;
		acl::json_node& eroot = err.create_node();
		eroot.add_bool("ok", false);
		eroot.add_text("error", "save files failed");
		return sendJson(res, 500, eroot, req.isKeepAlive());
	}

	root.add_number("count", saved_count);
	return sendJson(res, 200, root, req.isKeepAlive());
}

bool UploadAction::readBody(request_t& req, long long content_length,
	acl::ofstream& fp, acl::http_mime& mime)
{
	acl::istream& in = req.getInputStream();
	char buf[8192];
	long long read_total = 0;
	bool mime_done = false;

	while (read_total < content_length) {
		size_t want = sizeof(buf);
		long long remain = content_length - read_total;
		if ((long long) want > remain) {
			want = (size_t) remain;
		}

		int n = in.read(buf, want);
		if (n < 0) {
			fprintf(stderr, "read request body error: %s\n", acl::last_serror());
			return false;
		}
		if (n == 0) {
			fprintf(stderr, "read request body got 0 bytes before completion\n");
			return false;
		}

		if (fp.write(buf, n) == -1) {
			fprintf(stderr, "write tmp file error: %s\n", acl::last_serror());
			return false;
		}

		read_total += n;

		if (!mime_done && mime.update(buf, (size_t) n)) {
			mime_done = true;
		}
	}

	if (read_total != content_length) {
		fprintf(stderr, "request body incomplete: read=%lld, expect=%lld\n",
			read_total, content_length);
		return false;
	}

	return true;
}

bool UploadAction::saveFiles(acl::http_mime& mime, const std::string& upload_dir,
	acl::json_node& files_array, int& saved_count)
{
	saved_count = 0;

	const std::list<acl::http_mime_node*>& nodes = mime.get_nodes();
	for (std::list<acl::http_mime_node*>::const_iterator it = nodes.begin();
		it != nodes.end(); ++it)
	{
		const acl::http_mime_node* node = *it;
		if (node->get_mime_type() != acl::HTTP_MIME_FILE) {
			continue;
		}

		const char* filename = node->get_filename();
		if (filename == NULL || *filename == '\0') {
			continue;
		}

		const char* basename = acl_safe_basename(filename);
		if (basename == NULL || *basename == '\0') {
			continue;
		}

		acl::string dest;
		dest.format("%s/%s", upload_dir.c_str(), basename);
		acl::json_node& item = files_array.add_child(false, true);

		if (node->save(dest.c_str())) {
			acl::ifstream fin;
			long long fsize = -1;
			if (fin.open_read(dest.c_str())) {
				fsize = fin.fsize();
				fin.close();
			}

			if (fsize <= 0) {
				fprintf(stderr, "Saved file is empty or invalid: %s (%lld bytes), dest: %s\n",
					dest.c_str(), fsize, dest.c_str());
				::unlink(dest.c_str());
				item.add_text("name", basename);
				item.add_number("size", fsize);
				item.add_bool("saved", false);
				item.add_text("error", "empty or invalid file");
				continue;
			}

			item.add_text("name", basename);
			item.add_number("size", fsize);
			item.add_bool("saved", true);
			saved_count++;
			printf("Saved: %s (%lld bytes)\n", dest.c_str(), fsize);
		} else {
			fprintf(stderr, "Save file %s to %s failed: %s\n",
				basename, dest.c_str(), acl::last_serror());
			item.add_text("name", basename);
			item.add_bool("saved", false);
		}
	}
	return true;
}

} // namespace action
