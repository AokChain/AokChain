// Copyright (c) 2026 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/aokchain-config.h"
#endif

#include "firstrun.h"

#include "guiutil.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

QPixmap WizardHeader::logo(int size, qreal dpr)
{
    QPixmap source(":/icons/aok_logo");
    QPixmap rounded(QSize(size, size) * dpr);
    rounded.setDevicePixelRatio(dpr);
    rounded.fill(Qt::transparent);
    QPainter p(&rounded);
    p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform, true);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(0, 0, size, size), size / 4.0, size / 4.0);
    p.setClipPath(clip);
    p.drawPixmap(QRectF(0, 0, size, size), source, QRectF(source.rect()));
    p.end();
    return rounded;
}

WizardHeader::WizardHeader(Step step, const QString &title, const QString &subtitle, QWidget *parent) :
    QWidget(parent)
{
    const GUIUtil::Theme &t = GUIUtil::theme();
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    QHBoxLayout *brand = new QHBoxLayout();
    brand->setSpacing(12);
    QLabel *logoLabel = new QLabel(this);
    logoLabel->setFixedSize(44, 44);
    logoLabel->setPixmap(logo(44, devicePixelRatioF()));
    brand->addWidget(logoLabel);
    QVBoxLayout *words = new QVBoxLayout();
    words->setSpacing(0);
    QLabel *name = new QLabel(tr(PACKAGE_NAME), this);
    name->setFont(GUIUtil::appFont(13, QFont::Medium));
    name->setStyleSheet(QString("color: %1;").arg(t.textSecondary.name()));
    words->addWidget(name);
    QLabel *titleLabel = new QLabel(title, this);
    titleLabel->setFont(GUIUtil::appFont(20, QFont::Medium));
    titleLabel->setStyleSheet(QString("color: %1;").arg(t.text.name()));
    words->addWidget(titleLabel);
    brand->addLayout(words, 1);
    layout->addLayout(brand);

    // the steps: a numbered dot and a name each, the current one filled
    QHBoxLayout *steps = new QHBoxLayout();
    steps->setSpacing(8);
    const QStringList names = {tr("Start"), tr("Wallet passphrase"), tr("Backup")};
    for (int i = 0; i < names.size(); ++i) {
        const bool current = i == step;
        const bool done = i < step;
        QLabel *dot = new QLabel(done ? QString::fromUtf8("✓") : QString::number(i + 1), this);
        dot->setFixedSize(22, 22);
        dot->setAlignment(Qt::AlignCenter);
        dot->setFont(GUIUtil::appFont(11, QFont::Medium));
        dot->setStyleSheet(QString("background-color: %1; color: %2; border-radius: 11px;")
            .arg((current ? t.fill : done ? t.accentSoft : t.chip).name(), (current ? t.onFill : done ? t.accent : t.chipText).name()));
        steps->addWidget(dot);
        QLabel *label = new QLabel(names.at(i), this);
        label->setFont(GUIUtil::appFont(12, current ? QFont::Medium : QFont::Normal));
        label->setStyleSheet(QString("color: %1;").arg((current ? t.text : t.textSecondary).name()));
        steps->addWidget(label);
        if (i + 1 < names.size()) {
            QFrame *rule = new QFrame(this);
            rule->setProperty("role", "hline");
            rule->setMinimumWidth(16);
            steps->addWidget(rule, 1);
        }
    }
    layout->addLayout(steps);

    if (!subtitle.isEmpty()) {
        QLabel *sub = new QLabel(subtitle, this);
        sub->setWordWrap(true);
        sub->setProperty("role", "secondary");
        layout->addWidget(sub);
    }
}

