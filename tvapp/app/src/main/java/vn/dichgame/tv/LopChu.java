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
    }

    public void datPhuDe(String c, float[] k) {
        chu = c == null ? "" : c;
        if (k != null) khung = k;
        postInvalidate();
    }

    public void xoa() {
        chu = "";
        postInvalidate();
    }

    @Override
    protected void onDraw(Canvas cv) {
        if (chu.isEmpty()) return;
        int w = getWidth(), h = getHeight();
        if (w <= 0 || h <= 0) return;
        float co = h * 0.034f;
        net.setTextSize(co);
        vien.setTextSize(co);
        vien.setStrokeWidth(co * 0.24f);
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
}
