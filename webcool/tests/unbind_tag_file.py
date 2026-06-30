#!/usr/bin/env python3

import argparse
import json
import sys
import urllib.error
import urllib.parse
import urllib.request


def request_json(base_url, path, params=None, method="GET"):
    query = ""
    if params:
        query = "?" + urllib.parse.urlencode(params)
    url = base_url.rstrip("/") + path + query
    req = urllib.request.Request(url, method=method)
    try:
        with urllib.request.urlopen(req) as resp:
            body = resp.read().decode("utf-8")
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"{method} {url} failed: HTTP {exc.code}: {detail}") from exc
    except urllib.error.URLError as exc:
        raise RuntimeError(f"{method} {url} failed: {exc}") from exc

    try:
        data = json.loads(body)
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"{method} {url} returned invalid JSON: {body[:200]}") from exc

    if isinstance(data, dict) and data.get("ok") is False:
        raise RuntimeError(f"{method} {url} returned error: {data.get('error') or data}")
    return data


def walk_tags(nodes):
    for node in nodes or []:
        yield node
        yield from walk_tags(node.get("children") or [])


def resolve_tag_id(base_url, tag_id, tag_name):
    if tag_id:
        return tag_id
    tags_data = request_json(base_url, "/api/v1/tags")
    matches = [tag for tag in walk_tags(tags_data.get("tags") or []) if str(tag.get("name") or "") == tag_name]
    if not matches:
        raise RuntimeError(f"Tag not found: {tag_name}")
    if len(matches) > 1:
        ids = ", ".join(str(tag.get("id") or "") for tag in matches)
        raise RuntimeError(f"Multiple tags matched '{tag_name}': {ids}")
    resolved = str(matches[0].get("id") or "")
    if not resolved:
        raise RuntimeError(f"Matched tag missing id: {matches[0]}")
    return resolved


def main():
    parser = argparse.ArgumentParser(description="Unbind a file from a webcool tag")
    parser.add_argument("--base-url", default="http://127.0.0.1:18093", help="Base URL of the running webcool server")
    parser.add_argument("--tag-id", help="Tag id to unbind from")
    parser.add_argument("--tag-name", help="Exact tag name to resolve and unbind from")
    parser.add_argument("--file", required=True, help="Relative file path to unbind from the tag")
    args = parser.parse_args()

    if not args.tag_id and not args.tag_name:
        raise RuntimeError("Either --tag-id or --tag-name is required")

    tag_id = resolve_tag_id(args.base_url, args.tag_id, args.tag_name)
    result = request_json(
        args.base_url,
        "/api/v1/tags/unbind",
        {"tag_id": tag_id, "file": args.file},
        method="POST",
    )
    print(json.dumps({
        "ok": True,
        "tag_id": tag_id,
        "file": args.file,
        "response": result,
    }, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        print(str(exc), file=sys.stderr)
        sys.exit(1)