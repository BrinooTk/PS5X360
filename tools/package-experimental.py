"""Package a test candidate and a byte-identical baseline rollback together.

Build first with tools/build-experimental.sh. Never stages dist/PPSA50011 or
deploys to a console. The baseline archive must already have been preserved.
"""
import hashlib
import json
import shutil
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VERSION = "0.5.8-experimental.1"


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    if VERSION == "0.5.6-experimental.3":
        raise SystemExit("Experimental.3 rejected after a hardware performance regression; do not distribute")
    baseline = ROOT / "build/baselines/v0.5.7"
    identity = json.loads((baseline / "manifest.json").read_text())
    original = baseline / "PPSA50011.zip"
    if digest(original.read_bytes()) != identity[original.name]:
        raise SystemExit("Baseline archive changed; refusing to package")
    binary = ROOT / "build/canary-game-experimental/eboot.bin"
    executable = binary.read_bytes()
    if ("PS5X360 v" + VERSION).encode() not in executable:
        raise SystemExit("Executable does not identify this experimental build")
    output = Path.home() / "Desktop" / ("PS5X360-v" + VERSION)
    output.mkdir(exist_ok=True)
    candidate = output / "PPSA50011-experimental.zip"
    with zipfile.ZipFile(original) as src, zipfile.ZipFile(candidate, "w", zipfile.ZIP_DEFLATED) as dst:
        manifest = json.loads(src.read("PPSA50011/MANIFEST.json"))
        manifest["eboot.bin"] = {"bytes": len(executable), "sha256": digest(executable)}
        replacements = {
            "PPSA50011/eboot.bin": executable,
            "PPSA50011/VERSION": (VERSION + "\n").encode(),
            "PPSA50011/RELEASE_NOTES.md": (ROOT / "docs/releases" / ("v" + VERSION + ".md")).read_bytes(),
            "PPSA50011/RELEASE.json": json.dumps({"version": VERSION, "experimental": True,
                "baseline": "0.5.7", "executable_sha256": digest(executable)}, indent=2).encode(),
            "PPSA50011/MANIFEST.json": json.dumps(manifest, indent=2).encode(),
        }
        for name in src.namelist():
            dst.writestr(name, replacements.get(name, src.read(name)))
    with zipfile.ZipFile(candidate) as z:
        assert z.testzip() is None
        assert all(n.startswith("PPSA50011/") for n in z.namelist())
        assert z.read("PPSA50011/eboot.bin") == executable
        for name, record in manifest.items():
            data = z.read("PPSA50011/" + name)
            assert len(data) == record["bytes"] and digest(data) == record["sha256"], name
    rollback = output / "PPSA50011-rollback-v0.5.7.zip"
    shutil.copy2(original, rollback)
    shutil.copy2(baseline / "PS5X360-AutoLog-v1.0.9-preview.elf", output)
    (output / "LEIA-ME.txt").write_text(
        "BUILD EXPERIMENTAL: v" + VERSION + "\n"
        "Feche o emulador antes de copiar os arquivos.\n"
        "Extraia PPSA50011-experimental.zip e copie os arquivos sobre /data/homebrew/PPSA50011.\n"
        "NAO apague a pasta existente, os jogos, saves ou logs.\n"
        "Para voltar: feche e copie os arquivos do ZIP rollback-v0.5.7 sobre a mesma pasta.\n"
        "A identidade PPSA50011 e a mesma: nao sao dois apps instalados lado a lado.\n"
        "AutoLog 1.0.9 permanece igual e e opcional.\n"
        "PARA QUEM TEM JOGO QUE FUNCIONAVA NA 0.5.5 E PAROU NA 0.5.6:\n"
        "1. Instale esta build e abra o jogo sem mudar nada. Por padrao ela se comporta como a 0.5.5.\n"
        "2. Se o jogo voltou a funcionar: em Detalhes do jogo (Triangulo), aperte R1 para 'Ajustes deste jogo'\n"
        "   e ligue UMA opcao por vez: 'Travas rapidas' e depois 'Memoria de video otimizada'.\n"
        "   Diga qual das duas faz o jogo parar. Essa resposta e o que precisamos.\n"
        "3. Se o jogo NAO voltou a funcionar: mande o log da sessao (pasta PPSA50011/logs).\n"
        "Novidades em teste: configuracoes com submenus, ajustes por jogo, conquistas como notificacao do PS5\n"
        "e pagina de configuracoes pelo celular (QR code na tela de Configuracoes).\n",
        encoding="utf-8")
    symbols = ROOT / "build/symbols" / VERSION
    symbols.mkdir(parents=True, exist_ok=True)
    for name in ("eboot.bin", "eboot.elf", "llvm-pie.elf"):
        shutil.copy2(binary.parent / name, symbols / name)
    results = {"version": VERSION, "baseline_preserved": identity,
               "executable_sha256": digest(executable), "candidate_sha256": digest(candidate.read_bytes()),
               "rollback_sha256": digest(rollback.read_bytes()),
               "checks": ["ASAN/UBSAN shared-memory, invalidation-window and dynamic-buffer production-code harnesses",
                          "launcher preview render of the settings sub-menus and the per-game sheet",
                          "native image validation", "ZIP CRC and every manifest entry", "compiled version marker"],
               "hardware_gameplay": "Not tested at packaging time"}
    (ROOT / "build" / ("TEST-RESULTS-v" + VERSION + ".json")).write_text(json.dumps(results, indent=2))
    print(output)
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
