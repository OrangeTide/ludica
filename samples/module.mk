SUBDIRS = demo01_retrocrt demo02_multiscroll demo03_text_dialogs demo04_sprites demo05_audio demo06_slugtext demo07_vfont demo08_picking ansiview hero tridrop lilpc

# cliptest, dndtest, and dragtest drive a raw-Xlib second client, so they only
# build where X11 exists. Excludes the Emscripten and Windows targets.
ifeq ($(_TARGET_OS),Linux)
SUBDIRS += cliptest dndtest dragtest
endif
