# -*- coding: utf-8 -*-
# MAY DOC VieNeu-TTS v3 Nano - chuong trinh RIENG (chay bang venv_vieneu, hoac vieneu_may.exe khi dong goi).
# App goi qua stdin/stdout, moi dong 1 lenh JSON:
#   vao : {"id": 1, "chu": "...", "giong": "Mỹ Duyên", "toc": 1.5, "buoc": 8}
#   ra  : @@{"id": 1, "wav": "<base64 WAV 16-bit 24 kHz>", "giay": 0.6}    (dong khong bat dau bang @@ -> bo qua)
# Mo hinh: thu muc giong_vieneu/nano (ban dong goi) hoac bo nho HuggingFace da tai (ban chay thu).
import sys, os, json, time, base64, io

os.environ.setdefault("HF_HUB_DISABLE_SYMLINKS_WARNING", "1")
# 10/10: ban dong goi (.exe) bo qua PYTHONUTF8 -> stdin/stdout mac dinh cp1252, khong ghi duoc chu Viet ("Mỹ Duyên")
for _l in (sys.stdin, sys.stdout, sys.stderr):
    try:
        _l.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass


def tra(d):
    sys.stdout.write("@@" + json.dumps(d, ensure_ascii=False) + "\n")
    sys.stdout.flush()


def thu_muc_goc():
    return os.path.dirname(os.path.abspath(sys.executable if getattr(sys, "frozen", False) else __file__))


def mo_bo_doc():
    from vieneu import Vieneu
    nhan = max(2, min(4, (os.cpu_count() or 4) // 3))          # it luong: khong tranh CPU voi game
    nano = os.path.join(thu_muc_goc(), "giong_vieneu", "nano")
    if not os.path.isdir(nano):
        nano = os.path.join(thu_muc_goc(), "nano")
    if os.path.isfile(os.path.join(nano, "vector_estimator.onnx")):
        return Vieneu(mode="v3nano", onnx_dir=nano, threads=nhan, steps=8)
    os.environ["HF_HUB_OFFLINE"] = "1"                          # da tai roi -> khong hoi mang
    try:
        return Vieneu(mode="v3nano", threads=nhan, steps=8)
    except Exception:
        os.environ.pop("HF_HUB_OFFLINE", None)                  # chua tai -> tai ve
        return Vieneu(mode="v3nano", threads=nhan, steps=8)


def wav_16bit(a, tan_so):
    import numpy as np, wave
    a = np.asarray(a, dtype=np.float32).reshape(-1)
    dinh = float(np.max(np.abs(a))) if a.size else 0.0
    if dinh > 0:
        a = a * min(4.0, 0.92 / dinh)                           # can am luong ngang giong cu
    pcm = (np.clip(a, -1, 1) * 32767).astype("<i2").tobytes()
    buf = io.BytesIO()
    with wave.open(buf, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(tan_so)
        w.writeframes(pcm)
    return buf.getvalue()


def main():
    t0 = time.time()
    try:
        tts = mo_bo_doc()
        cac = [ten for _, ten in tts.list_preset_voices()]
        tts.infer("Xin chào.", voice=cac[0] if cac else None)          # lam nong
    except Exception as e:
        tra({"loi": f"{type(e).__name__}: {e}"})
        return
    tra({"san": True, "giong": cac, "giay_mo": round(time.time() - t0, 1)})
    for dong in sys.stdin:
        dong = dong.strip()
        if not dong:
            continue
        try:
            yc = json.loads(dong)
        except Exception:
            continue
        if yc.get("thoat"):
            break
        try:
            t = time.time()
            a = tts.infer(yc["chu"], voice=yc.get("giong"), speed=float(yc.get("toc", 1.0)),
                          steps=int(yc.get("buoc", 8)))
            tra({"id": yc.get("id"), "wav": base64.b64encode(wav_16bit(a, tts.sample_rate)).decode(),
                 "giay": round(time.time() - t, 2)})
        except Exception as e:
            tra({"id": yc.get("id"), "loi": f"{type(e).__name__}: {e}"})


if __name__ == "__main__":
    main()
