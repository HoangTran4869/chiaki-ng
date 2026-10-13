package vn.dichgame.tv;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.PixelFormat;
import android.media.AudioAttributes;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.media.AudioFormat;
import android.media.AudioTrack;
import android.media.tv.TvContract;
import android.media.tv.TvInputInfo;
import android.media.tv.TvInputManager;
import android.media.tv.TvView;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
import android.speech.tts.TextToSpeech;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.util.List;
import java.util.Locale;

/**
 * DICH GAME TV - BAN THU 0.1 (13/10): kiem tra TV lam duoc gi truoc khi lam app that.
 * 1) Liet ke cac cong TV (HDMI...)  2) Mo HDMI BEN TRONG app (TvView) + ve chu thu de len
 * 3) Ha / tra tieng HDMI  4) Phat tieng bip + doc thu  5) Chu noi tren HDMI (overlay, cach cua doi thu).
 */
public class MainActivity extends Activity {
    private FrameLayout goc;
    private TvView tvView;
    private LinearLayout menu;
    private ScrollView cuonMenu;
    private TextView nhatKy;
    private TextView chuThu;
    private TextToSpeech tts;
    private TextView chuNoi;
    private Button nutDau;
    private final StringBuilder log = new StringBuilder();

    private void ghi(String s) {
        log.append(s).append('\n');
        if (nhatKy != null) nhatKy.setText(log.toString());
    }

    private Button nut(String chu, View.OnClickListener bam) {
        Button b = new Button(this);
        b.setText(chu);
        b.setTextSize(20);
        b.setAllCaps(false);
        b.setOnClickListener(bam);
        b.setFocusable(true);
        menu.addView(b, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));
        if (nutDau == null) nutDau = b;
        return b;
    }

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        goc = new FrameLayout(this);
        goc.setBackgroundColor(Color.BLACK);
        setContentView(goc);

        tvView = new TvView(this);
        tvView.setVisibility(View.GONE);
        goc.addView(tvView, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));

        chuThu = new TextView(this);
        chuThu.setText("Chữ dịch thử: Xin chào! Đây là chữ tiếng Việt vẽ đè lên hình PS5.");
        chuThu.setTextColor(Color.WHITE);
        chuThu.setTextSize(34);
        chuThu.setShadowLayer(10, 0, 0, Color.BLACK);
        chuThu.setGravity(Gravity.CENTER);
        chuThu.setVisibility(View.GONE);
        FrameLayout.LayoutParams lpc = new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.BOTTOM);
        lpc.bottomMargin = 120;
        goc.addView(chuThu, lpc);

        cuonMenu = new ScrollView(this);
        cuonMenu.setBackgroundColor(Color.argb(235, 20, 24, 40));
        menu = new LinearLayout(this);
        menu.setOrientation(LinearLayout.VERTICAL);
        menu.setPadding(40, 30, 40, 30);
        cuonMenu.addView(menu);
        goc.addView(cuonMenu, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));

        TextView dau = new TextView(this);
        dau.setTextColor(Color.WHITE);
        dau.setTextSize(24);
        dau.setText("DỊCH GAME TV — BẢN THỬ 0.3\n" + Build.MANUFACTURER + " " + Build.MODEL
                + " · Android " + Build.VERSION.RELEASE + " (SDK " + Build.VERSION.SDK_INT + ")");
        menu.addView(dau);

        // ---- 1) cac cong TV ----
        TvInputManager tim = (TvInputManager) getSystemService(Context.TV_INPUT_SERVICE);
        if (tim == null) {
            ghi("❌ TV không cho app xem danh sách cổng (không có TV_INPUT_SERVICE)");
        } else {
            List<TvInputInfo> ds = tim.getTvInputList();
            ghi("Số cổng TV thấy được: " + ds.size());
            for (TvInputInfo info : ds) {
                CharSequence ten = info.loadLabel(this);
                boolean hdmi = info.getType() == TvInputInfo.TYPE_HDMI;
                ghi("• " + ten + " | loại " + info.getType() + (hdmi ? " (HDMI)" : "")
                        + " | passthrough=" + info.isPassthroughInput() + " | id=" + info.getId());

            }
        }
        tvView.setCallback(new TvView.TvInputCallback() {
            @Override public void onConnectionFailed(String inputId) { ghi("❌ Không kết nối được cổng: " + inputId); }
            @Override public void onDisconnected(String inputId) { ghi("⚠ Cổng ngắt: " + inputId); }
            @Override public void onVideoAvailable(String inputId) { ghi("✅ CÓ HÌNH HDMI trong app: " + inputId); }
            @Override public void onVideoUnavailable(String inputId, int lyDo) {
                ghi("⚠ Chưa có hình (lý do " + lyDo + "): " + inputId);
            }
        });

        // ---- 2) tieng ----
        // 0.3 (13/10): anh chot CACH 2 (chu noi, HDMI giu nguyen - khong tang do tre). Thu cach HA TIENG GAME.
        TextView hd3 = new TextView(this);
        hd3.setTextColor(Color.rgb(255, 224, 138));
        hd3.setTextSize(18);
        hd3.setText("THỬ HẠ TIẾNG GAME: bấm 1 nút A/B/C → trong 8 giây chuyển sang HDMI 1 (nút Nguồn vào) và mở"
                + " đoạn game có tiếng. Chữ nổi sẽ báo lúc bắt đầu thử: nghe xem tiếng GAME có nhỏ đi trong lúc"
                + " app phát giọng không. (Cần bật quyền chữ nổi trước: bấm nút 'Cách 2' 1 lần.)");
        menu.addView(hd3);
        nut("🅰 Thử A: xin TV nhường tiếng (nhỏ bớt)", v -> thuHaTieng("A"));
        nut("🅱 Thử B: xin TV nhường tiếng (tắt hẳn)", v -> thuHaTieng("B"));
        nut("🅲 Thử C: hạ âm lượng TV, giọng đọc kênh riêng", v -> thuHaTieng("C"));
        nut("⚡ Bíp NGAY (đo độ trễ tiếng)", v -> bipNhanh());
        nut("🔈 Phát tiếng bíp 1 giây", v -> phatBip());
        tts = new TextToSpeech(this, st -> ghi(st == TextToSpeech.SUCCESS ? "Giọng đọc của TV: sẵn sàng"
                : "Giọng đọc của TV: KHÔNG có"));
        nut("🗣 Đọc thử tiếng Việt (giọng của TV)", v -> docThu());

        // ---- 3) chu noi (overlay) ----
        nut("🪟 Cách 2: thử CHỮ NỔI trên HDMI", v -> chuNoiBat());
        nut("✖ Tắt chữ nổi", v -> chuNoiTat());

        TextView hd = new TextView(this);
        hd.setTextColor(Color.rgb(255, 224, 138));
        hd.setTextSize(18);
        hd.setText("\nKết quả thử ghi ở cuối trang (kéo xuống). Đang xem hình HDMI: bấm BACK để hiện lại menu này. Ở menu bấm BACK lần nữa để thoát app.\n");
        menu.addView(hd);

        nhatKy = new TextView(this);
        nhatKy.setTextColor(Color.rgb(200, 220, 255));
        nhatKy.setTextSize(17);
        nhatKy.setText(log.toString());
        menu.addView(nhatKy);

        if (nutDau != null) nutDau.requestFocus();
    }

    private void moHdmi(String id, String ten) {
        try {
            Uri uri = TvContract.buildChannelUriForPassthroughInput(id);
            tvView.setVisibility(View.VISIBLE);
            chuThu.setVisibility(View.VISIBLE);
            cuonMenu.setVisibility(View.GONE);
            tvView.tune(id, uri);
            ghi("Đang mở " + ten + " trong app...");
        } catch (Throwable t) {
            ghi("❌ Lỗi mở " + ten + ": " + t);
            cuonMenu.setVisibility(View.VISIBLE);
        }
    }

    private void phatBip() {
        new Thread(() -> {
            try {
                int rate = 44100, n = rate;
                short[] s = new short[n];
                for (int i = 0; i < n; i++) s[i] = (short) (Math.sin(2 * Math.PI * 660 * i / rate) * 8000);
                AudioTrack at = new AudioTrack.Builder()
                        .setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA)
                                .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH).build())
                        .setAudioFormat(new AudioFormat.Builder().setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                                .setSampleRate(rate).setChannelMask(AudioFormat.CHANNEL_OUT_MONO).build())
                        .setBufferSizeInBytes(n * 2).setTransferMode(AudioTrack.MODE_STATIC).build();
                at.write(s, 0, n);
                at.play();
                Thread.sleep(1300);
                at.release();
                runOnUiThread(() -> ghi("Đã phát tiếng bíp (nghe được là app phát âm thanh ra loa TV được)"));
            } catch (Throwable t) {
                runOnUiThread(() -> ghi("❌ Lỗi phát tiếng: " + t));
            }
        }).start();
    }

    private void docThu() {
        if (tts == null) return;
        int kq = tts.setLanguage(new Locale("vi", "VN"));
        if (kq == TextToSpeech.LANG_MISSING_DATA || kq == TextToSpeech.LANG_NOT_SUPPORTED) {
            ghi("TV không có giọng tiếng Việt (không sao: app thật nhận giọng đọc từ máy tính)");
            tts.setLanguage(Locale.US);
            tts.speak("Hello, this is a test.", TextToSpeech.QUEUE_FLUSH, null, "thu");
        } else {
            tts.speak("Xin chào, đây là giọng đọc thử trên ti vi.", TextToSpeech.QUEUE_FLUSH, null, "thu");
            ghi("Đang đọc thử tiếng Việt");
        }
    }

    private void chuNoiBat() {
        if (!Settings.canDrawOverlays(this)) {
            ghi("Chưa có quyền 'hiển thị trên ứng dụng khác' → mở trang cấp quyền, anh BẬT cho Dịch Game TV rồi quay lại bấm lần nữa");
            try {
                startActivity(new Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                        Uri.parse("package:" + getPackageName())));
            } catch (Throwable t) {
                ghi("❌ TV không có trang cấp quyền này (TV khoá quyền chữ nổi): " + t);
            }
            return;
        }
        if (chuNoi != null) return;
        try {
            WindowManager wm = (WindowManager) getSystemService(WINDOW_SERVICE);
            chuNoi = new TextView(this);
            chuNoi.setText("CHỮ NỔI THỬ: nếu thấy dòng này trên hình PS5 là cách 'chữ nổi' chạy được");
            chuNoi.setTextColor(Color.WHITE);
            chuNoi.setTextSize(30);
            chuNoi.setShadowLayer(10, 0, 0, Color.BLACK);
            chuNoi.setGravity(Gravity.CENTER);
            WindowManager.LayoutParams lp = new WindowManager.LayoutParams(
                    WindowManager.LayoutParams.MATCH_PARENT, WindowManager.LayoutParams.WRAP_CONTENT,
                    WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
                    WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE
                            | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN,
                    PixelFormat.TRANSLUCENT);
            lp.gravity = Gravity.BOTTOM;
            lp.y = 120;
            wm.addView(chuNoi, lp);
            ghi("✅ Đã bật chữ nổi. Giờ bấm nút NGUỒN VÀO (Input) trên remote → chọn HDMI 1 xem chữ còn hiện không");
        } catch (Throwable t) {
            chuNoi = null;
            ghi("❌ Lỗi bật chữ nổi: " + t);
        }
    }

    // ================= 0.3: THU HA TIENG GAME + TIENG NHANH =================
    private AudioTrack tiengNhanh;

    private AudioTrack taoTieng(int usage, int giay) {
        int rate = 44100, n = rate * giay;
        short[] s = new short[n];
        for (int i = 0; i < n; i++) {             // giong "noi": 2 not xen ke moi 0,25 giay
            double f = ((i / (rate / 4)) % 2 == 0) ? 520 : 690;
            s[i] = (short) (Math.sin(2 * Math.PI * f * i / rate) * 9000);
        }
        AudioTrack at = new AudioTrack.Builder()
                .setAudioAttributes(new AudioAttributes.Builder().setUsage(usage)
                        .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH).build())
                .setAudioFormat(new AudioFormat.Builder().setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                        .setSampleRate(rate).setChannelMask(AudioFormat.CHANNEL_OUT_MONO).build())
                .setBufferSizeInBytes(n * 2).setTransferMode(AudioTrack.MODE_STATIC).build();
        at.write(s, 0, n);
        return at;
    }

    private void bipNhanh() {
        try {
            if (tiengNhanh == null) {                // tao san 1 lan -> lan sau phat ngay
                int rate = 44100, n = rate / 3;
                short[] s = new short[n];
                for (int i = 0; i < n; i++) s[i] = (short) (Math.sin(2 * Math.PI * 880 * i / rate) * 9000);
                tiengNhanh = new AudioTrack.Builder()
                        .setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA).build())
                        .setAudioFormat(new AudioFormat.Builder().setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                                .setSampleRate(rate).setChannelMask(AudioFormat.CHANNEL_OUT_MONO).build())
                        .setPerformanceMode(AudioTrack.PERFORMANCE_MODE_LOW_LATENCY)
                        .setBufferSizeInBytes(n * 2).setTransferMode(AudioTrack.MODE_STATIC).build();
                tiengNhanh.write(s, 0, n);
                ghi("Đã tạo sẵn tiếng bíp nhanh (bấm lại để nghe độ trễ thật)");
            }
            tiengNhanh.stop();
            tiengNhanh.reloadStaticData();
            tiengNhanh.play();
        } catch (Throwable t) {
            ghi("❌ Lỗi bíp nhanh: " + t);
        }
    }

    private void hienNoi(String chu) {
        runOnUiThread(() -> {
            if (!Settings.canDrawOverlays(this)) return;
            if (chuNoi == null) chuNoiBat();
            if (chuNoi != null) chuNoi.setText(chu);
        });
    }

    private void thuHaTieng(String cach) {
        if (!Settings.canDrawOverlays(this)) {
            ghi("Cần bật quyền chữ nổi trước (bấm nút 'Cách 2' 1 lần)");
            chuNoiBat();
            return;
        }
        ghi("Thử " + cach + ": 8 giây nữa bắt đầu - chuyển sang HDMI 1 ngay");
        hienNoi("THỬ " + cach + " sau 8 giây - chuyển sang HDMI 1 và mở đoạn game có tiếng");
        new Thread(() -> {
            AudioManager am = (AudioManager) getSystemService(AUDIO_SERVICE);
            StringBuilder kq = new StringBuilder("Thử " + cach + ": ");
            final int amGoc = am.getStreamVolume(AudioManager.STREAM_MUSIC);
            try {
                Thread.sleep(8000);
                hienNoi("ĐANG THỬ " + cach + ": tiếng GAME có NHỎ ĐI không?");
                AudioFocusRequest xin = null;
                int amCu = am.getStreamVolume(AudioManager.STREAM_MUSIC);
                int usage = AudioAttributes.USAGE_ASSISTANT;
                if (cach.equals("A") || cach.equals("B")) {
                    xin = new AudioFocusRequest.Builder(cach.equals("A")
                            ? AudioManager.AUDIOFOCUS_GAIN_TRANSIENT_MAY_DUCK : AudioManager.AUDIOFOCUS_GAIN_TRANSIENT)
                            .setAudioAttributes(new AudioAttributes.Builder().setUsage(usage)
                                    .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH).build())
                            .build();
                    int r = am.requestAudioFocus(xin);
                    kq.append("xin nhường tiếng = ").append(r == AudioManager.AUDIOFOCUS_REQUEST_GRANTED ? "được" : "bị từ chối (" + r + ")");
                } else {
                    usage = AudioAttributes.USAGE_ALARM;
                    int amMoi = Math.max(1, amCu / 4);
                    am.setStreamVolume(AudioManager.STREAM_MUSIC, amMoi, 0);
                    kq.append("âm lượng TV ").append(amCu).append(" → ").append(amMoi)
                            .append(", kênh báo thức = ").append(am.getStreamVolume(AudioManager.STREAM_ALARM))
                            .append("/").append(am.getStreamMaxVolume(AudioManager.STREAM_ALARM));
                }
                AudioTrack at = taoTieng(usage, 4);
                at.play();
                Thread.sleep(4300);
                at.release();
                if (xin != null) am.abandonAudioFocusRequest(xin);
                if (cach.equals("C")) am.setStreamVolume(AudioManager.STREAM_MUSIC, amCu, 0);
                hienNoi("XONG THỬ " + cach + " - tiếng game đã trả lại bình thường");
                Thread.sleep(3000);
                runOnUiThread(this::chuNoiTat);
            } catch (Throwable t) {
                kq.append(" ❌ lỗi ").append(t);
                try { if (cach.equals("C")) am.setStreamVolume(AudioManager.STREAM_MUSIC, amGoc, 0); }
                catch (Throwable ignored) { }
            }
            runOnUiThread(() -> ghi(kq.toString()));
        }).start();
    }

    private void chuNoiTat() {
        if (chuNoi == null) return;
        try {
            ((WindowManager) getSystemService(WINDOW_SERVICE)).removeView(chuNoi);
        } catch (Throwable ignored) {
        }
        chuNoi = null;
        ghi("Đã tắt chữ nổi");
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent e) {
        if (keyCode == KeyEvent.KEYCODE_BACK && cuonMenu.getVisibility() != View.VISIBLE) {
            cuonMenu.setVisibility(View.VISIBLE);
            if (nutDau != null) nutDau.requestFocus();
            return true;
        }
        return super.onKeyDown(keyCode, e);
    }

    @Override
    protected void onDestroy() {
        chuNoiTat();
        try { tvView.reset(); } catch (Throwable ignored) { }
        if (tts != null) tts.shutdown();
        super.onDestroy();
    }
}
