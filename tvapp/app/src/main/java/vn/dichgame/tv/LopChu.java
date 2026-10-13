package vn.dichgame.tv;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.text.Layout;
import android.text.StaticLayout;
import android.text.TextPaint;
import android.view.View;

/** Lop chu NOI tren hinh HDMI: phu de tieng Viet (chu trang, vien xam dam) dat dung khung phu de. */
public class LopChu extends View {
    private String chu = "";
    private float[] khung = null;          // ti le 0-1: x1, y1, x2, y2 (theo hinh PS5 = ca man hinh TV)
    private String[] dongChu = null;       // 13/10: tung dong de dung len tung dong phu de Anh
    private float[][] dongHop = null;
    private float tiLe = 1f;                 // A- / A+ tren app may tinh
    private String[] bbChu = null;           // 13/10 buoc 3: ban dich toan man hinh (de len cho chu goc)
    private float[][] bbHop = null;
    private final Paint nenBb = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final TextPaint net = new TextPaint(Paint.ANTI_ALIAS_FLAG);
    private final TextPaint vien = new TextPaint(Paint.ANTI_ALIAS_FLAG);

    public LopChu(Context c) {
        super(c);
        net.setColor(Color.WHITE);
        net.setTypeface(Typeface.DEFAULT_BOLD);
        vien.setColor(Color.rgb(58, 58, 58));
        vien.setTypeface(Typeface.DEFAULT_BOLD);
        vien.setStyle(Paint.Style.STROKE);
        vien.setStrokeJoin(Paint.Join.ROUND);
        nenBb.setColor(Color.argb(232, 24, 26, 32));
    }

    public void datBongBong(String[] c, float[][] h) {
        bbChu = c;
        bbHop = h;
        postInvalidate();
    }

    /** Moi doan dich: o nen toi bo goc DE DUNG LEN cho chu goc (rong it nhat bang doan goc), chu trang. */
    private void veBongBong(Canvas cv, int w, int h) {
        if (bbChu == null || bbHop == null) return;
        float co = h * 0.024f * tiLe;
        TextPaint p = new TextPaint(Paint.ANTI_ALIAS_FLAG);
        p.setColor(Color.WHITE);
        p.setTextSize(co);
        int n = Math.min(bbChu.length, bbHop.length);
        for (int i = 0; i < n; i++) {
            float x1 = bbHop[i][0] * w, y1 = bbHop[i][1] * h, x2 = bbHop[i][2] * w;
            int rong = (int) Math.max(x2 - x1, w * 0.18f);
            StaticLayout l = StaticLayout.Builder.obtain(bbChu[i], 0, bbChu[i].length(), p, rong)
                    .setAlignment(Layout.Alignment.ALIGN_NORMAL).build();
            float pad = co * 0.35f;
            float trai = Math.max(pad, Math.min(w - rong - pad, x1));
            float tren = Math.max(pad, Math.min(h - l.getHeight() - pad, y1));
            cv.drawRoundRect(trai - pad, tren - pad, trai + rong + pad, tren + l.getHeight() + pad, pad, pad, nenBb);
            cv.save();
            cv.translate(trai, tren);
            l.draw(cv);
            cv.restore();
        }
    }

    public void datPhuDe(String c, float[] k) {
        datPhuDe(c, k, null, null);
    }

    public void datPhuDe(String c, float[] k, String[] dc, float[][] dh, float tl) {
        tiLe = tl > 0 ? tl : 1f;
        datPhuDe(c, k, dc, dh);
    }

    public void datPhuDe(String c, float[] k, String[] dc, float[][] dh) {
        chu = c == null ? "" : c;
        if (k != null) khung = k;
        dongChu = dc;
        dongHop = dh;
        postInvalidate();
    }

    public void xoa() {
        chu = "";
        postInvalidate();
    }

    @Override
    protected void onDraw(Canvas cv) {
        int w = getWidth(), h = getHeight();
        if (w <= 0 || h <= 0) return;
        veBongBong(cv, w, h);
        if (chu.isEmpty()) return;
        if (dongChu != null && dongHop != null && dongChu.length > 0) {
            veTungDong(cv, w, h);
            return;
        }
        float co = h * 0.034f * tiLe;
        net.setTextSize(co);
        vien.setTextSize(co);
        vien.setStrokeWidth(co * 0.48f);       // vien DAY: cac chu chong len nhau thanh nen om sat chu
        float x1 = 0.12f * w, x2 = 0.88f * w, yGiua = 0.86f * h;
        if (khung != null) {
            x1 = khung[0] * w;
            x2 = khung[2] * w;
            yGiua = (khung[1] + khung[3]) / 2f * h;
        }
        int rong = (int) Math.max(w * 0.4f, x2 - x1);
        float xGiua = (x1 + x2) / 2f;
        float trai = Math.max(0, Math.min(w - rong, xGiua - rong / 2f));
        StaticLayout lv = StaticLayout.Builder.obtain(chu, 0, chu.length(), vien, rong)
                .setAlignment(Layout.Alignment.ALIGN_CENTER).build();
        StaticLayout ln = StaticLayout.Builder.obtain(chu, 0, chu.length(), net, rong)
                .setAlignment(Layout.Alignment.ALIGN_CENTER).build();
        float tren = yGiua - ln.getHeight() / 2f;
        tren = Math.max(4, Math.min(h - ln.getHeight() - 4, tren));
        cv.save();
        cv.translate(trai, tren);
        lv.draw(cv);
        ln.draw(cv);
        cv.restore();
    }

    /** 2 dong Anh -> 2 dong Viet, moi dong de dung len dong Anh; cung 1 co chu; dong qua dai thi thu nho ca khoi. */
    private void veTungDong(Canvas cv, int w, int h) {
        int n = Math.min(dongChu.length, dongHop.length);
        float caoMin = Float.MAX_VALUE;
        for (int i = 0; i < n; i++) caoMin = Math.min(caoMin, (dongHop[i][3] - dongHop[i][1]) * h);
        float co = Math.max(h * 0.018f, caoMin * 0.82f);
        net.setTextSize(co);
        float thu = 1f;
        for (int i = 0; i < n; i++) {
            float doRong = net.measureText(dongChu[i]);
            float gioiHan = Math.max((dongHop[i][2] - dongHop[i][0]) * w * 1.3f, w * 0.3f);
            if (doRong > gioiHan) thu = Math.min(thu, gioiHan / doRong);
        }
        co = co * Math.max(0.7f, thu) * tiLe;
        net.setTextSize(co);
        vien.setTextSize(co);
        vien.setStrokeWidth(co * 0.48f);
        Paint.FontMetrics fm = net.getFontMetrics();
        for (int i = 0; i < n; i++) {
            String t = dongChu[i];
            float doRong = net.measureText(t);
            float cx = (dongHop[i][0] + dongHop[i][2]) / 2f * w;
            float cy = (dongHop[i][1] + dongHop[i][3]) / 2f * h;
            float x = Math.max(8, Math.min(w - doRong - 8, cx - doRong / 2f));
            float y = cy - (fm.ascent + fm.descent) / 2f;
            cv.drawText(t, x, y, vien);
            cv.drawText(t, x, y, net);
        }
    }
}
