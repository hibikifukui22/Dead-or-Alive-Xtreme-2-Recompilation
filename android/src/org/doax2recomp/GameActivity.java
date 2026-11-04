package org.doax2recomp;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.hardware.input.InputManager;
import android.os.Bundle;
import android.view.InputDevice;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.RandomAccessFile;

/** The game: the SDL activity, with the launcher's arguments and on-screen controls. */
public class GameActivity extends SDLActivity {
    public static final String EXTRA_ARGS = "args";
    public static final String EXTRA_TOUCH_OPACITY = "touch_opacity";  // percent, 0 = off
    public static final String EXTRA_PAD_FILE = "pad_file";

    private TouchPad touchPad;
    private InputManager inputManager;
    private final InputManager.InputDeviceListener controllerListener =
            new InputManager.InputDeviceListener() {
                @Override
                public void onInputDeviceAdded(int id) {
                    updateTouchPad();
                }

                @Override
                public void onInputDeviceRemoved(int id) {
                    updateTouchPad();
                }

                @Override
                public void onInputDeviceChanged(int id) {
                    updateTouchPad();
                }
            };

    /** Name of a connected physical controller (Bluetooth, USB or clip-on), or null. */
    static String controllerName() {
        int wanted = InputDevice.SOURCE_GAMEPAD | InputDevice.SOURCE_JOYSTICK;
        for (int id : InputDevice.getDeviceIds()) {
            InputDevice device = InputDevice.getDevice(id);
            if (device != null && !device.isVirtual() && (device.getSources() & wanted) == wanted) {
                return device.getName();
            }
        }
        return null;
    }

    /** The touch controls show only while no controller is connected. */
    private void updateTouchPad() {
        if (touchPad != null) {
            touchPad.setVisibility(controllerName() == null ? View.VISIBLE : View.GONE);
        }
    }

    @Override
    protected String[] getLibraries() {
        return new String[] {"c++_shared", "rexruntime", "main"};
    }

    @Override
    protected String[] getArguments() {
        String[] args = getIntent().getStringArrayExtra(EXTRA_ARGS);
        return args != null ? args : new String[0];
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // Controller players do not touch the screen.
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        int opacity = getIntent().getIntExtra(EXTRA_TOUCH_OPACITY, 0);
        String padFile = getIntent().getStringExtra(EXTRA_PAD_FILE);
        if (opacity > 0 && padFile != null) {
            touchPad = new TouchPad(this, new File(padFile), opacity);
            addContentView(touchPad, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.MATCH_PARENT));
            updateTouchPad();
            inputManager = (InputManager) getSystemService(Context.INPUT_SERVICE);
            inputManager.registerInputDeviceListener(controllerListener, null);
        }
    }

    @Override
    protected void onDestroy() {
        if (inputManager != null) {
            inputManager.unregisterInputDeviceListener(controllerListener);
        }
        super.onDestroy();
    }

    /** On-screen controller: writes the runtime's test pad file when its state changes. */
    static final class TouchPad extends View {
        // XInput button bits.
        static final int UP = 0x1, DOWN = 0x2, LEFT = 0x4, RIGHT = 0x8, START = 0x10, BACK = 0x20,
                LB = 0x100, RB = 0x200, A = 0x1000, B = 0x2000, X = 0x4000, Y = 0x8000;

        static final class Key {
            final String label;
            final int bit;
            final float x, y, r;  // in screen heights; negative x is from the right edge
            final int color;

            Key(String label, int bit, float x, float y, float r, int color) {
                this.label = label;
                this.bit = bit;
                this.x = x;
                this.y = y;
                this.r = r;
                this.color = color;
            }
        }

        private static final int GRAY = Color.rgb(90, 100, 120);
        private final Key[] keys = {
            new Key("A", A, -0.20f, 0.84f, 0.080f, Color.rgb(90, 190, 40)),
            new Key("B", B, -0.09f, 0.70f, 0.080f, Color.rgb(225, 60, 50)),
            new Key("X", X, -0.31f, 0.70f, 0.080f, Color.rgb(50, 120, 230)),
            new Key("Y", Y, -0.20f, 0.56f, 0.080f, Color.rgb(235, 190, 40)),
            new Key("LB", LB, 0.11f, 0.11f, 0.075f, GRAY),
            new Key("RB", RB, -0.11f, 0.11f, 0.075f, GRAY),
            new Key("BACK", BACK, 0.72f, 0.92f, 0.060f, GRAY),
            new Key("START", START, -0.62f, 0.92f, 0.060f, GRAY),
            new Key("▲", UP, 0.56f, 0.52f, 0.055f, GRAY),
            new Key("▼", DOWN, 0.56f, 0.76f, 0.055f, GRAY),
            new Key("◀", LEFT, 0.44f, 0.64f, 0.055f, GRAY),
            new Key("▶", RIGHT, 0.68f, 0.64f, 0.055f, GRAY),
        };
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final File file;
        private final int alpha;
        private int buttons, lx, ly;
        private String last = "";
        private float stickX, stickY;
        private int stickPointer = -1;

        TouchPad(Context context, File file, int opacityPercent) {
            super(context);
            this.file = file;
            alpha = opacityPercent * 255 / 100;
            write();
        }

        private float keyX(Key k) {
            return k.x < 0 ? getWidth() + k.x * getHeight() : k.x * getHeight();
        }

        private float stickCenterX() {
            return 0.22f * getHeight();
        }

        private float stickCenterY() {
            return 0.70f * getHeight();
        }

        private float stickRadius() {
            return 0.18f * getHeight();
        }

        private void write() {
            String text = "buttons=" + Integer.toHexString(buttons) + " lx=" + lx + " ly=" + ly;
            if (text.equals(last)) {
                return;
            }
            last = text;
            // One fixed-length write over the old text: the game never reads an empty file.
            StringBuilder padded = new StringBuilder(text);
            while (padded.length() < 48) {
                padded.append(' ');
            }
            try (RandomAccessFile out = new RandomAccessFile(file, "rw")) {
                out.write(padded.toString().getBytes("US-ASCII"));
            } catch (Exception ignored) {
                // Input is simply not delivered.
            }
        }

        @Override
        public boolean onTouchEvent(MotionEvent e) {
            int newButtons = 0;
            int newStick = -1;
            int action = e.getActionMasked();
            float cx = stickCenterX(), cy = stickCenterY(), radius = stickRadius();
            for (int i = 0; i < e.getPointerCount(); ++i) {
                if ((action == MotionEvent.ACTION_POINTER_UP && i == e.getActionIndex())
                        || action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) {
                    continue;
                }
                float x = e.getX(i), y = e.getY(i);
                int id = e.getPointerId(i);
                float dx = x - cx, dy = y - cy;
                if (id == stickPointer || (stickPointer < 0 && newStick < 0
                        && dx * dx + dy * dy < radius * radius * 1.7f)) {
                    newStick = id;
                    float d = (float) Math.sqrt(dx * dx + dy * dy);
                    float s = d > radius ? radius / d : 1f;
                    stickX = dx * s;
                    stickY = dy * s;
                    continue;
                }
                for (Key k : keys) {
                    float kx = x - keyX(k), ky = y - k.y * getHeight(), r = k.r * getHeight() * 1.25f;
                    if (kx * kx + ky * ky < r * r) {
                        newButtons |= k.bit;
                    }
                }
            }
            stickPointer = newStick;
            if (stickPointer < 0) {
                stickX = 0;
                stickY = 0;
            }
            buttons = newButtons;
            lx = (int) (stickX / radius * 32767);
            ly = (int) (-stickY / radius * 32767);
            write();
            invalidate();
            return true;
        }

        @Override
        protected void onDraw(Canvas c) {
            float h = getHeight();
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(Color.argb(alpha / 2, 40, 50, 70));
            c.drawCircle(stickCenterX(), stickCenterY(), stickRadius(), paint);
            paint.setColor(Color.argb(alpha, 200, 210, 230));
            c.drawCircle(stickCenterX() + stickX, stickCenterY() + stickY, stickRadius() * 0.45f, paint);
            paint.setTextAlign(Paint.Align.CENTER);
            for (Key k : keys) {
                boolean down = (buttons & k.bit) != 0;
                int a = down ? Math.min(255, alpha + 80) : alpha;
                paint.setColor(Color.argb(a, Color.red(k.color), Color.green(k.color), Color.blue(k.color)));
                float x = keyX(k), y = k.y * h, r = k.r * h;
                c.drawCircle(x, y, r, paint);
                paint.setColor(Color.argb(a, 255, 255, 255));
                paint.setTextSize(k.label.length() > 2 ? r * 0.55f : r * 0.9f);
                c.drawText(k.label, x, y + paint.getTextSize() * 0.35f, paint);
            }
        }
    }
}
