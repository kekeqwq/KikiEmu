package com.kiki.gpubench;

import android.app.Activity;
import android.opengl.GLES30;
import android.opengl.GLSurfaceView;
import android.opengl.Matrix;
import android.os.Debug;
import android.os.Bundle;
import android.util.Log;
import android.graphics.PixelFormat;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.FrameLayout;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.FloatBuffer;
import java.nio.ShortBuffer;
import java.util.Locale;
import java.util.concurrent.atomic.AtomicInteger;

public final class GpuBenchActivity extends Activity {
    private static final String TAG = "KikiGpuBench";
    static {
        System.loadLibrary("kikigpubench_buffer_probe");
    }

    private static native int nativeSetBufferCount(Surface surface, int count);

    private static final class BenchSurfaceView extends GLSurfaceView {
        private final int requestedBufferCount;

        BenchSurfaceView(Activity activity, int bufferCount) {
            super(activity);
            requestedBufferCount = bufferCount;
        }

        @Override
        public void surfaceCreated(SurfaceHolder holder) {
            if (requestedBufferCount >= 3 && requestedBufferCount <= 5) {
                int result = nativeSetBufferCount(holder.getSurface(), requestedBufferCount);
                Log.i(TAG, "BUFFER_COUNT_REQUEST=" + requestedBufferCount + " RESULT=" + result);
            }
            super.surfaceCreated(holder);
        }
    }

    private BenchRenderer renderer;

    @Override
    public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON
                | WindowManager.LayoutParams.FLAG_FULLSCREEN);

        FrameLayout root = new FrameLayout(this);
        int bufferCount = getIntent().getIntExtra("buffer_count", 0);
        GLSurfaceView surface = new BenchSurfaceView(this, bufferCount);
        surface.setZOrderOnTop(true);
        surface.getHolder().setFormat(PixelFormat.OPAQUE);
        surface.setEGLContextClientVersion(3);
        surface.setEGLConfigChooser(8, 8, 8, 0, 24, 0);
        renderer = new BenchRenderer(this);
        int requestedProfile = getIntent().getIntExtra("profile", BenchRenderer.PROFILE_STRESS);
        if (requestedProfile < BenchRenderer.PROFILE_TARGET
                || requestedProfile > BenchRenderer.PROFILE_STRESS) {
            requestedProfile = BenchRenderer.PROFILE_STRESS;
        }
        renderer.selectProfile(requestedProfile);
        surface.setRenderer(renderer);
        surface.setRenderMode(GLSurfaceView.RENDERMODE_CONTINUOUSLY);
        root.addView(surface, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
        setContentView(root);
        getWindow().setDecorFitsSystemWindows(false);
        applyImmersiveMode();
        Log.i(TAG, "SINGLE_OPAQUE_LAYER=1");
    }

    @Override
    protected void onResume() {
        super.onResume();
        applyImmersiveMode();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            applyImmersiveMode();
        }
    }

    private void applyImmersiveMode() {
        View decor = getWindow().getDecorView();
        getWindow().setFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN,
                WindowManager.LayoutParams.FLAG_FULLSCREEN);
        decor.setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                        | View.SYSTEM_UI_FLAG_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        WindowInsetsController insets = decor.getWindowInsetsController();
        if (insets != null) {
            insets.hide(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
            insets.setSystemBarsBehavior(
                    WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
        }
    }

    private void showStats(String fps, String detail, float rate) {
        // The digits are drawn into the same GL buffer. A second Android view would
        // force client composition and hide the direct-scanout path.
        if (fps == null || detail == null || rate < -1f) {
            return;
        }
    }

    private static final class BenchRenderer implements GLSurfaceView.Renderer {
        static final int PROFILE_TARGET = 0;
        static final int PROFILE_GAME = 1;
        static final int PROFILE_STRESS = 2;
        private static final String[] PROFILE_NAMES = {"60 FPS target", "Game load", "Extreme 3D"};
        private static final int[] INSTANCE_COUNTS = {1024, 8192, 32768};
        private static final int[] FRAGMENT_STEPS = {4, 16, 32};
        private static final int GL_TIME_ELAPSED_EXT = 0x88BF;
        private static final int GL_GPU_DISJOINT_EXT = 0x8FBB;
        private static final int GL_QUERY_RESULT_AVAILABLE = 0x8867;
        private static final int GL_QUERY_RESULT = 0x8866;
        private static final int GPU_QUERY_RING_SIZE = 4;

        private static final String VERTEX_SHADER =
                "#version 300 es\n" +
                "layout(location=0) in vec3 aPosition;\n" +
                "layout(location=1) in vec3 aNormal;\n" +
                "layout(location=2) in vec4 iTransform;\n" +
                "layout(location=3) in vec4 iTintPhase;\n" +
                "uniform mat4 uViewProj; uniform float uTime;\n" +
                "out vec3 vNormal; out vec3 vTint; out vec3 vWorld;\n" +
                "void main(){\n" +
                " float a=uTime*(0.35+iTintPhase.w*0.25)+iTintPhase.w*6.28318;\n" +
                " float c=cos(a), s=sin(a);\n" +
                " mat3 r=mat3(c,0.0,-s, 0.0,1.0,0.0, s,0.0,c);\n" +
                " vec3 local=r*(aPosition*iTransform.w);\n" +
                " vec3 world=local+iTransform.xyz;\n" +
                " vWorld=world; vNormal=normalize(r*aNormal); vTint=iTintPhase.rgb;\n" +
                " gl_Position=uViewProj*vec4(world,1.0);\n" +
                "}\n";

        private static final String FRAGMENT_SHADER =
                "#version 300 es\n" +
                "precision highp float;\n" +
                "in vec3 vNormal; in vec3 vTint; in vec3 vWorld;\n" +
                "uniform float uTime; uniform int uQuality;\n" +
                "out vec4 fragColor;\n" +
                "void main(){\n" +
                " vec3 n=normalize(vNormal);\n" +
                " float light=0.22+0.78*max(dot(n,normalize(vec3(-0.4,0.7,0.6))),0.0);\n" +
                " vec2 q=gl_FragCoord.xy*0.0017+vWorld.xy*0.09;\n" +
                " float wave=0.0;\n" +
                " for(int i=0;i<24;i++){ if(i>=uQuality) break;\n" +
                "   q=vec2(sin(q.x*1.31+q.y*0.71+uTime*0.08),cos(q.y*1.17-q.x*0.61));\n" +
                "   wave+=sin(q.x+q.y+uTime*0.7)*0.018;\n" +
                " }\n" +
                " fragColor=vec4(vTint*light+wave,1.0);\n" +
                "}\n";

        private final GpuBenchActivity activity;
        private final AtomicInteger requestedProfile = new AtomicInteger(PROFILE_STRESS);
        private final float[] projection = new float[16];
        private final float[] view = new float[16];
        private final float[] viewProjection = new float[16];
        private int program;
        private int cubeBuffer;
        private int indexBuffer;
        private int instanceBuffer;
        private int indexCount;
        private int width = 1;
        private int height = 1;
        private int activeProfile = -1;
        private int viewProjectionLocation;
        private int timeLocation;
        private int qualityLocation;
        private final int[] gpuQueries = new int[GPU_QUERY_RING_SIZE];
        private final boolean[] gpuQueryPending = new boolean[GPU_QUERY_RING_SIZE];
        private final boolean[] gpuQueryValid = new boolean[GPU_QUERY_RING_SIZE];
        private final int[] gpuQueryValue = new int[1];
        private final int[] gpuDisjointValue = new int[1];
        private boolean gpuTimerSupported;
        private String gpuTimerStatus = "unsupported";
        private int nextGpuQuery;
        private double gpuMsTotal;
        private int gpuSamples;
        private long startNs;
        private long lastFrameNs;
        private long statsStartNs;
        private int frames;
        private double frameMsTotal;
        private double onDrawCpuMsTotal;
        private double drawSubmitCpuMsTotal;
        private double gpuPollCpuMsTotal;
        private double glSetupCpuMsTotal;
        private double clearCpuMsTotal;
        private double stateCpuMsTotal;
        private double bufferBindCpuMsTotal;
        private double onDrawThreadCpuMsTotal;
        private int cpuSamples;
        private double minFps = Double.MAX_VALUE;
        private String rendererName = "unknown";
        private double displayedFps;
        private int overlayProgram;
        private int overlayBuffer;
        private int overlayScaleLocation;
        private int overlayOffsetLocation;
        private int overlayColorLocation;
        private static final int[] DIGIT_MASKS = {
                0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
        };
        private static final String OVERLAY_VERTEX =
                "#version 300 es\n" +
                "layout(location=0) in vec2 aPos;\n" +
                "uniform vec2 uScale; uniform vec2 uOffset;\n" +
                "void main(){ gl_Position = vec4(aPos * uScale + uOffset, 0.0, 1.0); }\n";
        private static final String OVERLAY_FRAGMENT =
                "#version 300 es\nprecision mediump float;\n" +
                "uniform vec4 uColor; out vec4 fragColor;\n" +
                "void main(){ fragColor = uColor; }\n";

        BenchRenderer(GpuBenchActivity activity) {
            this.activity = activity;
        }

        void selectProfile(int profile) {
            requestedProfile.set(profile);
        }

        @Override
        public void onSurfaceCreated(javax.microedition.khronos.opengles.GL10 ignored,
                                      javax.microedition.khronos.egl.EGLConfig config) {
            GLES30.glClearColor(0.025f, 0.04f, 0.075f, 1.0f);
            GLES30.glEnable(GLES30.GL_DEPTH_TEST);
            GLES30.glEnable(GLES30.GL_CULL_FACE);
            GLES30.glCullFace(GLES30.GL_BACK);
            program = linkProgram(VERTEX_SHADER, FRAGMENT_SHADER);
            overlayProgram = linkProgram(OVERLAY_VERTEX, OVERLAY_FRAGMENT);
            overlayScaleLocation = GLES30.glGetUniformLocation(overlayProgram, "uScale");
            overlayOffsetLocation = GLES30.glGetUniformLocation(overlayProgram, "uOffset");
            overlayColorLocation = GLES30.glGetUniformLocation(overlayProgram, "uColor");
            int[] overlayBuffers = new int[1];
            GLES30.glGenBuffers(1, overlayBuffers, 0);
            overlayBuffer = overlayBuffers[0];
            viewProjectionLocation = GLES30.glGetUniformLocation(program, "uViewProj");
            timeLocation = GLES30.glGetUniformLocation(program, "uTime");
            qualityLocation = GLES30.glGetUniformLocation(program, "uQuality");
            rendererName = GLES30.glGetString(GLES30.GL_RENDERER);
            initializeGpuTimerQueries();
            buildGeometry();
            startNs = System.nanoTime();
            statsStartNs = startNs;
            Log.i(TAG, "GL_RENDERER=" + rendererName + " GL_VERSION=" + GLES30.glGetString(GLES30.GL_VERSION)
                    + " GPU_TIMER=" + gpuTimerStatus);
            activity.showStats("-- FPS", "60 FPS target  |  " + rendererName, 0f);
        }

        @Override
        public void onSurfaceChanged(javax.microedition.khronos.opengles.GL10 ignored,
                                     int w, int h) {
            width = Math.max(1, w);
            height = Math.max(1, h);
            GLES30.glViewport(0, 0, width, height);
            Matrix.perspectiveM(projection, 0, 56f, width / (float) height, 0.1f, 260f);
            Matrix.setLookAtM(view, 0, 0f, 0f, 92f, 0f, 0f, 0f, 0f, 1f, 0f);
            Matrix.multiplyMM(viewProjection, 0, projection, 0, view, 0);
            Log.i(TAG, "SURFACE=" + width + "x" + height);
        }

        @Override
        public void onDrawFrame(javax.microedition.khronos.opengles.GL10 ignored) {
            long now = System.nanoTime();
            long callbackStartNs = now;
            long callbackThreadCpuStartNs = Debug.threadCpuTimeNanos();
            if (lastFrameNs != 0) {
                frameMsTotal += (now - lastFrameNs) / 1_000_000.0;
                frames++;
            }
            lastFrameNs = now;

            int requested = requestedProfile.get();
            if (requested != activeProfile) {
                activeProfile = requested;
                statsStartNs = now;
                frameMsTotal = 0;
                frames = 0;
                minFps = Double.MAX_VALUE;
                onDrawCpuMsTotal = 0;
                drawSubmitCpuMsTotal = 0;
                gpuPollCpuMsTotal = 0;
                glSetupCpuMsTotal = 0;
                clearCpuMsTotal = 0;
                stateCpuMsTotal = 0;
                bufferBindCpuMsTotal = 0;
                onDrawThreadCpuMsTotal = 0;
                cpuSamples = 0;
                gpuMsTotal = 0;
                gpuSamples = 0;
                Log.i(TAG, "PROFILE=" + PROFILE_NAMES[activeProfile] + " INSTANCES=" + INSTANCE_COUNTS[activeProfile]);
            }

            long gpuPollStartNs = System.nanoTime();
            pollGpuTimerQueries();
            gpuPollCpuMsTotal += (System.nanoTime() - gpuPollStartNs) / 1_000_000.0;

            float time = (now - startNs) / 1_000_000_000.0f;
            long glSetupStartNs = System.nanoTime();
            long clearStartNs = System.nanoTime();
            GLES30.glClear(GLES30.GL_COLOR_BUFFER_BIT | GLES30.GL_DEPTH_BUFFER_BIT);
            clearCpuMsTotal += (System.nanoTime() - clearStartNs) / 1_000_000.0;
            long stateStartNs = System.nanoTime();
            GLES30.glUseProgram(program);
            GLES30.glUniformMatrix4fv(viewProjectionLocation, 1, false, viewProjection, 0);
            GLES30.glUniform1f(timeLocation, time);
            GLES30.glUniform1i(qualityLocation, FRAGMENT_STEPS[activeProfile]);
            stateCpuMsTotal += (System.nanoTime() - stateStartNs) / 1_000_000.0;
            long bufferBindStartNs = System.nanoTime();
            GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, cubeBuffer);
            GLES30.glBindBuffer(GLES30.GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
            GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, instanceBuffer);
            bufferBindCpuMsTotal += (System.nanoTime() - bufferBindStartNs) / 1_000_000.0;
            glSetupCpuMsTotal += (System.nanoTime() - glSetupStartNs) / 1_000_000.0;
            int gpuQuerySlot = beginGpuTimerQuery();
            long drawSubmitStartNs = System.nanoTime();
            GLES30.glDrawElementsInstanced(GLES30.GL_TRIANGLES, indexCount,
                    GLES30.GL_UNSIGNED_SHORT, 0, INSTANCE_COUNTS[activeProfile]);
            drawSubmitCpuMsTotal += (System.nanoTime() - drawSubmitStartNs) / 1_000_000.0;
            endGpuTimerQuery(gpuQuerySlot);
            onDrawCpuMsTotal += (System.nanoTime() - callbackStartNs) / 1_000_000.0;
            long callbackThreadCpuEndNs = Debug.threadCpuTimeNanos();
            if (callbackThreadCpuStartNs >= 0 && callbackThreadCpuEndNs >= callbackThreadCpuStartNs) {
                onDrawThreadCpuMsTotal += (callbackThreadCpuEndNs - callbackThreadCpuStartNs) / 1_000_000.0;
            }
            cpuSamples++;

            long elapsed = now - statsStartNs;
            if (elapsed >= 1_000_000_000L && frames > 0) {
                double fps = frames * 1_000_000_000.0 / elapsed;
                displayedFps = fps;
                double avgMs = frameMsTotal / frames;
                double onDrawCpuAvgMs = cpuSamples == 0 ? Double.NaN : onDrawCpuMsTotal / cpuSamples;
                double drawSubmitCpuAvgMs = cpuSamples == 0 ? Double.NaN : drawSubmitCpuMsTotal / cpuSamples;
                double gpuPollCpuAvgMs = cpuSamples == 0 ? Double.NaN : gpuPollCpuMsTotal / cpuSamples;
                double glSetupCpuAvgMs = cpuSamples == 0 ? Double.NaN : glSetupCpuMsTotal / cpuSamples;
                double clearCpuAvgMs = cpuSamples == 0 ? Double.NaN : clearCpuMsTotal / cpuSamples;
                double stateCpuAvgMs = cpuSamples == 0 ? Double.NaN : stateCpuMsTotal / cpuSamples;
                double bufferBindCpuAvgMs = cpuSamples == 0 ? Double.NaN : bufferBindCpuMsTotal / cpuSamples;
                double onDrawThreadCpuAvgMs = cpuSamples == 0 ? Double.NaN : onDrawThreadCpuMsTotal / cpuSamples;
                double gpuAvgMs = gpuSamples == 0 ? Double.NaN : gpuMsTotal / gpuSamples;
                minFps = Math.min(minFps, fps);
                String detail = String.format(Locale.US,
                        "TARGET 60  |  %s  |  %dx%d\nframe %.1f ms  |  low %.1f FPS\nGPU %.2f ms | CPU draw %.2f ms",
                        PROFILE_NAMES[activeProfile], width, height, avgMs, minFps,
                        gpuSamples == 0 ? Double.NaN : gpuAvgMs, drawSubmitCpuAvgMs);
                activity.showStats(String.format(Locale.US, "%.1f FPS", fps), detail, (float) fps);
                Log.i(TAG, String.format(Locale.US,
                        "FPS=%.2f AVG_FRAME_MS=%.2f MIN_WINDOW_FPS=%.2f ON_DRAW_WALL_MS=%.3f ON_DRAW_THREAD_CPU_MS=%.3f QUERY_POLL_MS=%.3f GL_SETUP_MS=%.3f CLEAR_MS=%.3f STATE_MS=%.3f BUFFER_BIND_MS=%.3f DRAW_SUBMIT_MS=%.3f GPU_DRAW_MS=%s GPU_SAMPLES=%d GPU_TIMER=%s PROFILE=%s INSTANCES=%d RES=%dx%d",
                        fps, avgMs, minFps,
                        onDrawCpuAvgMs, onDrawThreadCpuAvgMs, gpuPollCpuAvgMs, glSetupCpuAvgMs,
                        clearCpuAvgMs, stateCpuAvgMs, bufferBindCpuAvgMs, drawSubmitCpuAvgMs,
                        gpuSamples == 0 ? "n/a" : String.format(Locale.US, "%.3f", gpuAvgMs),
                        gpuSamples, gpuTimerStatus, PROFILE_NAMES[activeProfile],
                        INSTANCE_COUNTS[activeProfile], width, height));
                statsStartNs = now;
                frameMsTotal = 0;
                frames = 0;
                onDrawCpuMsTotal = 0;
                drawSubmitCpuMsTotal = 0;
                gpuPollCpuMsTotal = 0;
                glSetupCpuMsTotal = 0;
                clearCpuMsTotal = 0;
                stateCpuMsTotal = 0;
                bufferBindCpuMsTotal = 0;
                onDrawThreadCpuMsTotal = 0;
                cpuSamples = 0;
                gpuMsTotal = 0;
                gpuSamples = 0;
            }
            drawFpsOverlay();
        }

        private void drawFpsOverlay() {
            if (overlayProgram == 0 || width <= 0 || height <= 0) {
                return;
            }
            int fps = (int) Math.max(0, Math.min(999, Math.round(displayedFps)));
            int hundreds = fps / 100;
            int tens = (fps / 10) % 10;
            int ones = fps % 10;
            java.util.ArrayList<Float> vertices = new java.util.ArrayList<>();
            float digitWidth = 0.16f;
            float gap = 0.03f;
            float startX = -0.92f;
            if (hundreds > 0) {
                appendDigit(vertices, hundreds, startX);
                appendDigit(vertices, tens, startX + digitWidth + gap);
                appendDigit(vertices, ones, startX + (digitWidth + gap) * 2f);
            } else {
                appendDigit(vertices, tens, startX);
                appendDigit(vertices, ones, startX + digitWidth + gap);
            }
            if (vertices.isEmpty()) {
                return;
            }
            float[] data = new float[vertices.size()];
            for (int i = 0; i < data.length; i++) {
                data[i] = vertices.get(i);
            }
            FloatBuffer buffer = ByteBuffer.allocateDirect(data.length * 4)
                    .order(ByteOrder.nativeOrder()).asFloatBuffer();
            buffer.put(data).position(0);
            GLES30.glDisable(GLES30.GL_DEPTH_TEST);
            GLES30.glUseProgram(overlayProgram);
            GLES30.glUniform2f(overlayScaleLocation, 1f, 1f);
            GLES30.glUniform2f(overlayOffsetLocation, 0f, 0f);
            float green = fps >= 59 ? 1f : fps >= 30 ? 0.82f : 0.25f;
            float red = fps >= 59 ? 0.25f : fps >= 30 ? 1f : 1f;
            GLES30.glUniform4f(overlayColorLocation, red, green, 0.35f, 1f);
            GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, overlayBuffer);
            GLES30.glBufferData(GLES30.GL_ARRAY_BUFFER, data.length * 4, buffer, GLES30.GL_STREAM_DRAW);
            GLES30.glEnableVertexAttribArray(0);
            GLES30.glVertexAttribPointer(0, 2, GLES30.GL_FLOAT, false, 8, 0);
            GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, data.length / 2);
            GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, cubeBuffer);
            GLES30.glVertexAttribPointer(0, 3, GLES30.GL_FLOAT, false, 24, 0);
            GLES30.glEnableVertexAttribArray(0);
            GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, instanceBuffer);
            GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, 0);
            GLES30.glEnable(GLES30.GL_DEPTH_TEST);
        }

        private void appendDigit(java.util.ArrayList<Float> vertices, int digit, float originX) {
            int mask = DIGIT_MASKS[digit];
            float[][] segments = {
                    {0.18f, 1.42f, 0.82f, 1.56f},
                    {0.78f, 0.82f, 0.94f, 1.40f},
                    {0.78f, 0.16f, 0.94f, 0.74f},
                    {0.18f, 0.02f, 0.82f, 0.16f},
                    {0.04f, 0.16f, 0.20f, 0.74f},
                    {0.04f, 0.82f, 0.20f, 1.40f},
                    {0.18f, 0.72f, 0.82f, 0.86f}
            };
            float scaleX = 0.16f;
            float scaleY = 0.18f;
            float originY = 0.62f;
            for (int segment = 0; segment < segments.length; segment++) {
                if ((mask & (1 << segment)) == 0) {
                    continue;
                }
                float left = originX + segments[segment][0] * scaleX;
                float bottom = originY + segments[segment][1] * scaleY;
                float right = originX + segments[segment][2] * scaleX;
                float top = originY + segments[segment][3] * scaleY;
                addQuad(vertices, left, bottom, right, top);
            }
        }

        private void addQuad(java.util.ArrayList<Float> vertices, float left, float bottom,
                             float right, float top) {
            float[] quad = {
                    left, bottom, right, bottom, right, top,
                    left, bottom, right, top, left, top
            };
            for (float value : quad) {
                vertices.add(value);
            }
        }

        private void initializeGpuTimerQueries() {
            int[] extensionCount = new int[1];
            GLES30.glGetIntegerv(GLES30.GL_NUM_EXTENSIONS, extensionCount, 0);
            gpuTimerSupported = false;
            for (int i = 0; i < extensionCount[0]; i++) {
                if ("GL_EXT_disjoint_timer_query".equals(GLES30.glGetStringi(GLES30.GL_EXTENSIONS, i))) {
                    gpuTimerSupported = true;
                    break;
                }
            }
            if (!gpuTimerSupported) {
                gpuTimerStatus = "EXT_disjoint_timer_query unavailable";
                return;
            }
            GLES30.glGenQueries(GPU_QUERY_RING_SIZE, gpuQueries, 0);
            int error = GLES30.glGetError();
            if (error != GLES30.GL_NO_ERROR) {
                gpuTimerSupported = false;
                gpuTimerStatus = String.format(Locale.US, "query init error 0x%04X", error);
                return;
            }
            gpuTimerStatus = "EXT_disjoint_timer_query active";
        }

        private int beginGpuTimerQuery() {
            if (!gpuTimerSupported) return -1;
            for (int attempt = 0; attempt < GPU_QUERY_RING_SIZE; attempt++) {
                int slot = (nextGpuQuery + attempt) % GPU_QUERY_RING_SIZE;
                if (gpuQueryPending[slot]) continue;
                nextGpuQuery = (slot + 1) % GPU_QUERY_RING_SIZE;
                gpuQueryValid[slot] = true;
                GLES30.glBeginQuery(GL_TIME_ELAPSED_EXT, gpuQueries[slot]);
                return slot;
            }
            return -1;
        }

        private void endGpuTimerQuery(int slot) {
            if (slot >= 0) {
                GLES30.glEndQuery(GL_TIME_ELAPSED_EXT);
                gpuQueryPending[slot] = true;
            }
        }

        private void pollGpuTimerQueries() {
            if (!gpuTimerSupported) return;
            GLES30.glGetIntegerv(GL_GPU_DISJOINT_EXT, gpuDisjointValue, 0);
            if (gpuDisjointValue[0] != 0) {
                for (int i = 0; i < GPU_QUERY_RING_SIZE; i++) {
                    if (gpuQueryPending[i]) gpuQueryValid[i] = false;
                }
                gpuTimerStatus = "GPU disjoint; dropping affected samples";
            } else {
                gpuTimerStatus = "EXT_disjoint_timer_query active";
            }

            for (int i = 0; i < GPU_QUERY_RING_SIZE; i++) {
                if (!gpuQueryPending[i]) continue;
                GLES30.glGetQueryObjectuiv(gpuQueries[i], GL_QUERY_RESULT_AVAILABLE, gpuQueryValue, 0);
                if (gpuQueryValue[0] == 0) continue;
                if (gpuQueryValid[i] && gpuDisjointValue[0] == 0) {
                    GLES30.glGetQueryObjectuiv(gpuQueries[i], GL_QUERY_RESULT, gpuQueryValue, 0);
                    long elapsedNs = Integer.toUnsignedLong(gpuQueryValue[0]);
                    gpuMsTotal += elapsedNs / 1_000_000.0;
                    gpuSamples++;
                }
                gpuQueryPending[i] = false;
                gpuQueryValid[i] = false;
            }
        }

        private void buildGeometry() {
            float[] cube = {
                    -1,-1, 1, 0,0,1,  1,-1, 1, 0,0,1,  1,1,1, 0,0,1,  -1,1,1, 0,0,1,
                    1,-1,-1, 0,0,-1, -1,-1,-1, 0,0,-1, -1,1,-1, 0,0,-1, 1,1,-1, 0,0,-1,
                    -1,-1,-1, -1,0,0, -1,-1,1, -1,0,0, -1,1,1, -1,0,0, -1,1,-1, -1,0,0,
                    1,-1,1, 1,0,0, 1,-1,-1, 1,0,0, 1,1,-1, 1,0,0, 1,1,1, 1,0,0,
                    -1,1,1, 0,1,0, 1,1,1, 0,1,0, 1,1,-1, 0,1,0, -1,1,-1, 0,1,0,
                    -1,-1,-1, 0,-1,0, 1,-1,-1, 0,-1,0, 1,-1,1, 0,-1,0, -1,-1,1, 0,-1,0
            };
            short[] indices = new short[36];
            for (int face = 0; face < 6; face++) {
                int v = face * 4;
                int i = face * 6;
                indices[i] = (short) v; indices[i + 1] = (short) (v + 1); indices[i + 2] = (short) (v + 2);
                indices[i + 3] = (short) v; indices[i + 4] = (short) (v + 2); indices[i + 5] = (short) (v + 3);
            }
            indexCount = indices.length;
            int[] buffers = new int[3];
            GLES30.glGenBuffers(3, buffers, 0);
            cubeBuffer = buffers[0]; indexBuffer = buffers[1]; instanceBuffer = buffers[2];

            GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, cubeBuffer);
            GLES30.glBufferData(GLES30.GL_ARRAY_BUFFER, cube.length * 4,
                    floatBuffer(cube), GLES30.GL_STATIC_DRAW);
            GLES30.glEnableVertexAttribArray(0);
            GLES30.glVertexAttribPointer(0, 3, GLES30.GL_FLOAT, false, 24, 0);
            GLES30.glEnableVertexAttribArray(1);
            GLES30.glVertexAttribPointer(1, 3, GLES30.GL_FLOAT, false, 24, 12);

            GLES30.glBindBuffer(GLES30.GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
            GLES30.glBufferData(GLES30.GL_ELEMENT_ARRAY_BUFFER, indices.length * 2,
                    shortBuffer(indices), GLES30.GL_STATIC_DRAW);

            float[] instances = new float[32768 * 8];
            int columns = 64, rows = 32, layers = 16;
            for (int i = 0; i < 32768; i++) {
                int x = i % columns;
                int y = (i / columns) % rows;
                int z = i / (columns * rows) % layers;
                int p = i * 8;
                instances[p] = (x - (columns - 1) * 0.5f) * 1.75f;
                instances[p + 1] = (y - (rows - 1) * 0.5f) * 1.75f;
                instances[p + 2] = -(z - (layers - 1) * 0.5f) * 1.9f;
                instances[p + 3] = 0.46f;
                instances[p + 4] = 0.25f + 0.75f * ((x * 37 % 101) / 100f);
                instances[p + 5] = 0.25f + 0.75f * ((y * 61 % 97) / 96f);
                instances[p + 6] = 0.25f + 0.75f * ((z * 43 % 89) / 88f);
                instances[p + 7] = (i % 17) / 17f;
            }
            GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, instanceBuffer);
            GLES30.glBufferData(GLES30.GL_ARRAY_BUFFER, instances.length * 4,
                    floatBuffer(instances), GLES30.GL_STATIC_DRAW);
            GLES30.glEnableVertexAttribArray(2);
            GLES30.glVertexAttribPointer(2, 4, GLES30.GL_FLOAT, false, 32, 0);
            GLES30.glVertexAttribDivisor(2, 1);
            GLES30.glEnableVertexAttribArray(3);
            GLES30.glVertexAttribPointer(3, 4, GLES30.GL_FLOAT, false, 32, 16);
            GLES30.glVertexAttribDivisor(3, 1);
            GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, 0);
        }

        private static int compileShader(int type, String source) {
            int shader = GLES30.glCreateShader(type);
            GLES30.glShaderSource(shader, source);
            GLES30.glCompileShader(shader);
            int[] status = new int[1];
            GLES30.glGetShaderiv(shader, GLES30.GL_COMPILE_STATUS, status, 0);
            if (status[0] == 0) {
                String message = GLES30.glGetShaderInfoLog(shader);
                GLES30.glDeleteShader(shader);
                throw new IllegalStateException("Shader compile failed: " + message);
            }
            return shader;
        }

        private static int linkProgram(String vertex, String fragment) {
            int program = GLES30.glCreateProgram();
            GLES30.glAttachShader(program, compileShader(GLES30.GL_VERTEX_SHADER, vertex));
            GLES30.glAttachShader(program, compileShader(GLES30.GL_FRAGMENT_SHADER, fragment));
            GLES30.glLinkProgram(program);
            int[] status = new int[1];
            GLES30.glGetProgramiv(program, GLES30.GL_LINK_STATUS, status, 0);
            if (status[0] == 0) {
                throw new IllegalStateException("Program link failed: " + GLES30.glGetProgramInfoLog(program));
            }
            return program;
        }

        private static FloatBuffer floatBuffer(float[] data) {
            FloatBuffer buffer = ByteBuffer.allocateDirect(data.length * 4)
                    .order(ByteOrder.nativeOrder()).asFloatBuffer();
            buffer.put(data).position(0);
            return buffer;
        }

        private static ShortBuffer shortBuffer(short[] data) {
            ShortBuffer buffer = ByteBuffer.allocateDirect(data.length * 2)
                    .order(ByteOrder.nativeOrder()).asShortBuffer();
            buffer.put(data).position(0);
            return buffer;
        }
    }
}
