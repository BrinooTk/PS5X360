"""Package only reviewed AutoLog source/tools, never credentials or queued logs."""
from pathlib import Path
import hashlib
import zipfile

root = Path(__file__).resolve().parents[1]
names = ["LICENSE", "docs/AUTOLOG.md", "tools/autolog.py", "tools/check-autolog.py",
         "tools/Configure AutoLog.bat", "tools/Start AutoLog.bat",
         "server/autolog/package.json", "server/autolog/package-lock.json",
         "server/autolog/wrangler.toml", "server/autolog/src/index.js",
         "server/autolog/index.test.js", "server/autolog/deploy.cmd",
         "server/autolog/set-webhook.cmd", "tools/provision-autolog.py",
         "tools/check-relay-live.py", "server/autolog/src/worker.js",
         "server/autolog/runtime.test.js", "tools/Enable AutoLog.bat"]
destination = root / "dist/PS5X360-AutoLog-v1.0.0.zip"
destination.parent.mkdir(exist_ok=True)
with zipfile.ZipFile(destination, "w", zipfile.ZIP_DEFLATED) as output:
    for name in names:
        output.write(root / name, "PS5X360-AutoLog/" + name)
with zipfile.ZipFile(destination) as output:
    assert output.testzip() is None
    assert len(output.namelist()) == len(names)
    assert all(name.startswith("PS5X360-AutoLog/") for name in output.namelist())
digest = hashlib.sha256(destination.read_bytes()).hexdigest()
destination.with_suffix(".zip.sha256").write_text(digest + "  " + destination.name + "\n")
print(destination)
print("Verified archive: " + str(len(names)) + " files; SHA-256 " + digest)
