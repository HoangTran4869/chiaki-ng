# -*- coding: utf-8 -*-
# DONG GOI 5 GIONG DOC HAY (VieNeu Nano) CHO MAC (chip Apple) - chay tren GitHub Actions (macos-14).
# Giong ban Windows (dong_goi_vieneu.py) nhung: file chay "vieneu_may" (khong .exe), zip GIU quyen chay,
# ra giong_vieneu_mac.json (link Release cua kho chiaki-ng).
import os, sys, json, shutil, hashlib, subprocess, zipfile, time, tempfile, threading, base64

GOC = os.path.dirname(os.path.abspath(__file__))
RA = os.path.join(GOC, "ra")
LAM = os.path.join(GOC, "build_vieneu")
PHIEN_BAN_GOI = 1
TEN_ZIP = f"GiongVieNeu_mac_v{PHIEN_BAN_GOI}.zip"
TAG = f"giong-vieneu-mac-v{PHIEN_BAN_GOI}"
LINK = f"https://github.com/HoangTran4869/chiaki-ng/releases/download/{TAG}/{TEN_ZIP}"
REPO_NANO = "pnnbao-ump/VieNeu-TTS-v3-Nano"
FILE_NANO = ["text_encoder.onnx", "duration_predictor.onnx", "vector_estimator.onnx",
             "codec_decoder.onnx", "config.json", "constants.npz"]
BO_THU_VIEN = ["gradio", "gradio_client", "hf_gradio", "fastapi", "uvicorn", "starlette", "pandas", "sklearn",
               "numba", "llvmlite", "librosa", "torch", "torchaudio", "matplotlib", "IPython", "pooch", "joblib",
               "tkinter", "pytest", "neucodec", "perth"]
BO_THEM = ["PIL", "hf_xet", "tokenizers", "sqlite3"]


def buoc(so, chu):
    print(f"\n========== BUOC {so}: {chu} ==========", flush=True)


def dong_goi(muc=2):
    shutil.rmtree(LAM, ignore_errors=True)
    os.makedirs(LAM, exist_ok=True)
    lenh = [sys.executable, "-m", "PyInstaller", "--noconfirm", "--clean", "--onedir", "--console",
            "--name", "vieneu_may", "--distpath", os.path.join(LAM, "dist"), "--workpath", os.path.join(LAM, "build"),
            "--specpath", LAM, "--collect-all", "vieneu", "--collect-all", "vieneu_utils", "--collect-all", "sea_g2p",
            "--collect-binaries", "onnxruntime", "--hidden-import", "vieneu.v3nano"]
    for t in (BO_THU_VIEN if muc >= 1 else []) + (BO_THEM if muc >= 2 else []):
        lenh += ["--exclude-module", t]
    lenh.append(os.path.join(GOC, "vieneu_may.py"))
    subprocess.run(lenh, cwd=GOC, check=True)
    thu_muc = os.path.join(LAM, "dist", "vieneu_may")
    if not os.path.isfile(os.path.join(thu_muc, "vieneu_may")):
        raise SystemExit("LOI: khong thay vieneu_may sau khi dong goi.")
    return thu_muc


def chep_mo_hinh(thu_muc):
    from huggingface_hub import hf_hub_download
    nano = os.path.join(thu_muc, "nano")
    os.makedirs(nano, exist_ok=True)
    for ten in FILE_NANO:
        p = hf_hub_download(REPO_NANO, ten)
        shutil.copyfile(p, os.path.join(nano, ten))
        print(f"   {ten}: {os.path.getsize(p) // 1_000_000} MB")


_THIEU = [False]


def chay_thu(thu_muc):
    hf_trong = tempfile.mkdtemp(prefix="hf_trong_")
    env = dict(os.environ, HF_HOME=hf_trong, HF_HUB_OFFLINE="1", PYTHONUTF8="1", PYTHONIOENCODING="utf-8")
    p = subprocess.Popen([os.path.join(thu_muc, "vieneu_may")], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE, cwd=thu_muc, env=env, text=True, encoding="utf-8", errors="replace")
    loi = []
    threading.Thread(target=lambda: loi.extend(p.stderr), daemon=True).start()
    kq = {}

    def doc():
        for dong in p.stdout:
            if dong.startswith("@@"):
                d = json.loads(dong[2:])
                if d.get("san"):
                    kq["san"] = d
                    p.stdin.write(json.dumps({"id": 1, "chu": "Xin chào, chúc bạn chơi game vui vẻ.",
                                              "giong": "Mỹ Duyên", "toc": 1.5, "buoc": 8}, ensure_ascii=False) + "\n")
                    p.stdin.flush()
                elif d.get("id") == 1 or d.get("loi"):
                    kq["xong"] = d
                    return
    t = threading.Thread(target=doc, daemon=True)
    t.start()
    t.join(300)
    try:
        p.stdin.write('{"thoat": true}\n')
        p.stdin.flush()
    except Exception:
        pass
    time.sleep(1)
    if p.poll() is None:
        p.kill()
    shutil.rmtree(hf_trong, ignore_errors=True)
    x = kq.get("xong") or {}
    if not x.get("wav"):
        print("   !!! CHUA DOC DUOC:", x.get("loi") or "(khong tra loi)")
        print("   ---- loi ----\n   " + "".join(loi)[-3000:].replace("\n", "\n   "))
        _THIEU[0] = any(k in "".join(loi) for k in ("ModuleNotFoundError", "ImportError", "No module named"))
        return False
    am = base64.b64decode(x["wav"])
    os.makedirs(RA, exist_ok=True)
    with open(os.path.join(RA, "thu_giong_mac.wav"), "wb") as f:
        f.write(am)
    print(f"   OK - san sang sau {kq['san'].get('giay_mo')}s, {len(kq['san'].get('giong', []))} giong: "
          f"{kq['san'].get('giong')}, doc thu {x.get('giay')}s ({len(am) // 1000} KB)")
    return True


def nen(thu_muc):
    os.makedirs(RA, exist_ok=True)
    ra = os.path.join(RA, TEN_ZIP)
    with zipfile.ZipFile(ra, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for g, cac_tm, cac in os.walk(thu_muc):
            for ten in cac:
                duong = os.path.join(g, ten)
                if os.path.islink(duong):          # PyInstaller co the tao link -> chep that
                    duong_that = os.path.realpath(duong)
                    z.write(duong_that, os.path.join("giong_vieneu", os.path.relpath(duong, thu_muc)))
                else:
                    z.write(duong, os.path.join("giong_vieneu", os.path.relpath(duong, thu_muc)))   # giu quyen chay
    du_lieu = open(ra, "rb").read()
    tt = {"phien_ban": PHIEN_BAN_GOI, "url": LINK, "sha256": hashlib.sha256(du_lieu).hexdigest(),
          "dung_luong": len(du_lieu), "ngay": time.strftime("%Y-%m-%d")}
    with open(os.path.join(RA, "giong_vieneu_mac.json"), "w", encoding="utf-8") as f:
        json.dump(tt, f, ensure_ascii=False, indent=2)
    return ra, len(du_lieu)


def main():
    os.chdir(GOC)
    buoc(1, "Dong goi may doc VieNeu (Mac)")
    for muc in (2, 1, 0):
        thu_muc = dong_goi(muc)
        buoc(2, f"Chep mo hinh Nano (muc {muc})")
        chep_mo_hinh(thu_muc)
        buoc(3, "Chay thu nhu may nguoi dung (khong mang)")
        if chay_thu(thu_muc):
            break
        if not _THIEU[0] or muc == 0:
            raise SystemExit("!!! may doc trong goi khong chay")
    tong = sum(os.path.getsize(os.path.join(g, t)) for g, _, cac in os.walk(thu_muc) for t in cac)
    print(f"   Thu muc goi: {tong // 1_000_000} MB")
    buoc(4, "Nen goi")
    ra, co = nen(thu_muc)
    print(f"XONG: {ra} ({co // 1_000_000} MB)")


if __name__ == "__main__":
    main()
