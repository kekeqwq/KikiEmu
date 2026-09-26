package com.kiki.gpubench;

import android.app.Activity;
import android.graphics.Color;
import android.opengl.GLES30;
import android.opengl.GLSurfaceView;
import android.opengl.Matrix;
import android.os.Bundle;
import android.util.Log;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.FloatBuffer;
import java.nio.ShortBuffer;
import java.util.Locale;
import java.util.concurrent.atomic.AtomicInteger;

public final class GpuBenchActivity extends Activity {
    private static final String TAG = "KikiGpuBench";
    private BenchRenderer renderer;
    private TextView fpsDisplay;
    private TextView details;

    @Override
    public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        FrameLayout root = new FrameLayout(this);
        GLSurfaceView surface = new GLSurfaceView(this);
        surface.setEGLContextClientVersion(3);
        surface.setEGLConfigChooser(8, 8, 8, 0, 24, 8);
        renderer = new BenchRenderer(this);
        surface.setRenderer(renderer);
        surface.setRenderMode(GLSurfaceView.RENDERMODE_CONTINUOUSLY);
        root.addView(surface, new FrameLayout.LayoutParams(-1, -1));

        LinearLayout panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setPadding(14, 8, 14, 8);
        panel.setBackgroundColor(0xCC101820);
        FrameLayout.LayoutParams panelParams = new FrameLayout.LayoutParams(-1, -2, Gravity.TOP);
        root.addView(panel, panelParams);

        fpsDisplay = new TextView(this);
        fpsDisplay.setTextColor(0xFF70FF9A);
        fpsDisplay.setTextSize(54);
        fpsDisplay.setTypeface(android.graphics.Typeface.MONOSPACE, android.graphics.Typeface.BOLD);
        fpsDisplay.setText("-- FPS");
        panel.addView(fpsDisplay, new LinearLayout.LayoutParams(-1, -2));

        details = new TextView(this);
        details.setTextColor(Color.WHITE);
        details.setTextSize(14);
        details.setTypeface(android.graphics.Typeface.MONOSPACE);
        details.setText("60 FPS target  |  Starting GLES 3D stress scene...");
        panel.addView(details, new LinearLayout.LayoutParams(-1, -2));

        LinearLayout controls = new LinearLayout(this);
        controls.setOrientation(LinearLayout.HORIZONTAL);
        panel.addView(controls, new LinearLayout.LayoutParams(-1, -2));
        addProfileButton(controls, "60 target", BenchRenderer.PROFILE_TARGET);
        addProfileButton(controls, "Game load", BenchRenderer.PROFILE_GAME);
        addProfileButton(controls, "Extreme", BenchRenderer.PROFILE_STRESS);

        setContentView(root);
    }

    private void addProfileButton(LinearLayout row, String label, int profile) {
        Button button = new Button(this);
        button.setText(label);
        button.setTextSize(12);
        button.setPadding(2, 0, 2, 0);
        row.addView(button, new LinearLayout.LayoutParams(0, 42, 1));
        button.setOnClickListener(v -> renderer.selectProfile(profile));
    }

    private void showStats(String fps, String detail, float rate) {
        runOnUiThread(() -> {
            fpsDisplay.setText(fps);
            fpsDisplay.setTextColor(rate >= 59.0f ? 0xFF70FF9A : rate >= 30.0f ? 0xFFFFD45C : 0xFFFF6B6B);
            details.setText(detail);
        });
    }

    private static final class BenchRenderer implements GLSurfaceView.Renderer {
        static final int PROFILE_TARGET = 0;
        static final int PROFILE_GAME = 1;
        static final int PROFILE_STRESS = 2;
        private static final String[] PROFILE_NAMES = {"60 FPS target", "Game load", "Extreme 3D"};
        private static final int[] INSTANCE_COUNTS = {1024, 8192, 32768};
        private static final int[] FRAGMENT_STEPS = {4, 16, 32};

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
        private long startNs;
        private long lastFrameNs;
        private long statsStartNs;
        private int frames;
        private double frameMsTotal;
        private double minFps = Double.MAX_VALUE;
        private String rendererName = "unknown";

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
            viewProjectionLocation = GLES30.glGetUniformLocation(program, "uViewProj");
            timeLocation = GLES30.glGetUniformLocation(program, "uTime");
            qualityLocation = GLES30.glGetUniformLocation(program, "uQuality");
            rendererName = GLES30.glGetString(GLES30.GL_RENDERER);
            buildGeometry();
            startNs = System.nanoTime();
            statsStartNs = startNs;
            Log.i(TAG, "GL_RENDERER=" + rendererName + " GL_VERSION=" + GLES30.glGetString(GLES30.GL_VERSION));
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
                Log.i(TAG, "PROFILE=" + PROFILE_NAMES[activeProfile] + " INSTANCES=" + INSTANCE_COUNTS[activeProfile]);
            }

            float time = (now - startNs) / 1_000_000_000.0f;
            GLES30.glClear(GLES30.GL_COLOR_BUFFER_BIT | GLES30.GL_DEPTH_BUFFER_BIT);
            GLES30.glUseProgram(program);
            GLES30.glUniformMatrix4fv(viewProjectionLocation, 1, false, viewProjection, 0);
            GLES30.glUniform1f(timeLocation, time);
            GLES30.glUniform1i(qualityLocation, FRAGMENT_STEPS[activeProfile]);
            GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, cubeBuffer);
            GLES30.glBindBuffer(GLES30.GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
            GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, instanceBuffer);
            GLES30.glDrawElementsInstanced(GLES30.GL_TRIANGLES, indexCount,
                    GLES30.GL_UNSIGNED_SHORT, 0, INSTANCE_COUNTS[activeProfile]);

            long elapsed = now - statsStartNs;
            if (elapsed >= 1_000_000_000L && frames > 0) {
                double fps = frames * 1_000_000_000.0 / elapsed;
                double avgMs = frameMsTotal / frames;
                minFps = Math.min(minFps, fps);
                String detail = String.format(Locale.US,
                        "TARGET 60  |  %s  |  %dx%d\n1s avg %.1f ms  |  1s low %.1f FPS",
                        PROFILE_NAMES[activeProfile], width, height, avgMs, minFps);
                activity.showStats(String.format(Locale.US, "%.1f FPS", fps), detail, (float) fps);
                Log.i(TAG, String.format(Locale.US,
                        "FPS=%.2f AVG_FRAME_MS=%.2f MIN_WINDOW_FPS=%.2f PROFILE=%s INSTANCES=%d RES=%dx%d",
                        fps, avgMs, minFps, PROFILE_NAMES[activeProfile],
                        INSTANCE_COUNTS[activeProfile], width, height));
                statsStartNs = now;
                frameMsTotal = 0;
                frames = 0;
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
