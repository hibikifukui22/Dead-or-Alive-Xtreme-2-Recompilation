DOAX2 Recomp v1.1.6 - Android test build
========================================

This is a first test build. It has not run on a real device yet, so expect
problems. It does not include the game: you need your own extracted copy of
Dead or Alive Xtreme 2 for the Xbox 360.

You need an ARM64 Android 10 or newer phone or tablet with Vulkan.


Install
-------
1. Copy DOAX2-Recomp-v1.1.6-android-test.apk to the device and open it.
   Allow installing from that app when Android asks.
2. Open DOAX2 Recomp. Tap the red bar and turn on "Allow access to manage all
   files", then go back.
3. Copy your extracted game files to the device. Either:
   - put them in "DOAX2 Recomp/extracted game files here", or
   - put them anywhere and tap BROWSE FOR GAME FILES, then pick the folder
     that contains default.xex (also in Settings, under Game Files).
     Android does not allow picking the top of a drive or the Download folder
     itself: use a folder inside them.
4. Press PLAY.


Controllers
-----------
Touch controls appear over the game. They hide by themselves while a
controller is connected and come back when it disconnects.

Xbox, DualSense and DualShock 4 controllers over Bluetooth or USB, and clip-on
controllers such as the Backbone, are expected to work. Pair Bluetooth
controllers in Android's settings. The Input page shows the connected
controller's name.

If an unusual controller's buttons are mixed up, put a gamecontrollerdb.txt
with its mapping in the "DOAX2 Recomp" folder.


Settings for a first run
------------------------
The defaults suit phones: Resolution Scale 1x, MSAA Mode Off, Vulkan (Native).
If the picture looks wrong, try Vulkan (Emulated) on the Graphics page. If it
runs slowly, try 30 FPS or turn off Character Shadows.


Reporting problems
------------------
Logs are in "DOAX2 Recomp/logs". Send the newest doax2_<number>.log with what
happened, your device model and your settings. If the app closes at once, turn
on Diagnostic Logging in Settings and try again. Never share game files.
