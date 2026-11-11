// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef AOKCHAIN_QT_GUICONSTANTS_H
#define AOKCHAIN_QT_GUICONSTANTS_H

#include "guiutil.h"
#include <QColor>

/* Milliseconds between model updates */
static const int MODEL_UPDATE_DELAY = 250;

/* AskPassphraseDialog -- Maximum passphrase length */
static const int MAX_PASSPHRASE_SIZE = 1024;

/* AokChainGUI -- Size of icons in status bar */
static const int STATUSBAR_ICONSIZE = 16;

static const bool DEFAULT_SPLASHSCREEN = true;

/* Invalid field background style */
#define STYLE_INVALID GUIUtil::invalidFieldStyle()
#define STYLE_VALID GUIUtil::validFieldStyle()

/* Transaction list -- unconfirmed transaction */
#define COLOR_UNCONFIRMED GUIUtil::theme().textDisabled
/* Transaction list -- negative amount */
#define COLOR_NEGATIVE GUIUtil::theme().negative
/* Transaction list -- bare address (without label) */
#define COLOR_BAREADDRESS GUIUtil::theme().textSecondary
/* Transaction list -- TX status decoration - open until date */
#define COLOR_TX_STATUS_OPENUNTILDATE GUIUtil::theme().accent
/* Transaction list -- TX status decoration - danger, tx needs attention */
#define COLOR_TX_STATUS_DANGER GUIUtil::theme().negative
/* Transaction list -- TX status decoration - default color */
#define COLOR_BLACK QColor(0, 0, 0)
/* Widget Background color - default color */
#define COLOR_WHITE QColor(255, 255, 255)

/* Color of labels */
#define COLOR_LABELS GUIUtil::theme().text
// #define COLOR_LABELS QColor("#4960ad")

/** LIGHT MODE */
/* Background color, very light gray */
#define COLOR_BACKGROUND_LIGHT QColor("#F4F6F8")
/* AokChain dark orange */
#define COLOR_DARK_ORANGE GUIUtil::theme().accent
/* AokChain light orange */
#define COLOR_LIGHT_ORANGE GUIUtil::theme().accentHover
/* AokChain dark blue */
#define COLOR_DARK_BLUE GUIUtil::theme().accent
/* AokChain light blue */
#define COLOR_LIGHT_BLUE GUIUtil::theme().accentHover
/* AokChain dark grar */
#define COLOR_DARK_GRAY QColor("#5d5d5d")
/* AokChain light gray */
#define COLOR_LIGHT_GRAY QColor("#777777")
/* AokChain token text */
#define COLOR_TOKEN_TEXT QColor(255, 255, 255)
/* AokChain shadow color - light mode */
#define COLOR_SHADOW_LIGHT QColor("#DFE4EA")
/* Toolbar not selected text color */
#define COLOR_TOOLBAR_NOT_SELECTED_TEXT QColor("#ffffff")
/* Toolbar selected text color */
#define COLOR_TOOLBAR_SELECTED_TEXT COLOR_WHITE


/** DARK MODE */
/* Widget background color, dark mode */
#define COLOR_WIDGET_BACKGROUND_DARK QColor("#1A2027")
/* AokChain shadow color - dark mode */
#define COLOR_SHADOW_DARK QColor("#2B343E")
/* AokChain Light blue - dark mode - dark mode */
#define COLOR_LIGHT_BLUE_DARK QColor("#1e2636")
/* AokChain Dark blue - dark mode - dark mode */
#define COLOR_DARK_BLUE_DARK QColor("#0b1018")
/* Pricing widget background color */
#define COLOR_PRICING_WIDGET QColor("#161e2d")
/* AokChain dark mode administrator background color */
#define COLOR_ADMIN_CARD_DARK COLOR_BLACK
/* AokChain dark mode regular token background color */
#define COLOR_REGULAR_CARD_DARK_BLUE_DARK_MODE QColor("#06132a")
/* AokChain dark mode regular token background color */
#define COLOR_REGULAR_CARD_LIGHT_BLUE_DARK_MODE QColor("#0e1b3b")
/* Toolbar not selected text color */
#define COLOR_TOOLBAR_NOT_SELECTED_TEXT_DARK_MODE QColor("#6c80c5")
/* Toolbar selected text color */
#define COLOR_TOOLBAR_SELECTED_TEXT_DARK_MODE QColor("#E7ECF1")

/** AOKCHAIN STYLE */
/* AokChain light dark */
#define COLOR_LIGHT_DARK QColor("#1e2024")


/* AokChain label color as a string */
#define STRING_LABEL_COLOR GUIUtil::labelColorStyle()
// #define STRING_LABEL_COLOR "color: #4960ad"








/* Tooltips longer than this (in characters) are converted into rich text,
   so that they can be word-wrapped.
 */
static const int TOOLTIP_WRAP_THRESHOLD = 80;

/* Maximum allowed URI length */
static const int MAX_URI_LENGTH = 255;

/* QRCodeDialog -- size of exported QR Code image */
#define QR_IMAGE_SIZE 300

/* Number of frames in spinner animation */
#define SPINNER_FRAMES 36

#define QAPP_ORG_NAME "AokChain"
#define QAPP_ORG_DOMAIN "aokchain.org"
#define QAPP_APP_NAME_DEFAULT "AokChain-Qt"
#define QAPP_APP_NAME_TESTNET "AokChain-Qt-testnet"

#endif // AOKCHAIN_QT_GUICONSTANTS_H
