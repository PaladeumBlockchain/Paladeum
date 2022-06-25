
Debian
====================
This directory contains files used to package akilad/akila-qt
for Debian-based Linux systems. If you compile akilad/akila-qt yourself, there are some useful files here.

## akila: URI support ##


akila-qt.desktop  (Gnome / Open Desktop)
To install:

	sudo desktop-file-install akila-qt.desktop
	sudo update-desktop-database

If you build yourself, you will either need to modify the paths in
the .desktop file or copy or symlink your akila-qt binary to `/usr/bin`
and the `../../share/pixmaps/akila128.png` to `/usr/share/pixmaps`

akila-qt.protocol (KDE)

