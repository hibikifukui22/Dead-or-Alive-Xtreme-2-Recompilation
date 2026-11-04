package org.doax2recomp;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.hardware.input.InputManager;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.provider.DocumentsContract;
import android.provider.Settings;
import android.text.Layout;
import android.text.StaticLayout;
import android.text.TextPaint;
import android.view.MotionEvent;
import android.view.View;

import java.io.File;
import java.util.ArrayList;
import java.util.List;

/**
 * The launcher: the same look, pages and options as the PC launcher, laid out for a portrait
 * screen. Drawn in a 900-wide design space scaled to the screen width.
 */
public class LauncherActivity extends Activity {
    private static final String VERSION = "v1.1.6 Android test";
    private static final float DESIGN_W = 900;

    private static final int OCEAN = Color.rgb(30, 120, 214);
    private static final int DEEP_BLUE = Color.rgb(13, 79, 168);
    private static final int AQUA = Color.rgb(61, 181, 240);
    private static final int LIME = Color.rgb(184, 224, 42);
    private static final int GREEN = Color.rgb(104, 186, 32);
    private static final int NAVY = Color.rgb(8, 36, 84);
    private static final int SOFT_TEXT = Color.rgb(205, 228, 250);

    private static final int PAGE_MAIN = 0, PAGE_SETTINGS = 1, PAGE_GRAPHICS = 2, PAGE_INPUT = 3;
    private static final int REQUEST_GAME_FOLDER = 1;

    private SharedPreferences prefs;
    private File root, defaultGameDir, gameDir, userDir, logsDir;
    private String browseError;
    private LauncherView view;
    private InputManager inputManager;
    private final InputManager.InputDeviceListener controllerListener =
            new InputManager.InputDeviceListener() {
                @Override
                public void onInputDeviceAdded(int id) {
                    view.invalidate();
                }

                @Override
                public void onInputDeviceRemoved(int id) {
                    view.invalidate();
                }

                @Override
                public void onInputDeviceChanged(int id) {
                    view.invalidate();
                }
            };

    private int getInt(String key, int fallback) {
        return prefs.getInt(key, fallback);
    }

    private void setInt(String key, int value) {
        prefs.edit().putInt(key, value).apply();
    }

    private boolean getBool(String key, boolean fallback) {
        return prefs.getBoolean(key, fallback);
    }

    private void setBool(String key, boolean value) {
        prefs.edit().putBoolean(key, value).apply();
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        prefs = getSharedPreferences("launcher", MODE_PRIVATE);
        root = new File(Environment.getExternalStorageDirectory(), "DOAX2 Recomp");
        defaultGameDir = new File(root, "extracted game files here");
        String savedGameDir = prefs.getString("game_dir", null);
        gameDir = savedGameDir != null ? new File(savedGameDir) : defaultGameDir;
        userDir = new File(root, "userdata");
        logsDir = new File(root, "logs");
        view = new LauncherView(this);
        setContentView(view);
    }

    @Override
    protected void onResume() {
        super.onResume();
        makeFolders();
        view.refreshStatus();
        view.invalidate();
        inputManager = (InputManager) getSystemService(INPUT_SERVICE);
        inputManager.registerInputDeviceListener(controllerListener, null);
    }

    @Override
    protected void onPause() {
        inputManager.unregisterInputDeviceListener(controllerListener);
        super.onPause();
    }

    private boolean hasStorage() {
        return Environment.isExternalStorageManager();
    }

    private void makeFolders() {
        if (hasStorage()) {
            defaultGameDir.mkdirs();
            userDir.mkdirs();
            logsDir.mkdirs();
        }
    }

    private void askForStorage() {
        startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                Uri.parse("package:" + getPackageName())));
    }

    private boolean gameFound() {
        return new File(gameDir, "default.xex").isFile();
    }

    private void browseForGameFiles() {
        if (!hasStorage()) {
            askForStorage();
            return;
        }
        startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), REQUEST_GAME_FOLDER);
    }

    /** The plain path of a folder from the system picker, or null if it has none. */
    private File folderFromPicker(Uri tree) {
        if (!"com.android.externalstorage.documents".equals(tree.getAuthority())) {
            return null;
        }
        String id = DocumentsContract.getTreeDocumentId(tree);
        int colon = id.indexOf(':');
        if (colon < 0) {
            return null;
        }
        String volume = id.substring(0, colon), path = id.substring(colon + 1);
        File top;
        if (volume.equals("primary")) {
            top = Environment.getExternalStorageDirectory();
        } else if (volume.equals("home")) {
            top = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOCUMENTS);
        } else {
            top = new File("/storage", volume);  // SD card or USB drive
        }
        return path.isEmpty() ? top : new File(top, path);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQUEST_GAME_FOLDER || resultCode != RESULT_OK || data == null) {
            return;
        }
        File folder = folderFromPicker(data.getData());
        if (folder == null) {
            browseError = "That location cannot be used. Pick a folder on your device, SD card or USB drive.";
        } else if (!new File(folder, "default.xex").isFile()) {
            browseError = "default.xex is not in that folder. Pick the folder that contains it.";
        } else {
            browseError = null;
            gameDir = folder;
            prefs.edit().putString("game_dir", folder.getPath()).apply();
        }
    }

    private void launch() {
        File padFile = new File(root, "touch_pad.txt");
        boolean fps60 = getInt("fps", 60) == 60;
        boolean msaaOff = getInt("msaa", 2) == 2;
        boolean native2x = getInt("msaa", 2) == 0;
        boolean nativeRenderer = getInt("renderer", 1) == 1;
        boolean diagnostics = getBool("diagnostics", false);
        String[] aaNames = {"none", "fxaa", "fxaa_extreme"};
        int anisotropic = getInt("anisotropic", -1);
        String logName = "doax2_" + System.currentTimeMillis() / 1000 + ".log";

        List<String> a = new ArrayList<>();
        a.add("--gpu_plugin=xenos");
        a.add("--gpu_backend=vulkan");
        a.add("--game_data_root=" + gameDir.getPath());
        a.add("--user_data_root=" + userDir.getPath());
        a.add("--log_file=" + new File(logsDir, logName).getPath());
        a.add("--log_level=" + (diagnostics ? "debug" : "info"));
        a.add("--vulkan_allow_present_mode_immediate=false");
        a.add("--video_mode_width=1280");
        a.add("--video_mode_height=720");
        a.add("--resolution_scale=" + getInt("scale", 1));
        a.add("--anisotropic_override=" + anisotropic);
        a.add("--native_2x_msaa=" + native2x);
        // MSAA off also needs the markers (the game's clears are told apart).
        a.add("--doax2_native_markers=" + (nativeRenderer || msaaOff));
        a.add("--gpu_force_1x_msaa=" + msaaOff);
        a.add("--gpu_native_tiling=" + nativeRenderer);
        a.add("--gpu_native_clears=" + nativeRenderer);
        a.add("--gpu_native_transfer_elision=" + nativeRenderer);
        a.add("--d3d12_doax2_incremental_resolve=" + nativeRenderer);
        a.add("--vsync=" + getBool("vsync", true));
        a.add("--video_mode_refresh_rate=" + (fps60 ? 120 : 60));
        a.add("--vblank_counter_rate_multiplier=1");
        a.add("--doax2_engine_60fps=" + fps60);
        a.add("--doax2_shadow_quality=" + (getBool("shadows", true) ? 3 : 0));
        a.add("--doax2_music_volume_bridge=true");
        a.add("--gpu_allow_invalid_fetch_constants=true");
        a.add("--fullscreen=true");
        a.add("--doax2_fps_overlay=" + getBool("fps_overlay", false));
        a.add("--swap_post_effect=" + aaNames[getInt("post_aa", 0)]);
        a.add("--execute_unclipped_draw_vs_on_cpu=true");
        a.add("--execute_unclipped_draw_vs_on_cpu_with_scissor=true");
        a.add("--gpu_display_locked_vblank=true");
        a.add("--gpu_precise_vblank_timer=true");
        a.add("--present_ui_with_guest_frames=true");
        a.add("--doax2_native60_counter_scan=" + diagnostics);
        a.add("--xmp_keep_song_across_playlists=" + getBool("keep_song", false));
        a.add("--doax2_money_multiplier=" + getInt("money", 1));
        // Always-connected device: the game sees a controller with touch controls alone.
        a.add("--mnk_mode=true");
        a.add("--mnk_mouse=false");
        a.add("--input_test_pad_file=" + padFile.getPath());
        a.add("--hid_mappings_file=" + new File(root, "gamecontrollerdb.txt").getPath());
        if (fps60) {
            a.add("--doax2_native60_tick_scale=0.5");
            a.add("--doax2_native60_menu_transition_scale=2");
            a.add("--doax2_native60_menu_window_scale=4");
            a.add("--doax2_native60_menu_repeat_scale=2");
        }

        Intent intent = new Intent(this, GameActivity.class);
        intent.putExtra(GameActivity.EXTRA_ARGS, a.toArray(new String[0]));
        intent.putExtra(GameActivity.EXTRA_PAD_FILE, padFile.getPath());
        intent.putExtra(GameActivity.EXTRA_TOUCH_OPACITY,
                getBool("touch", true) ? getInt("touch_opacity", 50) : 0);
        startActivity(intent);
    }

    /** One settings row: title, help text and a control. */
    private abstract static class Row {
        final String title, help;

        Row(String title, String help) {
            this.title = title;
            this.help = help;
        }

        /** Text on the control, or null for a toggle. */
        String value() {
            return null;
        }

        boolean on() {
            return false;
        }

        /** Direction is -1 or 1; 0 for a toggle or a button. */
        abstract void change(int direction);

        boolean isButton() {
            return false;
        }
    }

    private final class Toggle extends Row {
        final String key;
        final boolean fallback;

        Toggle(String title, String help, String key, boolean fallback) {
            super(title, help);
            this.key = key;
            this.fallback = fallback;
        }

        @Override
        boolean on() {
            return getBool(key, fallback);
        }

        @Override
        void change(int direction) {
            setBool(key, !on());
        }
    }

    private final class Choice extends Row {
        final String key;
        final int fallback;
        final int[] values;
        final String[] names;

        Choice(String title, String help, String key, int fallback, int[] values, String[] names) {
            super(title, help);
            this.key = key;
            this.fallback = fallback;
            this.values = values;
            this.names = names;
        }

        private int index() {
            int current = getInt(key, fallback);
            for (int i = 0; i < values.length; ++i) {
                if (values[i] == current) {
                    return i;
                }
            }
            return 0;
        }

        @Override
        String value() {
            return names[index()];
        }

        @Override
        void change(int direction) {
            setInt(key, values[(index() + direction + values.length) % values.length]);
        }
    }

    private List<Row> settingsRows() {
        List<Row> rows = new ArrayList<>();
        rows.add(new Row("Game Files",
                "The folder with your extracted game files: the one that contains default.xex. It can "
                + "be anywhere on your device, an SD card or a USB drive.\n\nAndroid does not allow "
                + "picking the top of a drive or the Download folder itself, so use a folder inside "
                + "them.") {
            @Override
            String value() {
                return "BROWSE: " + gameDir.getName();
            }

            @Override
            boolean isButton() {
                return true;
            }

            @Override
            void change(int direction) {
                browseForGameFiles();
            }
        });
        rows.add(new Toggle("Continuous Music Playback",
                "The current song keeps playing to its end instead of restarting whenever you change "
                + "scenes, start a minigame or an event begins. When it ends, the soundtrack carries on "
                + "with the next song.\n\nPre-rendered movies and returning to the title screen still "
                + "stop the music. Off is the game's original behaviour.", "keep_song", false));
        rows.add(new Choice("Winnings Multiplier",
                "Multiplies the money you win in the casino (blackjack, poker, roulette, slots) and the "
                + "prize money from minigames.\n\nOnly winnings are multiplied: bets, losses and shop "
                + "prices stay the same. Normal is the game's original amounts.", "money", 1,
                new int[] {1, 10, 15, 20, 100}, new String[] {"Normal", "10x", "15x", "20x", "100x"}));
        rows.add(new Toggle("Show FPS Overlay",
                "Shows the current frame rate in the corner of the screen.\n\nUseful for checking that "
                + "the game holds 30 or 60 FPS on your device.", "fps_overlay", false));
        rows.add(new Toggle("Diagnostic Logging",
                "Writes more detail to the logs for bug reports.\n\nIt lowers performance, so leave it "
                + "off for normal play and turn it on only when you are reporting a problem.",
                "diagnostics", false));
        rows.add(new Row("Log Files",
                "Logs are in the DOAX2 Recomp/logs folder on your device's storage. Open it with your "
                + "file manager.\n\nWhen reporting a problem, attach the newest doax2_<number>.log. "
                + "Never share game files.") {
            @Override
            String value() {
                return "DOAX2 Recomp/logs";
            }

            @Override
            boolean isButton() {
                return true;
            }

            @Override
            void change(int direction) {
                view.showHelp(this);
            }
        });
        return rows;
    }

    private List<Row> graphicsRows() {
        List<Row> rows = new ArrayList<>();
        rows.add(new Choice("Resolution Scale",
                "Renders the game at a multiple of the Xbox 360's 1280 x 720 resolution: 2x is "
                + "2560 x 1440.\n\nHigher values give a much sharper image but need a much faster GPU, "
                + "and use more battery. 1x is the original look.", "scale", 1, new int[] {1, 2, 3},
                new String[] {"1x (native)", "2x", "3x"}));
        rows.add(new Choice("MSAA Mode",
                "On the Xbox 360 the game smooths edges with 2x multisample anti-aliasing (MSAA).\n\n"
                + "Native uses your GPU's own 2x MSAA. Emulated uses a slower compatibility path; switch "
                + "to it only if you see rendering problems. Off draws without MSAA: the fastest, and "
                + "the default on phones; turn on Post-Process Anti-Aliasing (FXAA) to smooth the edges "
                + "instead.", "msaa", 2, new int[] {0, 1, 2},
                new String[] {"Native 2x", "Emulated 2x", "Off (use FXAA)"}));
        rows.add(new Choice("Renderer",
                "Vulkan (Native) draws the game's render passes the way the game describes them instead "
                + "of emulating the Xbox 360's 10 MB EDRAM: lower CPU and GPU load.\n\nVulkan (Emulated) "
                + "is the original, slowest path; switch to it if you see anything drawn wrongly, and "
                + "report the problem.", "renderer", 1, new int[] {1, 0},
                new String[] {"Vulkan (Native)", "Vulkan (Emulated)"}));
        rows.add(new Choice("Post-Process Anti-Aliasing",
                "Adds FXAA, an extra edge-smoothing filter, to the final image. FXAA Extreme smooths "
                + "more.\n\nBoth make the picture slightly softer and cost a little performance. Off "
                + "keeps the original image.", "post_aa", 0, new int[] {0, 1, 2},
                new String[] {"Off", "FXAA", "FXAA Extreme"}));
        rows.add(new Toggle("Character Shadows",
                "The real-time shadows that the characters and moving objects cast.\n\nTurning them off "
                + "saves a little GPU and CPU time on slower devices. Shadows that are part of the "
                + "scenery (trees, buildings) stay.", "shadows", true));
        rows.add(new Choice("Anisotropic Filtering",
                "Keeps textures sharp when they are seen at a steep angle, such as the sand, the water "
                + "and floors.\n\nGame default keeps the game's own setting; 16x is the sharpest.",
                "anisotropic", -1, new int[] {-1, 0, 1, 2, 3, 4, 5},
                new String[] {"Game default", "Off", "1x", "2x", "4x", "8x", "16x"}));
        rows.add(new Toggle("Guest V-Sync",
                "Paces the game from the Xbox 360's refresh signal, the way the console does.\n\nIt must "
                + "stay on for 60 FPS: without it the game can run too fast.", "vsync", true));
        return rows;
    }

    private List<Row> inputRows() {
        List<Row> rows = new ArrayList<>();
        rows.add(new Toggle("On-Screen Controls",
                "Shows a touch controller over the game: left stick, A / B / X / Y, LB / RB, the D-pad, "
                + "Back and Start.\n\nThey hide by themselves while a controller is connected and come "
                + "back when it disconnects.", "touch", true));
        rows.add(new Choice("On-Screen Controls Opacity",
                "How solid the touch controls look over the game.", "touch_opacity", 50,
                new int[] {25, 50, 75, 100}, new String[] {"25%", "50%", "75%", "100%"}));
        rows.add(new Row("Controller",
                "Xbox, DualSense and DualShock 4 controllers over Bluetooth or USB, and clip-on "
                + "controllers such as the Backbone, work in the game. Pair a Bluetooth controller in "
                + "your device's settings; it can connect before or during play.\n\nIf the buttons of "
                + "an unusual controller are mixed up, put a gamecontrollerdb.txt with its mapping in "
                + "the DOAX2 Recomp folder.") {
            @Override
            String value() {
                String name = GameActivity.controllerName();
                return name != null ? name : "Not found";
            }

            @Override
            boolean isButton() {
                return true;
            }

            @Override
            void change(int direction) {
                view.showHelp(this);
            }
        });
        return rows;
    }

    private final class LauncherView extends View {
        private final class Hit {
            final RectF rect;
            final Runnable action;

            Hit(RectF rect, Runnable action) {
                this.rect = rect;
                this.action = action;
            }
        }

        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
        private final TextPaint text = new TextPaint(Paint.ANTI_ALIAS_FLAG);
        private final List<Hit> hits = new ArrayList<>();
        private final Bitmap background, logo;
        private int page = PAGE_MAIN;
        private Row help;
        private String status = "";
        private boolean statusError;
        private float scroll, maxScroll, downY, lastY;
        private boolean dragged;

        LauncherView(Context context) {
            super(context);
            background = loadAsset("background.jpg");
            logo = loadAsset("logo.png");
        }

        private Bitmap loadAsset(String name) {
            try (java.io.InputStream in = getAssets().open(name)) {
                return BitmapFactory.decodeStream(in);
            } catch (Exception e) {
                return null;
            }
        }

        void refreshStatus() {
            if (!hasStorage()) {
                status = "Tap here to allow access to your files.";
                statusError = true;
            } else if (browseError != null) {
                status = browseError;
                statusError = true;
            } else if (!gameFound()) {
                status = "Game files not found. Tap BROWSE and pick the folder with your extracted game "
                        + "files.";
                statusError = true;
            } else {
                status = "Ready to play.";
                statusError = false;
            }
        }

        void showHelp(Row row) {
            help = row;
            invalidate();
        }

        private void setPage(int newPage) {
            page = newPage;
            scroll = 0;
            invalidate();
        }

        private void fillRound(Canvas c, RectF r, float radius, int color) {
            paint.setShader(null);
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(color);
            c.drawRoundRect(r, radius, radius, paint);
        }

        private void strokeRound(Canvas c, RectF r, float radius, int color, float width) {
            paint.setShader(null);
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(width);
            paint.setColor(color);
            c.drawRoundRect(r, radius, radius, paint);
            paint.setStyle(Paint.Style.FILL);
        }

        private void gradientRound(Canvas c, RectF r, float radius, int top, int bottom) {
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(Color.BLACK);  // The paint's alpha would apply to the gradient.
            paint.setShader(new LinearGradient(0, r.top, 0, r.bottom, top, bottom, Shader.TileMode.CLAMP));
            c.drawRoundRect(r, radius, radius, paint);
            paint.setShader(null);
        }

        private void label(Canvas c, String s, float x, float y, float size, boolean bold, int color,
                Paint.Align align) {
            text.setTypeface(bold ? Typeface.DEFAULT_BOLD : Typeface.DEFAULT);
            text.setTextSize(size);
            text.setTextAlign(align);
            text.setColor(color);
            c.drawText(s, x, y, text);
        }

        private void centered(Canvas c, String s, RectF r, float size, boolean bold, int color) {
            text.setTypeface(bold ? Typeface.DEFAULT_BOLD : Typeface.DEFAULT);
            float fitted = size;
            text.setTextSize(fitted);
            while (fitted > 12 && text.measureText(s) > r.width() - 12) {
                fitted -= 1;
                text.setTextSize(fitted);
            }
            label(c, s, r.centerX(), r.centerY() + fitted * 0.36f, fitted, bold, color, Paint.Align.CENTER);
        }

        private StaticLayout wrap(String s, float width, float size, boolean bold, int color,
                Layout.Alignment alignment) {
            TextPaint p = new TextPaint(Paint.ANTI_ALIAS_FLAG);
            p.setTypeface(bold ? Typeface.DEFAULT_BOLD : Typeface.DEFAULT);
            p.setTextSize(size);
            p.setColor(color);
            return StaticLayout.Builder.obtain(s, 0, s.length(), p, (int) width).setAlignment(alignment).build();
        }

        /** Glossy button, like the PC launcher's. */
        private void button(Canvas c, RectF r, String s, float size, int style, Runnable action) {
            // Style: 0 normal, 1 accent (PLAY), 2 selected, 3 unselected.
            boolean accent = style == 1;
            int top = accent ? LIME : style == 2 ? AQUA : OCEAN;
            int bottom = accent ? GREEN : style == 2 ? OCEAN : DEEP_BLUE;
            if (style == 3) {
                top = Color.argb(215, 24, 58, 112);
                bottom = Color.argb(215, 8, 36, 84);
            }
            float radius = Math.min(r.height() / 2, 34);
            fillRound(c, new RectF(r.left, r.top + 4, r.right, r.bottom + 4), radius, Color.argb(70, 0, 20, 60));
            gradientRound(c, r, radius, top, bottom);
            if (style != 3) {
                RectF gloss = new RectF(r.left + 5, r.top + 4, r.right - 5, r.top + r.height() * 0.48f);
                gradientRound(c, gloss, radius * 0.8f, Color.argb(110, 255, 255, 255), Color.argb(0, 255, 255, 255));
            }
            strokeRound(c, r, radius, Color.argb(style == 2 ? 255 : style == 3 ? 110 : 170, 255, 255, 255),
                    style == 2 ? 3.5f : 2f);
            centered(c, s, r, size, true, accent ? NAVY : style == 3 ? Color.rgb(190, 210, 235) : Color.WHITE);
            hits.add(new Hit(new RectF(r), action));
        }

        private void toggle(Canvas c, RectF r, final Row row) {
            boolean on = row.on();
            float radius = r.height() / 2;
            gradientRound(c, r, radius, on ? LIME : Color.rgb(70, 92, 128), on ? GREEN : Color.rgb(40, 58, 92));
            strokeRound(c, r, radius, Color.argb(200, 255, 255, 255), 2.5f);
            float knob = r.height() - 10;
            float x = on ? r.right - knob - 5 : r.left + 5;
            paint.setColor(Color.WHITE);
            c.drawOval(new RectF(x, r.top + 5, x + knob, r.top + 5 + knob), paint);
            RectF t = on ? new RectF(r.left + 8, r.top, r.right - knob - 10, r.bottom)
                         : new RectF(r.left + knob + 10, r.top, r.right - 8, r.bottom);
            centered(c, on ? "ON" : "OFF", t, 24, true, on ? NAVY : Color.WHITE);
            hits.add(new Hit(new RectF(r), () -> {
                row.change(0);
                invalidate();
            }));
        }

        private void selector(Canvas c, RectF r, final Row row) {
            float radius = r.height() / 2;
            fillRound(c, r, radius, Color.argb(215, 8, 36, 84));
            strokeRound(c, r, radius, Color.argb(200, 255, 255, 255), 2.5f);
            float arrow = r.height() * 1.15f;
            RectF left = new RectF(r.left, r.top, r.left + arrow, r.bottom);
            RectF right = new RectF(r.right - arrow, r.top, r.right, r.bottom);
            centered(c, "‹", left, 42, true, LIME);
            centered(c, "›", right, 42, true, LIME);
            centered(c, row.value(), new RectF(left.right, r.top, right.left, r.bottom), 26, true, Color.WHITE);
            hits.add(new Hit(left, () -> {
                row.change(-1);
                invalidate();
            }));
            hits.add(new Hit(new RectF(left.right, r.top, r.right, r.bottom), () -> {
                row.change(1);
                invalidate();
            }));
        }

        private void statusBar(Canvas c, RectF r) {
            fillRound(c, r, r.height() / 2, statusError ? Color.argb(215, 150, 20, 60) : Color.argb(190, 8, 36, 84));
            StaticLayout layout = wrap(status, r.width() - 50, 26, true, Color.WHITE, Layout.Alignment.ALIGN_CENTER);
            c.save();
            c.translate(r.left + 25, r.centerY() - layout.getHeight() / 2f);
            layout.draw(c);
            c.restore();
            if (!hasStorage()) {
                hits.add(new Hit(new RectF(r), LauncherActivity.this::askForStorage));
            }
        }

        private void paintMain(Canvas c, float height) {
            boolean browse = hasStorage() && !gameFound();
            if (logo != null) {
                // Smaller on the short screens of tablets, to leave room for the controls.
                float aspect = (float) logo.getWidth() / logo.getHeight();
                float room = height - 800 - (browse ? 100 : 0) - 110;
                float w = Math.min(760, room * aspect), h = w / aspect;
                c.drawBitmap(logo, null, new RectF((DESIGN_W - w) / 2, 90, (DESIGN_W + w) / 2, 90 + h), paint);
            }
            label(c, VERSION, DESIGN_W - 30, 50, 26, true, Color.WHITE, Paint.Align.RIGHT);

            float y = height - 800;
            if (browse) {
                button(c, new RectF(50, y - 96, DESIGN_W - 50, y - 16), "BROWSE FOR GAME FILES", 28, 0,
                        LauncherActivity.this::browseForGameFiles);
            }
            statusBar(c, new RectF(50, y, DESIGN_W - 50, y + 100));
            y += 130;
            centered(c, "FRAME RATE", new RectF(50, y, DESIGN_W - 50, y + 40), 28, true, Color.WHITE);
            y += 52;
            int fps = getInt("fps", 60);
            button(c, new RectF(50, y, 435, y + 90), "30 FPS", 36, fps == 30 ? 2 : 3, () -> {
                setInt("fps", 30);
                invalidate();
            });
            button(c, new RectF(465, y, DESIGN_W - 50, y + 90), "60 FPS", 36, fps == 60 ? 2 : 3, () -> {
                setInt("fps", 60);
                invalidate();
            });
            y += 118;
            button(c, new RectF(50, y, DESIGN_W - 50, y + 150), "PLAY", 72, 1, () -> {
                refreshStatus();
                if (hasStorage() && gameFound()) {
                    launch();
                } else {
                    invalidate();
                }
            });
            y += 178;
            button(c, new RectF(50, y, DESIGN_W - 50, y + 76), "SETTINGS", 28, 0, () -> setPage(PAGE_SETTINGS));
            y += 90;
            button(c, new RectF(50, y, 435, y + 76), "GRAPHICS SETTINGS", 26, 0, () -> setPage(PAGE_GRAPHICS));
            button(c, new RectF(465, y, DESIGN_W - 50, y + 76), "INPUT", 28, 0, () -> setPage(PAGE_INPUT));
        }

        private void paintRows(Canvas c, String title, List<Row> rows, float height) {
            float panelTop = 120, panelBottom = height - 150;
            centered(c, title, new RectF(0, 34, DESIGN_W, 96), 46, true, Color.WHITE);
            RectF panel = new RectF(30, panelTop, DESIGN_W - 30, panelBottom);
            fillRound(c, panel, 32, Color.argb(190, 8, 36, 84));

            float rowHeight = 188;
            float total = rows.size() * rowHeight + 20;
            maxScroll = Math.max(0, total - panel.height());
            scroll = Math.max(0, Math.min(scroll, maxScroll));

            c.save();
            c.clipRect(panel);
            float y = panelTop + 10 - scroll;
            for (final Row row : rows) {
                if (y + rowHeight > panelTop && y < panelBottom) {
                    label(c, row.title, 66, y + 56, 32, true, Color.WHITE, Paint.Align.LEFT);
                    RectF info = new RectF(DESIGN_W - 122, y + 16, DESIGN_W - 66, y + 72);
                    gradientRound(c, info, 28, OCEAN, DEEP_BLUE);
                    strokeRound(c, info, 28, Color.argb(210, 255, 255, 255), 2.5f);
                    centered(c, "?", info, 34, true, Color.WHITE);
                    hits.add(new Hit(info, () -> showHelp(row)));
                    RectF control = new RectF(66, y + 88, DESIGN_W - 66, y + 160);
                    if (row.isButton()) {
                        button(c, control, row.value(), 27, 0, () -> row.change(0));
                    } else if (row.value() == null) {
                        toggle(c, new RectF(control.right - 190, control.top, control.right, control.bottom), row);
                    } else {
                        selector(c, control, row);
                    }
                }
                y += rowHeight;
            }
            c.restore();
            // Controls scrolled out of the panel must not take touches.
            for (int i = hits.size() - 1; i >= 0; --i) {
                RectF r = hits.get(i).rect;
                if (r.centerY() < panelTop || r.centerY() > panelBottom) {
                    hits.remove(i);
                }
            }

            button(c, new RectF(50, height - 124, 340, height - 40), "‹  BACK", 34, 0, () -> setPage(PAGE_MAIN));
            label(c, "Changes are saved automatically.", DESIGN_W - 50, height - 72, 24, false, Color.WHITE,
                    Paint.Align.RIGHT);
        }

        private void paintHelp(Canvas c, float height) {
            hits.clear();
            paint.setColor(Color.argb(110, 0, 20, 50));
            c.drawRect(0, 0, DESIGN_W, height, paint);
            StaticLayout title = wrap(help.title, 720, 38, true, Color.WHITE, Layout.Alignment.ALIGN_CENTER);
            StaticLayout body = wrap(help.help, 720, 29, false, SOFT_TEXT, Layout.Alignment.ALIGN_NORMAL);
            float cardHeight = title.getHeight() + body.getHeight() + 220;
            float top = (height - cardHeight) / 2;
            RectF card = new RectF(50, top, DESIGN_W - 50, top + cardHeight);
            fillRound(c, card, 36, Color.argb(245, 8, 36, 84));
            strokeRound(c, card, 36, Color.argb(210, 255, 255, 255), 2.5f);
            c.save();
            c.translate(90, top + 36);
            title.draw(c);
            c.translate(0, title.getHeight() + 28);
            body.draw(c);
            c.restore();
            button(c, new RectF(300, card.bottom - 110, 600, card.bottom - 34), "CLOSE", 30, 0, () -> {
                help = null;
                invalidate();
            });
        }

        @Override
        protected void onDraw(Canvas c) {
            hits.clear();
            if (background != null) {
                float cover = Math.max((float) getWidth() / background.getWidth(),
                        (float) getHeight() / background.getHeight());
                float w = background.getWidth() * cover, h = background.getHeight() * cover;
                float x = (getWidth() - w) / 2, y = (getHeight() - h) / 2;
                c.drawBitmap(background, new Rect(0, 0, background.getWidth(), background.getHeight()),
                        new RectF(x, y, x + w, y + h), paint);
            } else {
                c.drawColor(DEEP_BLUE);
            }
            float scale = getWidth() / DESIGN_W;
            float height = getHeight() / scale;
            c.save();
            c.scale(scale, scale);
            switch (page) {
                case PAGE_SETTINGS: paintRows(c, "SETTINGS", settingsRows(), height); break;
                case PAGE_GRAPHICS: paintRows(c, "GRAPHICS SETTINGS", graphicsRows(), height); break;
                case PAGE_INPUT: paintRows(c, "INPUT", inputRows(), height); break;
                default: paintMain(c, height); break;
            }
            if (help != null) {
                paintHelp(c, height);
            }
            c.restore();
        }

        @Override
        public boolean onTouchEvent(MotionEvent e) {
            float scale = getWidth() / DESIGN_W;
            float x = e.getX() / scale, y = e.getY() / scale;
            switch (e.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                    downY = y;
                    lastY = y;
                    dragged = false;
                    return true;
                case MotionEvent.ACTION_MOVE:
                    if (Math.abs(y - downY) > 16) {
                        dragged = true;
                    }
                    if (dragged && page != PAGE_MAIN && help == null) {
                        scroll = Math.max(0, Math.min(scroll + lastY - y, maxScroll));
                        invalidate();
                    }
                    lastY = y;
                    return true;
                case MotionEvent.ACTION_UP:
                    if (!dragged) {
                        for (int i = hits.size() - 1; i >= 0; --i) {
                            if (hits.get(i).rect.contains(x, y)) {
                                hits.get(i).action.run();
                                break;
                            }
                        }
                    }
                    return true;
                default:
                    return true;
            }
        }
    }

    @Override
    public void onBackPressed() {
        if (view.help != null) {
            view.help = null;
            view.invalidate();
        } else if (view.page != PAGE_MAIN) {
            view.setPage(PAGE_MAIN);
        } else {
            super.onBackPressed();
        }
    }
}
