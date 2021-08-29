
Debian
====================
This directory contains files used to package yonad/yona-qt
for Debian-based Linux systems. If you compile yonad/yona-qt yourself, there are some useful files here.

## yona: URI support ##


yona-qt.desktop  (Gnome / Open Desktop)
To install:

	sudo desktop-file-install yona-qt.desktop
	sudo update-desktop-database

If you build yourself, you will either need to modify the paths in
the .desktop file or copy or symlink your yona-qt binary to `/usr/bin`
and the `../../share/pixmaps/yona128.png` to `/usr/share/pixmaps`

yona-qt.protocol (KDE)

