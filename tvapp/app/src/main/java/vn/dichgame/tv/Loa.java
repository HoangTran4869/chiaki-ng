package vn.dichgame.tv;

import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioTrack;

import java.util.concurrent.LinkedBlockingQueue;

/** Giong doc tu may tinh -> loa TV. Mo SAN 1 duong phat co dinh (PCM 16 bit, 1 kenh, 24000 Hz) -> phat ngay. */
public class Loa {
    private final AudioTrack at;
    private final LinkedBlockingQueue<byte[]> hang = new LinkedBlockingQueue<>();
    private volatile boolean dangDung = false;

    public Loa() {
        int rate = 24000;
        int min = AudioTrack.getMinBufferSize(rate, AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_16BIT);
        at = new AudioTrack.Builder()
                .setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA)
                        .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH).build())
                .setAudioFormat(new AudioFormat.Builder().setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                        .setSampleRate(rate).setChannelMask(AudioFormat.CHANNEL_OUT_MONO).build())
                .setBufferSizeInBytes(Math.max(min * 2, 9600))
                .setTransferMode(AudioTrack.MODE_STREAM)
                .setPerformanceMode(AudioTrack.PERFORMANCE_MODE_LOW_LATENCY)
                .build();
        at.play();
        Thread t = new Thread(() -> {
            while (true) {
                try {
                    byte[] b = hang.take();
                    int i = 0;
                    while (i < b.length && !dangDung) {          // ghi tung doan nho -> dung duoc giua chung
                        int n = Math.min(4800, b.length - i);
                        at.write(b, i, n);
                        i += n;
                    }
                    dangDung = false;
                } catch (Throwable ignored) {
                }
            }
        }, "loa");
        t.setDaemon(true);
        t.start();
    }

    public void phat(byte[] pcm) {
        hang.offer(pcm);
    }

    public void dung() {
        hang.clear();
        dangDung = true;
        try {
            at.pause();
            at.flush();
            at.play();
        } catch (Throwable ignored) {
        }
    }
}
