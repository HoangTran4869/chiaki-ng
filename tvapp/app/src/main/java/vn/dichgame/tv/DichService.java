package vn.dichgame.tv;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.PixelFormat;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.provider.Settings;
import android.view.WindowManager;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.BufferedInputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetSocketAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.util.HashSet;
import java.util.Set;

/**
 * Chay ngam tren TV: nhan chu tu MAY TINH qua mang nha, ve CHU NOI tren hinh HDMI.
 * - UDP 47801: may tinh hoi "DICHGAME_TIM" -> TV tra loi ten + cong.
 * - TCP 47800: moi goi tin = 4 byte do dai + JSON (UTF-8).
 *   chao {ten, ma} -> chao_lai {dong_y, ten_tv}; phu_de {chu, khung[4]}; xoa; ping.
 * - May tinh moi: hoi Dong y 1 lan (XacNhanActivity), nho trong SharedPreferences.
 */
public class DichService extends Service {
    public static final int CONG_TCP = 47800, CONG_UDP = 47801;
    static volatile String trangThai = "Đang chờ máy tính…";
    static volatile DichService dangChay;
    private static final Object khoaXN = new Object();
    private static Boolean ketQuaXN;

    private final Handler ui = new Handler(Looper.getMainLooper());
    private LopChu lop;
    private Loa loa;
    private volatile boolean chay = true;
    private ServerSocket server;
    private DatagramSocket udp;
    private volatile Socket khach;
    private WifiManager.MulticastLock khoaMc;

    static void traLoiXacNhan(boolean dongY) {
        synchronized (khoaXN) {
            ketQuaXN = dongY;
            khoaXN.notifyAll();
        }
    }

    private SharedPreferences pref() {
        return getSharedPreferences("dich_tv", MODE_PRIVATE);
    }

    static String tenTv(Context c) {
        String t = null;
        try { t = Settings.Global.getString(c.getContentResolver(), "device_name"); } catch (Throwable ignored) { }
        return (t == null || t.isEmpty()) ? Build.MODEL : t;
    }

    @Override
    public void onCreate() {
        super.onCreate();
        dangChay = this;
        NotificationManager nm = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
        nm.createNotificationChannel(new NotificationChannel("dich", "Dịch Game TV", NotificationManager.IMPORTANCE_LOW));
        Notification n = new Notification.Builder(this, "dich").setContentTitle("Dịch Game TV đang chạy")
                .setSmallIcon(android.R.drawable.ic_menu_info_details).build();
        startForeground(1, n);
        try {
            WifiManager wm = (WifiManager) getApplicationContext().getSystemService(WIFI_SERVICE);
            khoaMc = wm.createMulticastLock("dich_tv");
            khoaMc.setReferenceCounted(false);
            khoaMc.acquire();
        } catch (Throwable ignored) { }
        ui.post(this::batLop);
        new Thread(this::chayUdp, "udp").start();
        new Thread(this::chayTcp, "tcp").start();
    }

    @Override
    public int onStartCommand(Intent i, int f, int id) {
        if (lop == null) ui.post(this::batLop);
        return START_STICKY;
    }

    void batLop() {
        if (lop != null || !Settings.canDrawOverlays(this)) return;
        try {
            lop = new LopChu(this);
            WindowManager.LayoutParams lp = new WindowManager.LayoutParams(
                    WindowManager.LayoutParams.MATCH_PARENT, WindowManager.LayoutParams.MATCH_PARENT,
                    WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
                    WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE
                            | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN,
                    PixelFormat.TRANSLUCENT);
            ((WindowManager) getSystemService(WINDOW_SERVICE)).addView(lop, lp);
        } catch (Throwable t) {
            lop = null;
            trangThai = "❌ Không bật được chữ nổi: " + t;
        }
    }

    void chuThu(String c) {
        ui.post(() -> {
            batLop();
            if (lop != null) {
                lop.datPhuDe(c, null);
                ui.postDelayed(() -> { if (lop != null) lop.xoa(); }, 5000);
            }
        });
    }

    private void chayUdp() {
        while (chay) {
            try {
                udp = new DatagramSocket(null);
                udp.setReuseAddress(true);
                udp.setBroadcast(true);
                udp.bind(new InetSocketAddress(CONG_UDP));
                byte[] buf = new byte[512];
                while (chay) {
                    DatagramPacket p = new DatagramPacket(buf, buf.length);
                    udp.receive(p);
                    String s = new String(p.getData(), 0, p.getLength(), StandardCharsets.UTF_8);
                    if (!s.startsWith("DICHGAME_TIM")) continue;
                    JSONObject o = new JSONObject();
                    o.put("loai", "tv");
                    o.put("ten", tenTv(this));
                    o.put("cong", CONG_TCP);
                    byte[] tl = o.toString().getBytes(StandardCharsets.UTF_8);
                    udp.send(new DatagramPacket(tl, tl.length, p.getAddress(), p.getPort()));
                }
            } catch (Throwable t) {
                try { if (udp != null) udp.close(); } catch (Throwable ignored) { }
                ngu(2000);
            }
        }
    }

    private void chayTcp() {
        while (chay) {
            try {
                server = new ServerSocket();
                server.setReuseAddress(true);
                server.bind(new InetSocketAddress(CONG_TCP));
                while (chay) {
                    Socket s = server.accept();
                    Socket cu = khach;
                    if (cu != null) try { cu.close(); } catch (Throwable ignored) { }   // may moi thay may cu
                    khach = s;
                    new Thread(() -> phucVu(s), "khach").start();
                }
            } catch (Throwable t) {
                try { if (server != null) server.close(); } catch (Throwable ignored) { }
                ngu(2000);
            }
        }
    }

    private static void ngu(long ms) {
        try { Thread.sleep(ms); } catch (InterruptedException ignored) { }
    }

    private void gui(DataOutputStream ra, JSONObject o) throws Exception {
        byte[] b = o.toString().getBytes(StandardCharsets.UTF_8);
        synchronized (ra) {
            ra.writeInt(b.length);
            ra.write(b);
            ra.flush();
        }
    }

    private void phucVu(Socket s) {
        String tenMay = "?";
        try {
            s.setTcpNoDelay(true);
            DataInputStream vao = new DataInputStream(new BufferedInputStream(s.getInputStream()));
            DataOutputStream ra = new DataOutputStream(s.getOutputStream());
            boolean daDongY = false;
            while (chay) {
                int n = vao.readInt();
                if (n <= 0 || n > 8_000_000) throw new Exception("goi tin sai");
                byte[] b = new byte[n];
                vao.readFully(b);
                JSONObject o = new JSONObject(new String(b, StandardCharsets.UTF_8));
                byte[] nhiPhan = null;                          // giong doc: PCM di ngay sau JSON
                int nb = o.optInt("bytes", 0);
                if (nb > 0) {
                    if (nb > 50_000_000) throw new Exception("goi tin qua lon");
                    nhiPhan = new byte[nb];
                    vao.readFully(nhiPhan);
                }
                String loai = o.optString("loai");
                if (loai.equals("chao")) {
                    tenMay = o.optString("ten", "máy tính");
                    String ma = o.optString("ma", tenMay);
                    Set<String> ds = new HashSet<>(pref().getStringSet("may_da_dong_y", new HashSet<>()));
                    if (!ds.contains(ma)) {
                        trangThai = "Đang hỏi đồng ý kết nối với " + tenMay + "…";
                        synchronized (khoaXN) { ketQuaXN = null; }
                        Intent i = new Intent(this, XacNhanActivity.class).putExtra("ten", tenMay)
                                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                        startActivity(i);
                        synchronized (khoaXN) {
                            long het = System.currentTimeMillis() + 90_000;
                            while (ketQuaXN == null && System.currentTimeMillis() < het) khoaXN.wait(1000);
                        }
                        if (!Boolean.TRUE.equals(ketQuaXN)) {
                            JSONObject tl = new JSONObject().put("loai", "chao_lai").put("dong_y", false);
                            gui(ra, tl);
                            trangThai = "Đã từ chối " + tenMay;
                            s.close();
                            return;
                        }
                        ds.add(ma);
                        pref().edit().putStringSet("may_da_dong_y", ds).apply();
                    }
                    daDongY = true;
                    JSONObject tl = new JSONObject().put("loai", "chao_lai").put("dong_y", true)
                            .put("ten_tv", tenTv(this));
                    gui(ra, tl);
                    trangThai = "✅ Đã kết nối với máy tính: " + tenMay;
                    ui.post(this::batLop);
                } else if (!daDongY) {
                    throw new Exception("chua chao");
                } else if (loai.equals("phu_de")) {
                    String chu = o.optString("chu");
                    JSONArray k = o.optJSONArray("khung");
                    float[] kh = null;
                    if (k != null && k.length() == 4) {
                        kh = new float[4];
                        for (int i = 0; i < 4; i++) kh[i] = (float) k.optDouble(i);
                    }
                    final float[] khF = kh;
                    JSONArray d = o.optJSONArray("dong");
                    String[] dc = null;
                    float[][] dh = null;
                    if (d != null && d.length() > 0) {
                        dc = new String[d.length()];
                        dh = new float[d.length()][4];
                        for (int i = 0; i < d.length(); i++) {
                            JSONObject m = d.getJSONObject(i);
                            dc[i] = m.optString("chu");
                            JSONArray hp = m.getJSONArray("hop");
                            for (int j = 0; j < 4; j++) dh[i][j] = (float) hp.optDouble(j);
                        }
                    }
                    final String[] dcF = dc;
                    final float[][] dhF = dh;
                    final float tl = (float) o.optDouble("co", 1.0);
                    ui.post(() -> { batLop(); if (lop != null) lop.datPhuDe(chu, khF, dcF, dhF, tl); });
                } else if (loai.equals("giong")) {
                    if (nhiPhan != null) {
                        if (loa == null) loa = new Loa();
                        loa.phat(nhiPhan);
                    }
                } else if (loai.equals("dung_giong")) {
                    if (loa != null) loa.dung();
                } else if (loai.equals("bong_bong")) {
                    JSONArray m = o.optJSONArray("muc");
                    int n = m == null ? 0 : m.length();
                    String[] bc = new String[n];
                    float[][] bh = new float[n][4];
                    for (int i = 0; i < n; i++) {
                        JSONObject x = m.getJSONObject(i);
                        bc[i] = x.optString("chu");
                        JSONArray hp = x.getJSONArray("hop");
                        for (int j = 0; j < 4; j++) bh[i][j] = (float) hp.optDouble(j);
                    }
                    ui.post(() -> { batLop(); if (lop != null) lop.datBongBong(bc, bh); });
                } else if (loai.equals("xoa_bong_bong")) {
                    ui.post(() -> { if (lop != null) lop.datBongBong(null, null); });
                } else if (loai.equals("xoa")) {
                    ui.post(() -> { if (lop != null) lop.xoa(); });
                } else if (loai.equals("ping")) {
                    gui(ra, new JSONObject().put("loai", "pong"));
                }
            }
        } catch (Throwable t) {
            // mat ket noi
        } finally {
            try { s.close(); } catch (Throwable ignored) { }
            if (khach == s) {
                khach = null;
                trangThai = "Mất kết nối với " + tenMay + " — đang chờ máy tính…";
                ui.post(() -> { if (lop != null) lop.xoa(); });
            }
        }
    }

    void quenMayTinh() {
        pref().edit().remove("may_da_dong_y").apply();
    }

    @Override
    public void onDestroy() {
        chay = false;
        dangChay = null;
        try { if (server != null) server.close(); } catch (Throwable ignored) { }
        try { if (udp != null) udp.close(); } catch (Throwable ignored) { }
        try { if (khoaMc != null) khoaMc.release(); } catch (Throwable ignored) { }
        if (lop != null) {
            try { ((WindowManager) getSystemService(WINDOW_SERVICE)).removeView(lop); } catch (Throwable ignored) { }
        }
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent i) {
        return null;
    }
}
