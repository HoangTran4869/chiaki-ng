package vn.dichgame.tv;

import android.app.Activity;
import android.graphics.Color;
import android.os.Bundle;
import android.view.Gravity;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

/** Lan dau 1 may tinh ket noi: hoi "Dong y?" (bam OK tren remote). */
public class XacNhanActivity extends Activity {
    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        String ten = getIntent().getStringExtra("ten");
        LinearLayout l = new LinearLayout(this);
        l.setOrientation(LinearLayout.VERTICAL);
        l.setPadding(60, 50, 60, 50);
        l.setBackgroundColor(Color.rgb(20, 24, 40));
        TextView t = new TextView(this);
        t.setTextColor(Color.WHITE);
        t.setTextSize(26);
        t.setGravity(Gravity.CENTER);
        t.setText("Máy tính \"" + ten + "\" muốn kết nối với Dịch Game TV.\nĐồng ý?");
        l.addView(t);
        Button dong = new Button(this);
        dong.setText("✅ Đồng ý");
        dong.setTextSize(22);
        dong.setOnClickListener(v -> { DichService.traLoiXacNhan(true); finish(); });
        l.addView(dong);
        Button khong = new Button(this);
        khong.setText("Từ chối");
        khong.setTextSize(20);
        khong.setOnClickListener(v -> { DichService.traLoiXacNhan(false); finish(); });
        l.addView(khong);
        setContentView(l);
        dong.requestFocus();
    }

    @Override
    public void onBackPressed() {
        DichService.traLoiXacNhan(false);
        super.onBackPressed();
    }
}
