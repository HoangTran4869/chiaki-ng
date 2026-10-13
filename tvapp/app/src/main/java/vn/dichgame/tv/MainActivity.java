package vn.dichgame.tv;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.NetworkInterface;
import java.util.Collections;

/** Man hinh chinh app TV: trang thai ket noi + cap quyen chu noi + chu thu. Chay ngam = DichService. */
public class MainActivity extends Activity {
    static final String PHIEN_BAN = "0.8";
    private TextView trangThai;
    private Button nutQuyen;
    private final Handler h = new Handler(Looper.getMainLooper());

    private Button nut(LinearLayout l, String chu, View.OnClickListener bam) {
        Button b = new Button(this);
        b.setText(chu);
        b.setTextSize(22);
        b.setAllCaps(false);
        b.setOnClickListener(bam);
        l.addView(b, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));
        return b;
    }

    static String ipTv() {
        try {
            for (NetworkInterface ni : Collections.list(NetworkInterface.getNetworkInterfaces())) {
                for (InetAddress a : Collections.list(ni.getInetAddresses())) {
                    if (!a.isLoopbackAddress() && a instanceof Inet4Address) return a.getHostAddress();
                }
            }
        } catch (Throwable ignored) { }
        return "?";
    }

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        LinearLayout l = new LinearLayout(this);
        l.setOrientation(LinearLayout.VERTICAL);
        l.setPadding(80, 50, 80, 50);
        l.setBackgroundColor(Color.rgb(20, 24, 40));
        TextView dau = new TextView(this);
        dau.setTextColor(Color.WHITE);
        dau.setTextSize(30);
        dau.setText("DỊCH GAME TV  ·  bản " + PHIEN_BAN);
        l.addView(dau);
        trangThai = new TextView(this);
        trangThai.setTextColor(Color.rgb(200, 220, 255));
        trangThai.setTextSize(22);
        trangThai.setPadding(0, 20, 0, 30);
        l.addView(trangThai);
        nutQuyen = nut(l, "🔓 Cấp quyền hiện chữ trên hình HDMI (cần làm 1 lần)", v -> {
            try {
                startActivity(new Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                        Uri.parse("package:" + getPackageName())));
            } catch (Throwable ignored) { }
        });
        Button thu = nut(l, "👀 Hiện chữ thử 5 giây", v -> {
            DichService s = DichService.dangChay;
            if (s != null) s.chuThu("Chữ thử: Xin chào! Phụ đề tiếng Việt sẽ hiện ở đây.");
        });
        nut(l, "🧹 Quên các máy tính đã kết nối", v -> {
            DichService s = DichService.dangChay;
            if (s != null) s.quenMayTinh();
        });
        TextView hd = new TextView(this);
        hd.setTextColor(Color.rgb(255, 224, 138));
        hd.setTextSize(19);
        hd.setGravity(Gravity.START);
        hd.setPadding(0, 30, 0, 0);
        hd.setText("Cách dùng: để app này chạy, bấm nút Nguồn vào (Input) trên remote → chọn cổng HDMI của PS5. "
                + "Mở app dịch trên máy tính: máy tính tự tìm TV. Lần đầu TV hỏi Đồng ý → bấm OK.\n"
                + "Mẹo: vào cài đặt âm thanh của game, giảm Voice Volume (giọng nói) để giọng Việt nghe rõ, "
                + "vẫn giữ nhạc và hiệu ứng.");
        l.addView(hd);
        setContentView(l);
        thu.requestFocus();
        startForegroundService(new Intent(this, DichService.class));
        capNhat();
    }

    private void capNhat() {
        boolean quyen = Settings.canDrawOverlays(this);
        nutQuyen.setVisibility(quyen ? View.GONE : View.VISIBLE);
        trangThai.setText((quyen ? "" : "⚠ Chưa có quyền hiện chữ trên hình HDMI — bấm nút bên dưới để cấp.\n")
                + "Tên TV: " + DichService.tenTv(this) + "   ·   Địa chỉ: " + ipTv() + "\n" + DichService.trangThai);
        h.postDelayed(this::capNhat, 1000);
    }

    @Override
    protected void onResume() {
        super.onResume();
        startForegroundService(new Intent(this, DichService.class));
    }

    @Override
    protected void onDestroy() {
        h.removeCallbacksAndMessages(null);
        super.onDestroy();
    }
}
