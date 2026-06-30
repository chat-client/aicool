#include "stdafx.h"
#include "html_renderer.h"
#include <map>
#include <mutex>

namespace {

std::map<std::string, std::string> g_template_cache;
std::mutex g_template_cache_mutex;

} // namespace

namespace tpl {

bool html_renderer::render_file(const char* file, const template_vars& vars,
	acl::string& out)
{
	if (!load_cached_file(file, out)) {
		return false;
	}

	render_text(out, vars);
	return true;
}

void html_renderer::render_text(acl::string& text, const template_vars& vars)
{
	std::string src = text.c_str();

	for (size_t i = 0; i < vars.size(); ++i) {
		replace_all(src, vars[i].first, vars[i].second);
	}

	text = src.c_str();
}

void html_renderer::clear_cache()
{
	std::lock_guard<std::mutex> guard(g_template_cache_mutex);
	g_template_cache.clear();
}

bool html_renderer::load_cached_file(const char* file, acl::string& out)
{
	if (file == NULL || *file == '\0') {
		return false;
	}

	{
		std::lock_guard<std::mutex> guard(g_template_cache_mutex);
		std::map<std::string, std::string>::const_iterator it =
			g_template_cache.find(file);
		if (it != g_template_cache.end()) {
			out = it->second.c_str();
			return true;
		}
	}

	acl::string content;
	if (!acl::ifstream::load(file, &content)) {
		return false;
	}

	{
		std::lock_guard<std::mutex> guard(g_template_cache_mutex);
		g_template_cache[file] = content.c_str();
	}

	out = content;
	return true;
}

void html_renderer::replace_all(std::string& text,
	const std::string& token, const std::string& value)
{
	if (token.empty()) {
		return;
	}

	size_t pos = 0;
	while ((pos = text.find(token, pos)) != std::string::npos) {
		text.replace(pos, token.size(), value);
		pos += value.size();
	}
}

} // namespace tpl
