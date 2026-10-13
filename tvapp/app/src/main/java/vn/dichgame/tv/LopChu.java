package vn.dichgame.tv;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.DashPathEffect;
import android.graphics.Paint;
import android.graphics.Path;
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

    private float[][] bbO = null;            // 0.9: cho dat bong bong (may tinh tinh san, giong cua so app)
    private float bbCo = 1f;

    public void datBongBong(String[] c, float[][] h, float[][] o, float co) {
        bbChu = c;
        bbHop = h;
        bbO = o;
        bbCo = co > 0 ? co : 1f;
        postInvalidate();
    }

    /** 0.9: GIONG cua so app tren may tinh - bong bong trang chu den DAT CANH doan goc (khong de len chu goc, khong
     *  de len loi thoai), duoi nhon chi ve doan goc, vien vang net dut quanh doan goc. */
    private void veBongBong(Canvas cv, int w, int h) {
        if (bbChu == null || bbHop == null) return;
        int n = Math.min(bbChu.length, bbHop.length);
        float co0 = h * 0.024f * bbCo;
        float s = co0 / 18f;
        Paint vang = new Paint(Paint.ANTI_ALIAS_FLAG);
        vang.setColor(Color.rgb(255, 224, 138));
        vang.setStyle(Paint.Style.STROKE);
        vang.setStrokeWidth(Math.max(2f, 2f * s));
        vang.setPathEffect(new DashPathEffect(new float[]{6 * s, 4 * s}, 0));
        Paint nen = new Paint(Paint.ANTI_ALIAS_FLAG);
        nen.setColor(Color.WHITE);
        Paint vienBb = new Paint(Paint.ANTI_ALIAS_FLAG);
        vienBb.setColor(Color.rgb(17, 17, 17));
        vienBb.setStyle(Paint.Style.STROKE);
        vienBb.setStrokeWidth(Math.max(2f, 2f * s));
        Paint bong = new Paint(Paint.ANTI_ALIAS_FLAG);
        bong.setColor(Color.argb(150, 42, 42, 42));
        for (int i = 0; i < n; i++) {
            float nx1 = bbHop[i][0] * w, ny1 = bbHop[i][1] * h, nx2 = bbHop[i][2] * w, ny2 = bbHop[i][3] * h;
            if (bbO == null || i >= bbO.length || bbO[i] == null) {
                veBongBongCu(cv, w, h, bbChu[i], nx1, ny1, nx2);
                continue;
            }
            float x1 = bbO[i][0] * w, y1 = bbO[i][1] * h, x2 = bbO[i][2] * w, y2 = bbO[i][3] * h;
            float pad = 5 * s;
            cv.drawRect(nx1 - pad, ny1 - pad, nx2 + pad, ny2 + pad, vang);
            // chu: vua khung da tinh (font TV hoi khac may tinh -> thu nho dan neu tran)
            TextPaint p = new TextPaint(Paint.ANTI_ALIAS_FLAG);
            p.setColor(Color.rgb(17, 17, 17));
            p.setTypeface(Typeface.DEFAULT_BOLD);
            int rongChu = (int) Math.max(40, (x2 - x1) - 36 * s);
            float caoCho = (y2 - y1) - 24 * s;
            float co = co0;
            StaticLayout l = null;
            for (int t = 0; t < 8; t++) {
                p.setTextSize(co);
                l = StaticLayout.Builder.obtain(bbChu[i], 0, bbChu[i].length(), p, rongChu)
                        .setAlignment(Layout.Alignment.ALIGN_CENTER).build();
                if (l.getHeight() <= caoCho || co < co0 * 0.6f) break;
                co *= 0.92f;
            }
            float r = Math.min(22 * s, (y2 - y1) / 2.5f);
            // duoi nhon chi ve doan goc
            float gx = (nx1 + nx2) / 2f, gy = (ny1 + ny2) / 2f, rd = 8 * s;
            float bx, by, mx, my, ax, ay, cx2, cy2;
            if (gx >= x2 || gx <= x1) {
                bx = gx >= x2 ? x2 : x1;
                by = Math.min(Math.max(gy, y1 + r + rd), y2 - r - rd);
                ax = bx; ay = by - rd; cx2 = bx; cy2 = by + rd;
                mx = gx >= x2 ? nx1 - 6 * s : nx2 + 6 * s;
                my = Math.min(Math.max(by, ny1 + 4), ny2 - 4);
            } else {
                by = gy >= y2 ? y2 : y1;
                bx = Math.min(Math.max(gx, x1 + r + rd), x2 - r - rd);
                ax = bx - rd; ay = by; cx2 = bx + rd; cy2 = by;
                my = gy >= y2 ? ny1 - 6 * s : ny2 + 6 * s;
                mx = Math.min(Math.max(bx, nx1 + 4), nx2 - 4);
            }
            float dx = mx - bx, dy = my - by;
            float dai = (float) Math.max(1.0, Math.hypot(dx, dy));
            float dd = Math.min(dai, 40 * s);
            float tx = bx + dx / dai * dd, ty = by + dy / dai * dd;
            Path duoi = new Path();
            duoi.moveTo(ax, ay);
            duoi.lineTo(tx, ty);
            duoi.lineTo(cx2, cy2);
            duoi.close();
            cv.drawRoundRect(x1 + 4 * s, y1 + 5 * s, x2 + 4 * s, y2 + 5 * s, r, r, bong);
            cv.drawPath(duoi, nen);
            cv.drawPath(duoi, vienBb);
            cv.drawRoundRect(x1, y1, x2, y2, r, r, nen);
            cv.drawRoundRect(x1, y1, x2, y2, r, r, vienBb);
            Paint lap = new Paint(Paint.ANTI_ALIAS_FLAG);          // xoa vach vien o chan duoi
            lap.setColor(Color.WHITE);
            lap.setStrokeWidth(4 * s);
            cv.drawLine(ax, ay, cx2, cy2, lap);
            cv.save();
            cv.translate(x1 + 18 * s, (y1 + y2) / 2f - l.getHeight() / 2f);
            l.draw(cv);
            cv.restore();
        }
    }

    /** Ban cu (may tinh chua cap nhat): o toi de len cho chu goc. */
    private void veBongBongCu(Canvas cv, int w, int h, String chu, float x1, float y1, float x2) {
        float co = h * 0.024f * tiLe;
        TextPaint p = new TextPaint(Paint.ANTI_ALIAS_FLAG);
        p.setColor(Color.WHITE);
        p.setTextSize(co);
        int rong = (int) Math.max(x2 - x1, w * 0.18f);
        StaticLayout l = StaticLayout.Builder.obtain(chu, 0, chu.length(), p, rong)
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
