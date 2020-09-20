
Debian
====================
This directory contains files used to package aokchaind/aokchain-qt
for Debian-based Linux systems. If you compile aokchaind/aokchain-qt yourself, there are some useful files here.

## aokchain: URI support ##


aokchain-qt.desktop  (Gnome / Open Desktop)
To install:

	sudo desktop-file-install aokchain-qt.desktop
	sudo update-desktop-database

If you build yourself, you will either need to modify the paths in
the .desktop file or copy or symlink your aokchain-qt binary to `/usr/bin`
and the `../../share/pixmaps/aokchain128.png` to `/usr/share/pixmaps`

aokchain-qt.protocol (KDE)

