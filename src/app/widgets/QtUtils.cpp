// SPDX-License-Identifier: GPL-2.0-or-later
// Overgenomen uit Dolphin (GPL-2.0-or-later), commit e6f3ae17627e4344da95b13424af5baf4c892b08:
//   Source/Core/DolphinQt/QtUtils/WrapInScrollArea.cpp en NonDefaultQPushButton.cpp
// Copyright 2018/2022 Dolphin Emulator Project.
#include "widgets/QtUtils.h"

#include <QFrame>
#include <QLayout>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>

namespace rg::ui {

NonDefaultQPushButton::NonDefaultQPushButton(const QString& text, QWidget* parent) : QPushButton(text, parent) {
    setAutoDefault(false);
}

namespace {
// This scroll area prefers the size of its underlying widget.
class HintingScrollArea final : public QScrollArea {
public:
    HintingScrollArea() {
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        setWidgetResizable(true);
        setFrameStyle(QFrame::NoFrame);
    }
    QSize sizeHint() const override {
        // Including the scrollbars in our hint can prevent a window undersized in one direction gaining unnecessary
        // scrolling in both directions.
        const auto scrollbar_padding = QSize{verticalScrollBar()->sizeHint().width(), horizontalScrollBar()->sizeHint().height()};
        const auto size_hint = widget()->sizeHint();
        return size_hint + scrollbar_padding;
    }
};
}  // namespace

QWidget* GetWrappedWidget(QWidget* wrapped_widget) {
    auto* const scroll = new HintingScrollArea;
    scroll->setWidget(wrapped_widget);
    scroll->viewport()->setAutoFillBackground(false);
    wrapped_widget->setAutoFillBackground(false);
    return scroll;
}

void WrapInScrollArea(QWidget* parent, QLayout* wrapped_layout) {
    auto* widget = new QWidget;
    widget->setLayout(wrapped_layout);
    auto* scroll_area = GetWrappedWidget(widget);
    auto* scroll_layout = new QVBoxLayout;
    scroll_layout->addWidget(scroll_area);
    scroll_layout->setContentsMargins(0, 0, 0, 0);
    parent->setLayout(scroll_layout);
}

}  // namespace rg::ui
