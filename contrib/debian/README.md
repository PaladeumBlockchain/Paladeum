
Debian
====================
This directory contains files used to package paladeumd/paladeum-qt
for Debian-based Linux systems. If you compile paladeumd/paladeum-qt yourself, there are some useful files here.

## paladeum: URI support ##


paladeum-qt.desktop  (Gnome / Open Desktop)
To install:

	sudo desktop-file-install paladeum-qt.desktop
	sudo update-desktop-database

If you build yourself, you will either need to modify the paths in
the .desktop file or copy or symlink your paladeum-qt binary to `/usr/bin`
and the `../../share/pixmaps/paladeum128.png` to `/usr/share/pixmaps`

paladeum-qt.protocol (KDE)

