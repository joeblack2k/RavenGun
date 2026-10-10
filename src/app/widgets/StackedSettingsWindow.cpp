// SPDX-License-Identifier: GPL-2.0-or-later
// Overgenomen uit Dolphin (GPL-2.0-or-later), commit e6f3ae17627e4344da95b13424af5baf4c892b08:
//   Source/Core/DolphinQt/Config/SettingsWindow.cpp (StackedSettingsWindow)
// Copyright 2016 Dolphin Emulator Project.
#include "widgets/StackedSettingsWindow.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPalette>
#include <QScreen>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "widgets/QtUtils.h"

namespace rg::ui {

StackedSettingsWindow::StackedSettingsWindow(QWidget* parent) : QDialog{parent} {
    // This eliminates the ugly line between the title bar and window contents with KDE Plasma.
    setStyleSheet(QStringLiteral("QDialog { border: none; }"));

    auto* const layout = new QHBoxLayout{this};

    // Calculated value for the padding in our list items.
    const int list_item_padding = layout->contentsMargins().left() / 2;

    // Eliminate padding around layouts.
    layout->setContentsMargins(QMargins{});
    layout->setSpacing(0);

    m_navigation_list = new QListWidget;

    // Ensure list doesn't grow horizontally and is not resized smaller than its contents.
    m_navigation_list->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Minimum);
    m_navigation_list->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);

    m_navigation_list->setStyleSheet(
        QString::fromUtf8(
            // Remove border around entire widget and adjust background color.
            "QListWidget { border: 0; background: %1; } "
            "QListWidget::item { padding-top: %2px; padding-bottom: %2px; } "
            // Maintain selected item color when unfocused.
            "QListWidget::item:selected { background: palette(highlight); "
            // Prevent text color change on focus loss.
            "color: palette(highlighted-text); "
            "} "
            // Remove ugly dotted outline on selected row (Windows and GNOME).
            "* { outline: none; } ")
            .arg(QStringLiteral("palette(alternate-base)"))
            .arg(list_item_padding));

    UpdateNavigationListStyle();

    layout->addWidget(m_navigation_list);

    auto* const right_side = new QVBoxLayout;
    layout->addLayout(right_side);

    m_stacked_panes = new QStackedWidget;
    right_side->addWidget(m_stacked_panes);

    // The QFrame gives us some padding around the button.
    auto* const button_frame = new QFrame;
    auto* const button_layout = new QGridLayout{button_frame};
    m_button_box = new QDialogButtonBox(QDialogButtonBox::Close);
    right_side->addWidget(button_frame);
    button_layout->addWidget(m_button_box);

    connect(m_button_box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_navigation_list, &QListWidget::currentRowChanged, m_stacked_panes, &QStackedWidget::setCurrentIndex);
}

void StackedSettingsWindow::OnDoneCreatingPanes() {
    // Make sure the first item is actually selected by default.
    ActivatePane(0);
    // Take on the preferred size (Dolphin: QtUtils::AdjustSizeWithinScreen).
    adjustSize();
    if (const QScreen* s = screen()) {
        const QSize avail = s->availableSize() * 0.9;
        resize(size().boundedTo(avail));
    }
}

void StackedSettingsWindow::changeEvent(QEvent* event) {
    QDialog::changeEvent(event);
    const auto type = event->type();
    if (type == QEvent::PaletteChange || type == QEvent::ApplicationPaletteChange || type == QEvent::StyleChange ||
        type == QEvent::ThemeChange)
        UpdateNavigationListStyle();
}

void StackedSettingsWindow::UpdateNavigationListStyle() {
    if (!m_navigation_list) return;
    QPalette list_palette = m_navigation_list->palette();
    const QPalette app_palette = qApp->palette();
    const QColor highlight_color = app_palette.color(QPalette::Active, QPalette::Highlight);
    const QColor highlighted_text = app_palette.color(QPalette::Active, QPalette::HighlightedText);
    for (const QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive}) {
        list_palette.setColor(group, QPalette::Base, app_palette.color(group, QPalette::Base));
        list_palette.setColor(group, QPalette::AlternateBase, app_palette.color(group, QPalette::AlternateBase));
        list_palette.setColor(group, QPalette::Highlight, highlight_color);
        list_palette.setColor(group, QPalette::HighlightedText, highlighted_text);
    }
    m_navigation_list->setPalette(list_palette);
}

void StackedSettingsWindow::AddPane(QWidget* widget, const QString& name) {
    m_stacked_panes->addWidget(widget);
    // Pad the left and right of each item.
    m_navigation_list->addItem(QStringLiteral("  %1  ").arg(name));
}

void StackedSettingsWindow::AddWrappedPane(QWidget* widget, const QString& name) { AddPane(GetWrappedWidget(widget), name); }

void StackedSettingsWindow::ActivatePane(int index) { m_navigation_list->setCurrentRow(index); }

int StackedSettingsWindow::PaneCount() const { return m_stacked_panes->count(); }

QString StackedSettingsWindow::PaneName(int index) const {
    auto* item = m_navigation_list->item(index);
    return item ? item->text().trimmed() : QString();
}

}  // namespace rg::ui
