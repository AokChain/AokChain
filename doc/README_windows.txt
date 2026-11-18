AokChain Core
=============

Intro
-----
AokChain is a free open source peer-to-peer electronic cash system that is
completely decentralized, without the need for a central server or trusted
parties.  Users hold the crypto keys to their own money and transact directly
with each other, with the help of a P2P network to check for double-spending.


Setup
-----
Run the setup program (aokchain-<version>-win64-setup.exe) and start AokChain Core
from the Start menu, or unpack the archive into a directory and run aokchain-qt.exe.

The setup program puts the wallet program into the chosen folder and the node
programs (aokchaind, aokchain-cli, aokchain-tx, aokchain-wallet) into its
"daemon" folder. The wallet, the settings and the block data are kept in the
data folder, %APPDATA%\AokChain unless another one is chosen at the first start.
Neither the setup program nor its uninstaller touches that folder: keep a copy
of wallet.dat before you install a new version.

AokChain Core is the original AokChain client and it builds the backbone of the network.
However, it downloads and stores the entire history of AokChain transactions;
depending on the speed of your computer and network connection, the synchronization
process can take anywhere from a few hours to a day or more.
