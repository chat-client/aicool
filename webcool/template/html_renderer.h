#pragma once

#include <string>
#include <utility>
#include <vector>
#include "acl_cpp/lib_acl.hpp"

namespace tpl {

typedef std::vector<std::pair<std::string, std::string> > template_vars;

class html_renderer {
public:
	static bool render_file(const char* file, const template_vars& vars,
		acl::string& out);

	static void render_text(acl::string& text, const template_vars& vars);

	static void clear_cache();

private:
	static bool load_cached_file(const char* file, acl::string& out);

	static void replace_all(std::string& text, const std::string& token,
		const std::string& value);
};

} // namespace tpl
