This fork of the mGBA test suite runs every suite automatically on boot
instead of waiting at the menu, and reports results over the mGBA debug
channel, so emulators can drive it unattended:

BEGIN: <suite name>
END: <passes>/<total>
...
SKIP: Video tests
ALL DONE
