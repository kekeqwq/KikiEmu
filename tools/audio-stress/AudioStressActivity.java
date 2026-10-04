// SPDX-License-Identifier: GPL-2.0-or-later
package com.kiki.audiostress;

import android.app.Activity;
import android.os.Bundle;
import android.os.SystemClock;
import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioTrack;
import android.media.MediaPlayer;
import android.media.SoundPool;
import android.util.Log;
import android.widget.TextView;
import java.io.File;
import java.io.FileOutputStream;
import java.io.PrintWriter;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

/** Standalone developer test app. Never uses microphone/network/user media.
 * Synthetic -14 dBFS sine bursts with 10-ms ramps, varied rates/channels and
 * short/long gaps exercise decoding, track reuse, teardown and ALSA standby.
 * A reported completion is NOT proof of host sound: pair with process loopback.
 */
public final class AudioStressActivity extends Activity {
    private static final String TAG = "KikiAudioStress";
    private volatile boolean cancelled;
    private TextView status;
    private PrintWriter log;
    private final AudioAttributes attrs = new AudioAttributes.Builder()
            .setUsage(AudioAttributes.USAGE_MEDIA).setContentType(AudioAttributes.CONTENT_TYPE_SPEECH).build();
    private void report(String text) {
        String line = SystemClock.elapsedRealtime() + " " + text;
        Log.i(TAG, line);
        synchronized (this) { if (log != null) { log.println(line); log.flush(); } }
        runOnUiThread(() -> status.setText("KikiEmu audio regression\n\n" + line + "\n\nSynthetic clips only; no microphone capture.\nSee events.log and host process-loopback evidence."));
    }
    @Override public void onCreate(Bundle bundle) {
        super.onCreate(bundle);
        status = new TextView(this); status.setTextSize(20); status.setPadding(20, 30, 20, 20); setContentView(status);
        int count = Math.min(2000, Math.max(1, getIntent().getIntExtra("count", 400)));
        String mode = getIntent().getStringExtra("mode");
        String selected = mode == null ? "player" : mode;
        int gap = Math.min(10000, Math.max(0, getIntent().getIntExtra("gap", 100)));
        new Thread(() -> run(count, selected, gap), "KikiAudioStress").start();
    }
    private byte[] pcm(int rate, int channels, int millis, int index) {
        int frames = rate * millis / 1000;
        ByteBuffer b = ByteBuffer.allocate(frames * channels * 2).order(ByteOrder.LITTLE_ENDIAN);
        double freq = 440 + (index % 7) * 110;
        for (int i=0; i<frames; ++i) {
            double gain = Math.min(1.0, Math.min(i, frames-1-i) / (rate * .010));
            short sample = (short)Math.round(6500 * gain * Math.sin(2 * Math.PI * freq * i / rate));
            for (int c=0; c<channels; ++c) b.putShort(sample);
        }
        return b.array();
    }
    private File wav(File dir, int rate, int channels, int millis, int index) throws Exception {
        byte[] data = pcm(rate, channels, millis, index);
        File f = new File(dir, "clip-" + index + ".wav");
        ByteBuffer h = ByteBuffer.allocate(44).order(ByteOrder.LITTLE_ENDIAN);
        h.put("RIFF".getBytes("US-ASCII")).putInt(36 + data.length).put("WAVEfmt ".getBytes("US-ASCII"));
        h.putInt(16).putShort((short)1).putShort((short)channels).putInt(rate).putInt(rate*channels*2);
        h.putShort((short)(channels*2)).putShort((short)16).put("data".getBytes("US-ASCII")).putInt(data.length);
        try (FileOutputStream out = new FileOutputStream(f)) { out.write(h.array()); out.write(data); }
        return f;
    }
    private void player(File file, int index) throws Exception {
        MediaPlayer p = new MediaPlayer();
        CountDownLatch done = new CountDownLatch(1); AtomicInteger error = new AtomicInteger();
        try {
            p.setAudioAttributes(attrs);
            p.setOnCompletionListener(x -> done.countDown());
            p.setOnErrorListener((x, what, extra) -> { error.set(what); report("PLAYER_ERROR i="+index+" what="+what+" extra="+extra); done.countDown(); return true; });
            p.setDataSource(file.getAbsolutePath()); p.prepare();
            report("START i="+index+" duration="+p.getDuration()); p.start();
            if (!done.await(8, TimeUnit.SECONDS)) throw new IllegalStateException("MediaPlayer completion timeout i="+index);
            if (error.get()!=0) throw new IllegalStateException("MediaPlayer error i="+index);
            report("COMPLETE i="+index+" pos="+p.getCurrentPosition());
        } finally { p.release(); }
    }
    private void track(int rate, int channels, int millis, int index) throws Exception {
        byte[] bytes = pcm(rate, channels, millis, index);
        int mask = channels==1 ? AudioFormat.CHANNEL_OUT_MONO : AudioFormat.CHANNEL_OUT_STEREO;
        AudioTrack t = new AudioTrack.Builder().setAudioAttributes(attrs).setAudioFormat(new AudioFormat.Builder()
                .setSampleRate(rate).setChannelMask(mask).setEncoding(AudioFormat.ENCODING_PCM_16BIT).build())
                .setTransferMode(AudioTrack.MODE_STATIC).setBufferSizeInBytes(bytes.length).build();
        try {
            // MODE_STATIC is STATE_NO_STATIC_DATA until the first successful write.
            if(t.write(bytes,0,bytes.length)!=bytes.length || t.getState()!=AudioTrack.STATE_INITIALIZED) throw new IllegalStateException("AudioTrack setup failed");
            report("START i="+index+" rate="+rate+" channels="+channels+" duration="+millis);
            long started=SystemClock.elapsedRealtime(); t.play();
            long until=started+8000; int expected=bytes.length/(channels*2);
            while(t.getPlaybackHeadPosition()<expected && SystemClock.elapsedRealtime()<until) Thread.sleep(10);
            int frames=t.getPlaybackHeadPosition();
            report("COMPLETE i="+index+" frames="+frames+" expected="+expected+" underruns="+t.getUnderrunCount());
            if(frames<expected) throw new IllegalStateException("AudioTrack head stalled");
            if(index==9999 && SystemClock.elapsedRealtime()-started<millis/2) throw new IllegalStateException("Reference head advanced implausibly fast (HAL may be discarding PCM)");
            t.stop();
        } finally { t.release(); }
    }
    private void hold(int count, int gap) throws Exception {
        AudioTrack t = new AudioTrack.Builder().setAudioAttributes(attrs).setAudioFormat(new AudioFormat.Builder()
                .setSampleRate(48000).setChannelMask(AudioFormat.CHANNEL_OUT_STEREO)
                .setEncoding(AudioFormat.ENCODING_PCM_16BIT).build())
                .setTransferMode(AudioTrack.MODE_STREAM).setBufferSizeInBytes(48000*4/2).build();
        try {
            if(t.getState()!=AudioTrack.STATE_INITIALIZED) throw new IllegalStateException("Streaming setup failed");
            t.play(); long frames=0;
            for(int i=0;i<count && !cancelled;i++) {
                byte[] sound=pcm(48000,2,100+(i%5)*100,i);
                report("START i="+i+" duration="+(100+(i%5)*100)+" framesHead="+t.getPlaybackHeadPosition());
                if(t.write(sound,0,sound.length,AudioTrack.WRITE_BLOCKING)!=sound.length) throw new IllegalStateException("Streaming short write");
                frames+=sound.length/4;
                byte[] silence=new byte[48000*4*(i%40==39?4000:gap)/1000];
                if(silence.length>0 && t.write(silence,0,silence.length,AudioTrack.WRITE_BLOCKING)!=silence.length) throw new IllegalStateException("Streaming silence short write");
                frames+=silence.length/4;
                report("COMPLETE i="+i+" submitted="+frames+" underruns="+t.getUnderrunCount());
            }
            long until=SystemClock.elapsedRealtime()+8000;
            while(t.getPlaybackHeadPosition()<frames && SystemClock.elapsedRealtime()<until) Thread.sleep(10);
            if(t.getPlaybackHeadPosition()<frames) throw new IllegalStateException("Streaming head stalled");
            t.stop();
        } finally {t.release();}
    }
    private void pool(File dir, int count, int gap) throws Exception {
        SoundPool pool=new SoundPool.Builder().setMaxStreams(1).setAudioAttributes(attrs).build();
        try {
            int[] ids=new int[30]; int[] rates={8000,16000,22050,24000,44100,48000};
            for(int i=0;i<ids.length;i++) {
                CountDownLatch ready=new CountDownLatch(1); AtomicInteger error=new AtomicInteger();
                pool.setOnLoadCompleteListener((p,id,status) -> {error.set(status);ready.countDown();});
                ids[i]=pool.load(wav(dir,rates[i%6],i%3==0?2:1,100+(i%5)*100,i).getAbsolutePath(),1);
                if(ids[i]==0 || !ready.await(8,TimeUnit.SECONDS) || error.get()!=0) throw new IllegalStateException("SoundPool load failed");
            }
            for(int i=0;i<count && !cancelled;i++) {
                int j=i%ids.length, duration=100+(j%5)*100;
                int stream=pool.play(ids[j],1,1,1,0,1);
                report("START i="+i+" source="+j+" duration="+duration+" stream="+stream);
                if(stream==0) throw new IllegalStateException("SoundPool play refused");
                Thread.sleep(duration+(i%40==39?4000:gap));
                report("COMPLETE i="+i+" (scheduled, not output acknowledgement)");
            }
        } finally {pool.release();}
    }
    private void run(int count, String mode, int gap) {
        try {
            File dir=getExternalFilesDir(null);
            log = new PrintWriter(new File(dir,"events.log"));
            report("BEGIN mode="+mode+" count="+count+" gap="+gap);
            int[] rates={8000,16000,22050,24000,44100,48000};
            if(mode.equals("hold")) hold(count,gap);
            else if(mode.equals("pool")) pool(dir,count,gap);
            else for(int i=0; i<count && !cancelled; ++i) {
                int rate=rates[i%rates.length], channels=(i%3==0)?2:1, millis=100+(i%5)*100;
                if(mode.equals("track")) track(rate,channels,millis,i);
                else player(wav(dir,rate,channels,millis,i),i);
                Thread.sleep(i%40==39?4000:gap); // Explicit idle/standby boundaries.
            }
            if(!cancelled) { // Last low-level probe distinguishes decoder from whole output failure.
                report("FINAL_REFERENCE_START"); track(48000,2,2000,9999); report("DONE count="+count);
            } else report("CANCELLED");
        } catch(Throwable e) { report("FAIL "+Log.getStackTraceString(e)); }
        finally { synchronized(this) { if(log!=null) {log.close();log=null;} } }
    }
    @Override public void onDestroy() { cancelled=true; super.onDestroy(); }
}
