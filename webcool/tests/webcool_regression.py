#!/usr/bin/env python3

import argparse
import json
import sys
import time
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


def fetch_files(base_url):
    data = request_json(base_url, "/api/v1/files")
    return data.get("files") or []


def fetch_tag_files(base_url, tag_id):
    data = request_json(base_url, "/api/v1/tag-files", {"tag_id": tag_id})
    return data.get("files") or []


def choose_move_candidate(files):
    root_names = {
        str(item.get("name") or "")
        for item in files
        if not str(item.get("folder_path") or "")
    }
    for item in files:
        folder_path = str(item.get("folder_path") or "")
        path = str(item.get("path") or item.get("name") or "")
        name = str(item.get("name") or "")
        if folder_path and path and name and name not in root_names:
            return {
                "path": path,
                "name": name,
                "folder_path": folder_path,
            }
    raise RuntimeError("No movable non-root file found whose basename does not already exist in root")


def choose_file_record(files, path):
    target = str(path or "")
    for item in files:
        current = str(item.get("path") or item.get("name") or "")
        if current == target:
            return item
    return None


def run_move_regression(base_url):
    files = fetch_files(base_url)
    candidate = choose_move_candidate(files)
    original_path = candidate["path"]
    original_folder = candidate["folder_path"]
    root_path = candidate["name"]

    print(f"[move] source: {original_path}")
    request_json(
        base_url,
        "/api/v1/files/move",
        {"file": original_path, "folder": ""},
        method="POST",
    )

    try:
        files_after_move = fetch_files(base_url)
        if choose_file_record(files_after_move, original_path) is not None:
            raise RuntimeError(f"Moved file still present at original path: {original_path}")
        if choose_file_record(files_after_move, root_path) is None:
            raise RuntimeError(f"Moved file missing at root path: {root_path}")
        print(f"[move] moved to root: {root_path}")
    except Exception:
        request_json(
            base_url,
            "/api/v1/files/move",
            {"file": root_path, "folder": original_folder},
            method="POST",
        )
        raise

    request_json(
        base_url,
        "/api/v1/files/move",
        {"file": root_path, "folder": original_folder},
        method="POST",
    )
    files_after_restore = fetch_files(base_url)
    if choose_file_record(files_after_restore, original_path) is None:
        raise RuntimeError(f"Restored file missing at original path: {original_path}")
    if choose_file_record(files_after_restore, root_path) is not None:
        raise RuntimeError(f"Restored file still present at root path: {root_path}")
    print(f"[move] restored: {original_path}")
    return original_path


def run_tag_regression(base_url, file_path):
    tag_name = f"webcool_regression_{int(time.time())}"
    created = request_json(
        base_url,
        "/api/v1/tags/create",
        {"name": tag_name},
        method="POST",
    )
    tag_id = str(created.get("id") or "")
    if not tag_id:
        raise RuntimeError(f"Created tag missing id: {created}")

    print(f"[tag] created: {tag_name} ({tag_id})")

    try:
        request_json(
            base_url,
            "/api/v1/tags/bind",
            {"tag_id": tag_id, "file": file_path},
            method="POST",
        )
        files_after_bind = fetch_tag_files(base_url, tag_id)
        if not any(str(item.get("path") or item.get("name") or "") == file_path for item in files_after_bind):
            raise RuntimeError(f"Bound file missing from tag-files response: {file_path}")
        print(f"[tag] bound: {file_path}")

        request_json(
            base_url,
            "/api/v1/tags/unbind",
            {"tag_id": tag_id, "file": file_path},
            method="POST",
        )
        files_after_unbind = fetch_tag_files(base_url, tag_id)
        if any(str(item.get("path") or item.get("name") or "") == file_path for item in files_after_unbind):
            raise RuntimeError(f"File still present after unbind: {file_path}")
        print(f"[tag] unbound: {file_path}")
    finally:
        request_json(
            base_url,
            "/api/v1/tags/delete",
            {"id": tag_id},
            method="POST",
        )
        print(f"[tag] deleted temp tag: {tag_id}")


def main():
    parser = argparse.ArgumentParser(description="Run webcool regression checks for path-based file move and tag bind flows")
    parser.add_argument("--base-url", default="http://127.0.0.1:18093", help="Base URL of the running webcool server")
    args = parser.parse_args()

    moved_file_path = run_move_regression(args.base_url)
    run_tag_regression(args.base_url, moved_file_path)
    print("Regression checks passed")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        print(f"Regression checks failed: {exc}", file=sys.stderr)
        sys.exit(1)
