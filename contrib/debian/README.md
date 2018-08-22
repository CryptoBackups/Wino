
Debian
====================
This directory contains files used to package winod/wino-qt
for Debian-based Linux systems. If you compile winod/wino-qt yourself, there are some useful files here.

## wino: URI support ##


wino-qt.desktop  (Gnome / Open Desktop)
To install:

	sudo desktop-file-install wino-qt.desktop
	sudo update-desktop-database

If you build yourself, you will either need to modify the paths in
the .desktop file or copy or symlink your winoqt binary to `/usr/bin`
and the `../../share/pixmaps/wino128.png` to `/usr/share/pixmaps`

wino-qt.protocol (KDE)

