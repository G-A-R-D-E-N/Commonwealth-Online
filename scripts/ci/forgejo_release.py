import argparse
import json
import os
import sys
import urllib.error
import urllib.parse
import urllib.request
import uuid


class ReleaseError(Exception):
    pass


def quote(value):
    return urllib.parse.quote(value, safe="")


class Forge:
    def __init__(self, server_url, repository, token, dry_run):
        owner, separator, name = repository.partition("/")
        if not server_url or not separator or not owner or not name or "/" in name:
            raise ReleaseError("RELEASE_SERVER_URL and RELEASE_REPOSITORY (owner/name) are required")
        if not dry_run and not token:
            raise ReleaseError("RELEASE_TOKEN is not set")
        self.repo_url = f"{server_url.rstrip('/')}/api/v1/repos/{quote(owner)}/{quote(name)}"
        self.token = token
        self.dry_run = dry_run

    def call(self, method, path, payload=None, data=None, content_type=None, missing_ok=False):
        url = self.repo_url + path
        if self.dry_run:
            print(f"dry-run: {method} {url}")
            return None
        if payload is not None:
            data = json.dumps(payload).encode("utf-8")
            content_type = "application/json"
        headers = {"Authorization": f"token {self.token}", "Accept": "application/json"}
        if content_type is not None:
            headers["Content-Type"] = content_type
        request = urllib.request.Request(url, data=data, method=method, headers=headers)
        try:
            with urllib.request.urlopen(request, timeout=600) as response:
                body = response.read()
        except urllib.error.HTTPError as error:
            if error.code == 404 and missing_ok:
                return None
            detail = error.read(400).decode("utf-8", "replace").replace(self.token, "<redacted>")
            raise ReleaseError(f"{method} {path} failed: HTTP {error.code} {detail}".rstrip()) from None
        except urllib.error.URLError as error:
            raise ReleaseError(f"{method} {path} failed: {error.reason}") from None
        if not body:
            return None
        try:
            return json.loads(body)
        except ValueError:
            raise ReleaseError(f"{method} {path} returned a non-JSON response") from None


def find_release(forge, tag):
    return forge.call("GET", f"/releases/tags/{quote(tag)}", missing_ok=True)


def require_files(paths):
    for path in paths:
        if not os.path.isfile(path):
            raise ReleaseError(f"asset not found: {path}")


def upload(forge, release, path):
    name = os.path.basename(path)
    release_path = f"/releases/{release['id']}"
    for asset in forge.call("GET", f"{release_path}/assets") or []:
        if asset.get("name") == name:
            forge.call("DELETE", f"{release_path}/assets/{asset['id']}")
    with open(path, "rb") as handle:
        content = handle.read()
    boundary = uuid.uuid4().hex
    data = (
        f"--{boundary}\r\n"
        f'Content-Disposition: form-data; name="attachment"; filename="{name}"\r\n'
        "Content-Type: application/octet-stream\r\n\r\n"
    ).encode("utf-8") + content + f"\r\n--{boundary}--\r\n".encode("utf-8")
    forge.call(
        "POST",
        f"{release_path}/assets?name={quote(name)}",
        data=data,
        content_type=f"multipart/form-data; boundary={boundary}",
    )
    print(f"asset {name}: uploaded")


def publish(forge, args):
    require_files(args.files)
    if args.notes is not None:
        notes = args.notes
    else:
        with open(args.notes_file, encoding="utf-8", newline="") as handle:
            notes = handle.read()
    release = find_release(forge, args.tag)
    if release is None:
        payload = {
            "tag_name": args.tag,
            "name": args.title,
            "body": notes,
            "draft": args.draft,
            "prerelease": args.prerelease,
        }
        if args.target:
            payload["target_commitish"] = args.target
        release = forge.call("POST", "/releases", payload=payload) or {"id": "<new-release-id>"}
        print(f"release {args.tag}: created")
    else:
        payload = {"name": args.title, "body": notes}
        if args.draft:
            payload["draft"] = True
        if args.prerelease:
            payload["prerelease"] = True
        forge.call("PATCH", f"/releases/{release['id']}", payload=payload)
        print(f"release {args.tag}: updated")
    for path in args.files:
        upload(forge, release, path)


def delete(forge, args):
    release = find_release(forge, args.tag)
    if release is not None:
        forge.call("DELETE", f"/releases/{release['id']}")
        print(f"release {args.tag}: deleted")
    if args.cleanup_tag:
        forge.call("DELETE", f"/tags/{quote(args.tag)}", missing_ok=True)


def url(forge, args):
    release = find_release(forge, args.tag)
    if release is None:
        if forge.dry_run:
            return
        raise ReleaseError(f"no release for tag {args.tag}")
    html_url = release.get("html_url")
    if not html_url:
        raise ReleaseError(f"release {args.tag} has no html_url")
    print(html_url)


def build_parser():
    parser = argparse.ArgumentParser(description="Publish CI release assets through the Forgejo/Gitea releases API.")
    parser.add_argument("--dry-run", action="store_true", help="print the API calls without sending them")
    commands = parser.add_subparsers(dest="command", required=True)

    publish_parser = commands.add_parser("publish", help="create or update the release for TAG and replace the named assets")
    publish_parser.add_argument("tag")
    publish_parser.add_argument("files", nargs="+")
    publish_parser.add_argument("--title", required=True)
    notes = publish_parser.add_mutually_exclusive_group(required=True)
    notes.add_argument("--notes")
    notes.add_argument("--notes-file")
    publish_parser.add_argument("--target", help="commit or branch used when creating the tag")
    publish_parser.add_argument("--prerelease", action="store_true")
    publish_parser.add_argument("--draft", action="store_true")
    publish_parser.set_defaults(handler=publish)

    delete_parser = commands.add_parser("delete", help="delete the release for TAG")
    delete_parser.add_argument("tag")
    delete_parser.add_argument("--cleanup-tag", action="store_true")
    delete_parser.set_defaults(handler=delete)

    url_parser = commands.add_parser("url", help="print the html_url of the release for TAG")
    url_parser.add_argument("tag")
    url_parser.set_defaults(handler=url)
    return parser


def main(argv=None):
    args = build_parser().parse_args(argv)
    try:
        forge = Forge(
            os.environ.get("RELEASE_SERVER_URL", ""),
            os.environ.get("RELEASE_REPOSITORY", ""),
            os.environ.get("RELEASE_TOKEN", ""),
            args.dry_run,
        )
        args.handler(forge, args)
    except (ReleaseError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
